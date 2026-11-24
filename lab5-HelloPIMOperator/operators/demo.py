"""Independent operator fixtures for Lab 5. Use ONLY=name to isolate a kernel."""
import argparse
import numpy as np
from runtime import Hardware, OPS, TILE

def run(hw, name):
    a = np.array([1, -2, 3, -4], np.float32)
    b = np.array([2, 3, -1, 1], np.float32)
    alpha = 0.25
    if name == "linear":
        x = np.resize(np.array([-8, 7, -1, 0], np.int8), TILE)
        w = ((np.arange(TILE*TILE).reshape(TILE, TILE) * 11 + np.arange(TILE)[:,None]) % 16 - 8).astype(np.int8)
        got = hw.raw(x, w)
        np.testing.assert_array_equal(got, w.astype(np.int32) @ x.astype(np.int32))
        print("PASS linear (128 signed INT32 outputs)")
        return
    if name == "rope":
        b = np.array([0, 1, 1, 0], np.float32)
    expected = {
        "add": lambda: a+b,
        "rms": lambda: a*b / np.sqrt(np.mean(a*a) + 1e-6),
        "rope": lambda: np.array([-3, -2, 1, -4]),
        "dot": lambda: np.array([a @ b]),
        "softmax": lambda: np.exp(a-a.max()) / np.exp(a-a.max()).sum(),
        "swiglu": lambda: a*b / (1 + np.exp(-a)),
        "axpy": lambda: a + alpha*b,
    }[name]()
    for lane in range(8):
        got = hw.vector(name, a, b, alpha=alpha, lane=lane)
        np.testing.assert_allclose(got, expected, rtol=2e-5, atol=2e-6)
    print(f"PASS {name} (8 DarkRISCV lanes)", flush=True)

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("build")
    parser.add_argument("only", choices=("linear",) + OPS, nargs="?")
    args = parser.parse_args()
    with Hardware(args.build) as hw:
        for name in ([args.only] if args.only else ("linear",) + OPS):
            run(hw, name)
