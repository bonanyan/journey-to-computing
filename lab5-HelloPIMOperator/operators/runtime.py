"""Host-side tensor layout, quantization and transport to the Verilated blocks."""
from pathlib import Path
import struct
import subprocess

import numpy as np

TILE = 128
OPS = ("add", "rms", "rope", "dot", "softmax", "swiglu", "axpy")


def quantize(x):
    """Symmetric signed INT4, one FP32 scale per row of up to 128 entries."""
    peak = np.max(np.abs(x), axis=-1, keepdims=True)
    scale = np.where(peak == 0, np.float32(1), peak / np.float32(7))
    return np.rint(x / scale).clip(-7, 7).astype(np.int8), scale[..., 0]


class Hardware:
    def __init__(self, build):
        build = Path(build)
        self.pim = subprocess.Popen([build / "pim/Vmpu"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        try:
            self.lanes = subprocess.Popen([build / "lane/Vdarkriscv", build / "vector.bin"],
                                          stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        except OSError:
            self.pim.stdin.close()
            self.pim.wait()
            raise
        self.jobs = 0
        self.calls = 0

    def __enter__(self):
        return self

    def __exit__(self, typ, value, tb):
        for proc in (self.pim, self.lanes):
            proc.stdin.close()
        codes = [proc.wait() for proc in (self.pim, self.lanes)]
        if typ is None and any(codes):
            raise RuntimeError(f"Verilator subprocess exit codes {codes}")

    @staticmethod
    def exchange(proc, payload, count, dtype):
        proc.stdin.write(payload)
        proc.stdin.flush()
        data = proc.stdout.read(count * 4)
        if len(data) != count * 4:
            raise RuntimeError("Verilator stopped before returning a complete result")
        return np.frombuffer(data, dtype=dtype)

    def vector(self, op, x, y=None, *, alpha=0, lane=0):
        x = np.asarray(x, dtype="<f4")
        y = np.zeros_like(x) if y is None else np.asarray(y, dtype="<f4")
        if x.ndim != 1 or x.shape != y.shape or not 0 < x.size <= 16384 or not 0 <= lane < 8:
            raise ValueError("lane expects equal-length vectors of 1..16384 elements and lane 0..7")
        if op == "rope" and x.size % 2:
            raise ValueError("rotary heads need an even width")
        header = struct.pack("<IIIf", OPS.index(op) + 1, x.size, lane, alpha)
        self.calls += 1
        return self.exchange(self.lanes, header + x.tobytes() + y.tobytes(),
                             1 if op == "dot" else x.size, "<f4")

    def raw(self, x, w):
        x, w = np.asarray(x), np.asarray(w)
        if x.shape != (TILE,) or w.shape != (TILE, TILE) or x.dtype != np.int8 or w.dtype != np.int8:
            raise ValueError("raw expects an int8[128] and an int8[128,128]")
        if x.min() < -8 or x.max() > 7 or w.min() < -8 or w.max() > 7:
            raise ValueError("INT4 operand outside [-8,7]")
        packed = (w[:, ::2] & 15) | ((w[:, 1::2] & 15) << 4)
        self.jobs += 1
        return self.exchange(self.pim, x.tobytes() + packed.tobytes(), TILE, "<i4")

    def linear(self, x, w):
        """w is output-major FP32. Quantize each row independently along K."""
        n, k = w.shape
        if x.shape != (k,):
            raise ValueError("activation length must match the weight row")
        out = np.zeros(n, dtype=np.float32)
        for first in range(0, k, TILE):
            last = min(first + TILE, k)
            qx, sx = quantize(x[first:last])
            xp = np.zeros(TILE, np.int8)
            xp[:last-first] = qx
            for row in range(0, n, TILE):
                limit = min(row + TILE, n)
                qw, sw = quantize(w[row:limit, first:last])
                wp = np.zeros((TILE, TILE), np.int8)
                wp[:limit-row, :last-first] = qw
                partial = self.raw(xp, wp)[:limit-row]
                out[row:limit] += partial.astype(np.float32) * (sx * sw)
        return out
