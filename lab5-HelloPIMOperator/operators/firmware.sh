#!/bin/sh
set -eu
src=$1
build=$2
mkdir -p "$build"
if command -v riscv64-unknown-elf-gcc >/dev/null 2>&1; then
    riscv64-unknown-elf-gcc -march=rv32i -mabi=ilp32 -Os -ffreestanding -msmall-data-limit=0 -nostdlib -Wl,--no-relax -T "$src/kernel.ld" "$src/kernels.S" "$src/vector.c" -lgcc -o "$build/vector.elf"
    riscv64-unknown-elf-objcopy -O binary "$build/vector.elf" "$build/vector.bin"
    riscv64-unknown-elf-objdump -d "$build/vector.elf" > "$build/vector.dis"
else
    zigcmd() {
        if command -v zig >/dev/null 2>&1; then zig "$@"
        else "${ZIG_PYTHON:-python3}" -m ziglang "$@"; fi
    }
    export ZIG_GLOBAL_CACHE_DIR="$build/zig-global" ZIG_LOCAL_CACHE_DIR="$build/zig-local"
    zigcmd cc --target=riscv32-freestanding -mcpu=generic_rv32 -mabi=ilp32 -Os -ffreestanding -msmall-data-limit=0 -nostdlib -Wl,-T,"$src/kernel.ld" "$src/kernels.S" "$src/vector.c" -o "$build/vector.elf"
    python3 "$src/elf_image.py" "$build/vector.elf" "$build/vector.bin"
    zigcmd cc --target=riscv32-freestanding -mcpu=generic_rv32 -mabi=ilp32 -Os -ffreestanding -msmall-data-limit=0 -S "$src/vector.c" -o "$build/vector.dis"
fi
