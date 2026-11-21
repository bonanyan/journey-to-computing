# Lab3 · Write a C++ simulator for a CIM array and Lab 2’s CPU–PIM system

In Step 1, implement a standalone computing-in-memory (CIM) array and estimate the latency of a matrix–vector product. In Step 2, execute Lab 2’s RISC-V firmware in C++ and model the memory-mapped digital PIM (DPIM) peripheral one rising edge at a time. The tutorial traces a command from the CPU store to the result read and shows how to compare it with Lab 2’s RTL waveform. [HTML tutorial](./lab3-tutorial.html) · [Markdown tutorial](./lab3-tutorial.md)

From this directory, run both steps with GNU Make and a C++17 compiler (GCC or Clang). The bundled firmware images are ready to execute.

```sh
make run
```

The reports are written to `results/cim.txt` and `results/cpu-dpim.txt`. Use `make` to build both executables and `make clean` to remove them.

## Step 1. Simulate the CIM array on its own

```sh
make run-cim
```

The standalone driver programs a 3 × 2 weight matrix once, then feeds it two input vectors. Each column produces one signed 64-bit sum. [Workload](./examples/example.cim) · [Standalone driver](./simulators/cim.cpp) · [Array model](./simulators/cim_array.hpp)

| Input vector | Output sums | Input cycles | Compute cycles | Readout cycles |
|---|---|---:|---:|---:|
| `2 0 -1` | `7 -10` | 1 | 9 | 1 |
| `-1 2 3` | `-10 28` | 1 | 9 | 1 |

Each vector takes 11 cycles. Programming takes 3 cycles, so the two-vector workload takes **25 cycles**. [Standalone report](./results/cim.txt)

To try another workload, copy `examples/example.cim`, edit it, and pass the file to `./build/cim`. The first three integers give the row count, column count, and vector count. Supply the weights in row-major order, followed by one input per vector, each containing one value per row. Weights and inputs are decimal integers in `-128..127`. Run `./build/cim` for the built-in demo or `./build/cim -` to read a workload from standard input.

## Step 2. Run Lab 2 firmware on the C++ CPU and DPIM model

```sh
make run-cpu-cim
./build/cosim examples/lab2/row_switch.hex --trace
```

The first command builds the simulator and runs the four Lab 2 assembly benchmarks. Each image executes from SRAM and reports its own pass or failure through the X-HEEP exit registers. The trace follows the CPU's DPIM loads and stores and the compute edge for each accepted command. In `row_switch`, lane 31 produces 48, 335, and 160. A byte store to COMMAND between the first two operations preserves 48. [Run report](./results/cpu-dpim.txt) · [C++ peripheral](./simulators/dpim.hpp)

The CPU executes the RV32IMC instructions used by the benchmarks. The `dpim_bus_edges` count measures time spent servicing DPIM accesses. For example, `row_switch` uses 54 bus edges for 18 transactions. Whole-system timing, including the CV32E20 pipeline and time between accesses, comes from the Lab 2 RTL run. [CPU model](./simulators/rv32.hpp) · [Lab 2 tutorial](../lab2-CPU+PIM/lab2-tutorial.html)

### Verify the C++ model against the RTL

With Lab 2 configured and Python 3.8+ and `fst2vcd` available, run the following from the Lab 3 directory. The checker runs the same hex image as the RTL and compares each DPIM transaction’s direction, address, byte enables, data, and grant-to-response latency. [Lab 2 setup](../lab2-CPU+PIM/README.md) · [Waveform checker](./tools/compare_obi.py)

```sh
make build/cosim
make -C ../lab2-CPU+PIM test-row_switch
python3 tools/compare_obi.py ../lab2-CPU+PIM/build/tests/row_switch.hex ../lab2-CPU+PIM/build/tests/row_switch.fst
```

Expect `PASS row_switch matched 18 DPIM transactions and response latencies`. The four reference benchmarks together matched **8,488 transactions**. [Full-suite verification instructions](./lab3-tutorial.html#check-the-model-against-the-rtl)

The bundled hex files come from the Lab 2 assembly sources and startup code, copyright 2026 Dr. Bonan Yan, licensed under Apache 2.0. To run a modified benchmark, build it in Lab 2 with `make test-<name>` and give its `build/tests/<name>.hex` to `./build/cosim`. [Assembly sources](../lab2-CPU+PIM/design/sw/benchmarks/) · [Firmware license](./examples/lab2/LICENSE)
