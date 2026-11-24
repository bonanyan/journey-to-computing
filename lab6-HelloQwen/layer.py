"""Decode one Qwen3 layer on the Verilated MPU and RV32I lanes."""
import argparse
from dataclasses import dataclass
from pathlib import Path

import numpy as np
from runtime import Hardware, TILE
from reference import prefill

@dataclass(frozen=True)
class Shape:
    hidden: int
    intermediate: int
    heads: int
    kv_heads: int
    head_dim: int

SMALL = Shape(32, 64, 4, 2, 8)
QWEN3 = Shape(2560, 9728, 32, 8, 128)

def shapes(s):
    h, i, q, kv, d = s.hidden, s.intermediate, s.heads*s.head_dim, s.kv_heads*s.head_dim, s.head_dim
    return {"q": (q,h), "k": (kv,h), "v": (kv,h), "o": (h,q), "gate": (i,h), "up": (i,h),
            "down": (h,i), "input_norm": (h,), "post_norm": (h,), "q_norm": (d,), "k_norm": (d,)}

def weights(s, folder):
    values = {}
    rng = np.random.default_rng(304)
    for name, shape in shapes(s).items():
        if folder is not None:
            value = np.load(folder / (name + ".npy"), mmap_mode="r", allow_pickle=False)
            if value.shape != shape or value.dtype != np.float32 or not np.isfinite(value).all():
                raise ValueError(f"{name} must hold finite float32 values of shape {shape}")
        elif len(shape) == 2:
            value = rng.integers(-7, 8, size=shape, dtype=np.int8).astype(np.float32)
            value *= np.float32(0.2 / np.sqrt(shape[1]))
        else:
            value = rng.uniform(0.8, 1.2, shape).astype(np.float32)
        values[name] = value
    return values

def decode(hw, s, w, x, keys, values):
    p = len(keys)
    vec = hw.vector
    ops = {}
    def elementwise(name, left, right):
        spans = zip(np.array_split(left, 8), np.array_split(right, 8))
        return np.concatenate([vec(name, a, b, lane=lane) for lane, (a,b) in enumerate(spans) if len(a)])

    def project(name, vector):
        out = hw.linear(vector, w[name])
        ops[name] = out
        return out

    z = vec("rms", x, w["input_norm"])
    q = project("q", z).reshape(s.heads, s.head_dim)
    k = project("k", z).reshape(s.kv_heads, s.head_dim)
    v = project("v", z).reshape(s.kv_heads, s.head_dim)
    angles = p * 1_000_000.0 ** (-2 * np.arange(s.head_dim//2) / s.head_dim)
    rotation = np.concatenate((np.cos(angles), np.sin(angles))).astype(np.float32)
    def prepare(heads, gamma):
        return np.stack([vec("rope", vec("rms", head, gamma, lane=h%8), rotation, lane=h%8)
                         for h, head in enumerate(heads)])
    q = prepare(q, w["q_norm"])
    k = prepare(k, w["k_norm"])
    # The pending K,V participate in attention. Commit to the cache after successful writeback.
    all_k, all_v = keys + [k], values + [v]
    context = np.empty_like(q)
    for h in range(s.heads):
        kv = h // (s.heads // s.kv_heads)
        scores = [vec("dot", q[h], past[kv], lane=h%8)[0] for past in all_k]
        probs = vec("softmax", np.asarray(scores, np.float32) / np.float32(np.sqrt(s.head_dim)), lane=h%8)
        acc = np.zeros(s.head_dim, np.float32)
        for prob, past in zip(probs, all_v):
            acc = vec("axpy", acc, past[kv], alpha=prob, lane=h%8)
        context[h] = acc
    ops["context"] = context.ravel()
    residual = elementwise("add", x, project("o", context.ravel()))
    z = vec("rms", residual, w["post_norm"])
    product = elementwise("swiglu", project("gate", z), project("up", z))
    out = elementwise("add", residual, project("down", product))
    keys.append(k)
    values.append(v)
    ops.update(key_cache=k, value_cache=v, output=out)
    return ops

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--size", choices=("small", "qwen3"), required=True)
    ap.add_argument("--build", required=True)
    ap.add_argument("--tokens", type=int, default=2)
    ap.add_argument("--weights", type=Path)
    args = ap.parse_args()
    if not 1 <= args.tokens <= 16384:
        ap.error("choose 1..16384 positions (lane scratch capacity)")
    s = SMALL if args.size == "small" else QWEN3
    w = weights(s, args.weights)
    x = np.random.default_rng(2026).standard_normal((args.tokens, s.hidden)).astype(np.float32)
    print(f"One layer: {s}, tokens={len(x)}, weights={args.weights or 'synthetic seed 304'}", flush=True)
    golden = prefill(s, w, x)
    keys, values = [], []
    tiles = sum(((v.shape[0]+TILE-1)//TILE)*((v.shape[1]+TILE-1)//TILE)
                for v in w.values() if v.ndim == 2)
    with Hardware(args.build) as hw:
        for p, token in enumerate(x):
            ops = decode(hw, s, w, token, keys, values)
            for name, actual in ops.items():
                np.testing.assert_allclose(actual, golden[name][p], atol=0.006, rtol=0.002,
                                           err_msg=f"position {p}, {name}")
            assert hw.jobs == (p+1)*tiles and len(keys) == p+1
            err = np.max(np.abs(ops["output"]-golden["output"][p]))
            print(f"PASS position={p} output={s.hidden} cache={len(keys)} projections=7 "
                  f"MPU_tiles={tiles} max_abs_error={err:.6g}", flush=True)
        print(f"PASS one decoder layer: {hw.jobs} MPU jobs, {hw.calls} DarkRISCV dispatches")

if __name__ == "__main__":
    main()
