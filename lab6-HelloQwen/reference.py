"""Independent batched causal reference. Computes every prefix without a mutable KV cache."""
import numpy as np

def linear(x, weight):
    output = np.zeros((len(x), len(weight)), np.float32)
    for first in range(0, x.shape[-1], 128):
        a = x[:, first:first+128]
        b = weight[:, first:first+128]
        sa = np.abs(a).max(axis=1) / np.float32(7)
        sb = np.abs(b).max(axis=1) / np.float32(7)
        sa[sa == 0] = 1
        sb[sb == 0] = 1
        ai = np.rint(a / sa[:,None]).clip(-7, 7).astype(np.int32)
        bi = np.rint(b / sb[:,None]).clip(-7, 7).astype(np.int32)
        output += (ai @ bi.T).astype(np.float32) * (sa[:,None] * sb[None,:])
    return output

def norm(x, gamma):
    return x / np.sqrt(np.mean(x*x, axis=-1, keepdims=True) + np.float32(1e-6)) * gamma

def prefill(s, weights, x):
    saved = {}
    def proj(name, inp):
        saved[name] = linear(inp, weights[name])
        return saved[name]

    t, d = len(x), s.head_dim
    z = norm(x, weights["input_norm"])
    q = norm(proj("q", z).reshape(t, s.heads, d), weights["q_norm"])
    k = norm(proj("k", z).reshape(t, s.kv_heads, d), weights["k_norm"])
    v = proj("v", z).reshape(t, s.kv_heads, d)
    freq = (1_000_000.0 ** (-np.arange(0, d, 2) / d))
    angle = np.outer(np.arange(t), freq)
    angle = np.concatenate([angle, angle], axis=1)[:,None,:]
    def rotate(a):
        other = np.concatenate([-a[..., d//2:], a[..., :d//2]], axis=-1)
        return a*np.cos(angle).astype(np.float32) + other*np.sin(angle).astype(np.float32)
    q = rotate(q)
    saved["key_cache"] = k = rotate(k)
    saved["value_cache"] = v
    shared = s.heads // s.kv_heads
    scores = np.einsum("thd,shd->hts", q, np.repeat(k, shared, axis=1)) / np.float32(np.sqrt(d))
    scores = np.where(np.tri(t, dtype=bool)[None], scores, -np.inf)
    exp = np.exp(scores - scores.max(axis=-1, keepdims=True))
    probs = exp / exp.sum(axis=-1, keepdims=True)
    ctx = np.einsum("hts,shd->thd", probs, np.repeat(v, shared, axis=1)).reshape(t, -1)
    saved["context"] = ctx
    residual = x + proj("o", ctx)
    z = norm(residual, weights["post_norm"])
    gate, up = proj("gate", z), proj("up", z)
    e = np.exp(-np.abs(gate))
    sigmoid = np.where(gate >= 0, 1, e)/(1+e)
    saved["output"] = residual + proj("down", (gate*sigmoid)*up)
    return saved
