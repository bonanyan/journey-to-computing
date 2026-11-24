"""Flatten the RV32 little-endian ELF load segments into reset ROM."""
from pathlib import Path
import struct
import sys

data = Path(sys.argv[1]).read_bytes()
if data[:6] != b"\x7fELF\x01\x01" or struct.unpack_from("<H", data, 18)[0] != 243:
    raise ValueError("expected a little-endian RV32 executable")
entry, offset = struct.unpack_from("<II", data, 24)
stride, count = struct.unpack_from("<HH", data, 42)
image = bytearray()
for i in range(count):
    kind, start, _, addr, size, mem_size = struct.unpack_from("<6I", data, offset + i * stride)
    if kind == 1:
        if entry != 0 or addr + mem_size > 0xe000 or start + size > len(data):
            raise ValueError("segment outside firmware memory")
        image.extend(bytes(max(0, addr + mem_size - len(image))))
        image[addr:addr + size] = data[start:start + size]
Path(sys.argv[2]).write_bytes(image)
