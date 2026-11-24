"""Extract eleven tensors from one layer of a local Qwen3-4B safetensors snapshot."""
import argparse
import json
from pathlib import Path

import numpy as np

TENSORS = {"input_norm": "input_layernorm", "post_norm": "post_attention_layernorm",
           "q_norm": "self_attn.q_norm", "k_norm": "self_attn.k_norm",
           **{k: "self_attn."+k+"_proj" for k in ("q", "k", "v", "o")},
           **{k: "mlp."+k+"_proj" for k in ("gate", "up", "down")}}

def extract(shard, name):
    with shard.open("rb") as file:
        size = int.from_bytes(file.read(8), "little")
        if size <= 0 or size > min(100_000_000, shard.stat().st_size - 8):
            raise ValueError(f"invalid header in {shard}")
        spec = json.loads(file.read(size))[name]
    dtype = {"BF16": "<u2", "F16": "<f2", "F32": "<f4"}[spec["dtype"]]
    lo, hi = spec["data_offsets"]
    expected = int(np.prod(spec["shape"])) * np.dtype(dtype).itemsize
    if lo < 0 or hi-lo != expected or hi+8+size > shard.stat().st_size:
        raise ValueError(f"invalid extent for {name}")
    a = np.memmap(shard, mode="r", dtype=dtype, offset=8+size+lo, shape=tuple(spec["shape"]))
    if spec["dtype"] == "BF16":
        return (a.astype("<u4") << 16).view("<f4")
    return a.astype("<f4")

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("snapshot", type=Path)
    ap.add_argument("output", type=Path)
    ap.add_argument("--layer", type=int, default=0)
    args = ap.parse_args()
    cfg = json.loads((args.snapshot/"config.json").read_text())
    required = {"model_type": "qwen3", "hidden_size": 2560, "intermediate_size": 9728,
                "head_dim": 128, "num_attention_heads": 32, "num_key_value_heads": 8,
                "rope_theta": 1000000, "rms_norm_eps": 1e-6, "hidden_act": "silu",
                "attention_bias": False, "use_sliding_window": False, "rope_scaling": None}
    if any(cfg.get(key) != value for key, value in required.items()) or not 0 <= args.layer < cfg["num_hidden_layers"]:
        ap.error("expected a supported Qwen3-4B configuration and layer index")
    index = json.loads((args.snapshot/"model.safetensors.index.json").read_text())["weight_map"]
    args.output.mkdir(parents=True, exist_ok=True)
    for short, suffix in TENSORS.items():
        name = f"model.layers.{args.layer}.{suffix}.weight"
        a = extract(args.snapshot/index[name], name)
        np.save(args.output/(short+".npy"), a, allow_pickle=False)
        print(short, a.shape)
if __name__ == "__main__":
    main()
