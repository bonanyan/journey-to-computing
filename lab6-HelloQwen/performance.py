"""Run the checked full-width layer and total the counters in the two RTL simulators."""
import argparse
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--tokens", type=int, default=1)
    parser.add_argument("--weights", type=Path)
    args = parser.parse_args()
    command = [sys.executable, "-B", str(Path(__file__).with_name("layer.py")),
               "--size", "qwen3", "--build", str(args.build), "--tokens", str(args.tokens)]
    if args.weights:
        command.extend(["--weights", str(args.weights)])
    run = subprocess.run(command, stderr=subprocess.PIPE, text=True)
    counts = {"mpu": [], "lane": []}
    for line in run.stderr.splitlines():
        if line.startswith("RTL_CYCLES "):
            _, block, cycles = line.split()
            counts[block].append(int(cycles))
        else:
            print(line, file=sys.stderr)
    if run.returncode:
        raise SystemExit(run.returncode)
    if len(counts["mpu"]) != 1 or len(counts["lane"]) != 8:
        raise SystemExit("Expected counters from one MPU and eight lanes. Build with make perf.")

    mpu = counts["mpu"][0]
    lanes = sum(counts["lane"])
    total = mpu + lanes
    print(f"MPU cycles: {mpu:,}")
    print("Lane cycles (0..7):", " + ".join(f"{n:,}" for n in counts["lane"]))
    print(f"Lane cycles (sum): {lanes:,}")
    print(f"Layer cycles for {args.tokens} token(s): {total:,}")
    print(f"At 1 GHz: {total / 1e6:.3f} ms, {args.tokens * 1e9 / total:.3f} layer tokens/s")


if __name__ == "__main__":
    main()
