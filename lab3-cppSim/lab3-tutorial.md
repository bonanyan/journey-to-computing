<p align="center">
  <img src="../assets/journey-computing-jc_v2.png" alt="Journey to Computing logo" width="144" height="86" />
</p>

Lab3 · Write a C++ simulator for a CIM array and Lab 2’s CPU–PIM system
=======================================================================

<p align="center">
  <strong>Author:</strong> Dr. Bonan Yan (<a href="mailto:bonanyan@pku.edu.cn">bonanyan@pku.edu.cn</a>) · November 22, 2026
</p>

## What to do

In Lab 2, you ran RISC-V firmware on the X-HEEP RTL and watched lane 31 produce 335. In this lab you will write the C++ that predicts that result and the clock edge when the CPU can read it. Start in Step 1 with a standalone computing-in-memory (CIM) array that sums products down each column. In Step 2, model Lab 2’s digital PIM (DPIM) macro, where each of 32 lanes produces its own 32-bit result. Run the same firmware on both implementations and compare their Open Bus Interface (OBI) transactions. [Lab 2 tutorial](../lab2-CPU+PIM/lab2-tutorial.html)

## How to run

Run the examples from the directory containing the Makefile with GNU Make and a C++17 compiler. Run the standalone CIM array first, then run the bundled Lab 2 firmware images and trace the row-switch benchmark.

```sh
make run-cim
make run-cpu-cim
./build/cosim examples/lab2/row_switch.hex --trace
```

## Step 1. Simulate the CIM array on its own

The standalone driver reads three sizes from a workload file, programs one weight matrix, and runs each input vector against those weights. The example contains three rows, two columns, and two vectors. [Workload](./examples/example.cim) · [Standalone driver](./simulators/cim.cpp)

```text
3 2 2
1 -2
3 4
-5 6
2 0 -1
-1 2 3
```

### Compute one sum per column

Store the `R × C` signed 8-bit weights in row-major order. Each vector supplies `R` signed bytes. Accumulate into `C` signed 64-bit sums. Each pass over a row adds its contribution to every output column. [CimArray::multiply](./simulators/cim_array.hpp)

```cpp
std::vector<std::int64_t> y(cols_, 0);
for (std::size_t r = 0; r < rows_; ++r) {
    for (std::size_t c = 0; c < cols_; ++c) {
        y[c] += std::int64_t(x[r]) * weights_[r * cols_ + c];
    }
}
```

Check the first vector by hand. The first column gives `2×1 + 0×3 + (−1)×(−5) = 7`. The second gives `2×(−2) + 0×4 + (−1)×6 = −10`. The rectangular matrix makes a swapped row and column index easy to spot, and the negative values exercise signed multiplication.

### Account for programming and per-vector latency

Model a 64-row × 32-column tile and a 16-byte-per-cycle bus. A transfer of `n` bytes takes `beats(n) = ceil(n / 16)` cycles. Round each transfer separately. Programming starts a fresh transfer for each row, so even a two-byte row costs one cycle. [TimingModel](./simulators/timing.hpp)

| Phase | Cycles | For the 3 × 2 example |
|---|---|---:|
| Program weights once | `R × beats(C)` | 3 |
| Transfer one input | `beats(R)` | 1 |
| Compute one vector | `ceil(R / 64) × ceil(C / 32) × 9` | 9 |
| Transfer one output | `beats(8 × C)` | 1 |

The nine compute cycles represent eight activation-bit steps and one merge per tile. Columns in a tile operate in parallel. When the matrix spans several tiles, the standalone controller visits them sequentially. The C++ loop calculates the values directly, while the timing formula accounts for the modeled parallelism.

Read the two vector lines in the report. Their outputs should be `7 -10` and `-10 28`, each with `latency=11`. The weights stay resident for the second vector, giving `3 + 2 × (1 + 9 + 1) = 25` total cycles. [Standalone report](./results/cim.txt)

To check weight reuse yourself, copy the workload, change the vector count from two to three, and append the vector `1 0 0`. Run `./build/cim` with the edited file as its argument. The new vector selects the first row of weights, giving `1 -2`. The total should rise from 25 to 36 cycles.

## Step 2. Model Lab 2's RISC-V CPU and digital PIM peripheral in C++

Start with the program you ran in Lab 2. In `row_switch.S`, the CPU programs two weights in lane 31, first addresses row 0, then row 127, then returns to row 0. Run the four Lab 2 firmware images and trace this one in detail. The bundled images contain the assembled instructions and data used by the RTL testbench. This step needs only the host C++ compiler and Make. [Assembly source](../lab2-CPU+PIM/design/sw/benchmarks/row_switch.S) · [Firmware images](./examples/lab2/)


### Start with the CPU's load and store addresses

Load the hex image into 128 KiB of SRAM. Set the program counter to the linked entry point, `0x180`. Fetch the first 16 bits. If the low two bits are both one, fetch the next halfword to complete a 32-bit instruction. The instruction model executes the RV32IMC subset used by the lab firmware, including the branches in its polling and result-checking loops. At the end, the startup code writes the return value to `0x20000004` and asserts exit-valid at `0x20000000`. This gives each benchmark its own pass or failure code. [CPU model](./simulators/rv32.hpp) · [SRAM and address decoder](./simulators/lab2_system.hpp) · [Lab 2 startup](../lab2-CPU+PIM/design/sw/start.S)

Route a data access in the 32 KiB window starting at `0xC0030000` to the DPIM wrapper. The C++ address decoder stands in for X-HEEP's bus demultiplexer and external crossbar. For a naturally aligned load or store, the CPU sends a word-aligned bus address and a four-bit byte enable. A word selects all four bytes. A byte at offset 2 selects `be=0x4`. [Lab 2 register definitions](../lab2-CPU+PIM/design/sw/dpim.h) · [RTL wrapper](../lab2-CPU+PIM/design/rtl/dpim_obi.sv)

| CPU-visible item | Byte offset from DPIM base | State to keep in C++ |
|---|---|---|
| Weight for lane `i`, row `r` | `4 × (128 × i + r)` | 4,096 words, organized as 32 lanes with 128 rows each |
| COMMAND | `0x4000` | Latch the command on a full-word store |
| STATUS | `0x4004` | DONE in bit 0, initially zero |
| INPUT for lane `i` | `0x4100 + 4 × i` | One 32-bit operand per lane, writable by byte |
| RESULT for lane `i` | `0x4200 + 4 × i` | One registered 32-bit result per lane. Reads return zero until DONE |

To find lane 31, row 127, add `4 × (128 × 31 + 127) = 0x3FFC` to the base. The firmware stores 67 at that address. It stages 5 at `0xC003417C`, the INPUT address for lane 31. The same operand stays in place as the CPU switches rows.

### Translate one rising edge into one C++ call

Give the wrapper the four states in `dpim_obi.sv`. A call to `Dpim::tick` samples the response visible before the rising edge and advances the stored state. This order lets the CPU observe a grant and a completion on the same edges it would sample in the RTL. [C++ peripheral](./simulators/dpim.hpp) · [RTL wrapper](../lab2-CPU+PIM/design/rtl/dpim_obi.sv)

| State before the edge | What happens on the edge |
|---|---|
| `Idle` | With a request present, assert grant and latch address, write data, byte enables, and direction. An accepted full-word COMMAND clears DONE. |
| `Access` | Perform the scalar read, full-word weight write, masked INPUT update, or compute. A partial weight store first reads the old word into `scalarQ`. |
| `MergeWrite` | For a partial weight store, write `(scalarQ & ~mask) \| (wdata & mask)`. Each enabled byte contributes eight ones to `mask`. |
| `Response` | Assert `rvalid`, supply the selected read data, and return to `Idle`. |

If the CPU stores `0x7F` to COMMAND on edge **G** with all four byte enables, the macro computes on **G + 1** and the CPU samples the write response on **G + 2**. A partial weight store uses the extra merge edge and completes on **G + 3**. A byte store to COMMAND receives a response and preserves DONE and all previous results.

On the compute edge, take the row from command bits 6–0 and the operation from bits 9–8. Each of the 32 lanes uses its own staged input and the weight at the selected row. The four operations are low-32-bit multiply (0), AND (1), OR (2), and XOR (3). Use unsigned 32-bit words so an overflowing product wraps as it does in the Verilog. The loop below shows the multiply case. All 32 results become visible together when this call to `tick` finishes. [Verilog macro](../lab2-CPU+PIM/design/third_party/pislib/DPIM.v) · [C++ implementation](./simulators/dpim.hpp)

```cpp
for (unsigned i = 0; i < results.size(); ++i) {
    results[i] = operands[i] * weights[128 * i + row];
}
```

### Follow lane 31 through three commands

Find the loads from RESULT at `0xC003427C` in the trace. The initial read is zero. The next four reads show when the macro replaces its registered output. [Row-switch firmware](../lab2-CPU+PIM/design/sw/benchmarks/row_switch.S)

| CPU action | What sets or preserves the value | Next RESULT read |
|---|---|---:|
| Word store selecting row 0 | Multiply weight 16 by input 3 | `0x30` (48) |
| Rewrite row 0 to 32, change the input to 5, then byte-store `0x7F` to COMMAND | Only one command byte is enabled. The previous result holds. | `0x30` (48) |
| Word store selecting row 127 | Multiply weight 67 by input 5 | `0x14F` (335) |
| Word store selecting row 0 again | Multiply the rewritten weight 32 by the retained input 5 | `0xA0` (160) |

Find the full-word store selecting row 127. The simulator prints each OBI transaction when its response arrives, so its compute message appears first.

```text
compute edge=38 op=0 row=127 lane31=335
store addr=0xc0034000 be=0xf data=0x7f grant=37 response=39
load addr=0xc0034004 be=0xf data=0x1 grant=40 response=42
load addr=0xc003427c be=0xf data=0x14f grant=43 response=45
```

Read the edge numbers in order. The wrapper grants the command store on edge 37, captures all lane results on edge 38, and returns the store response on edge 39. The CPU then reads DONE from STATUS and `0x14f` (335) from RESULT. In Lab 2’s reference waveform, lane 31 captures 335 at 2,025 ns. The C++ edge counter advances only while the peripheral services a transaction. Use `response − grant` to compare each access and the RTL waveform to measure elapsed time across the whole SoC. [Lab 2 waveform walkthrough](../lab2-CPU+PIM/lab2-tutorial.html)

### Check the model against the RTL

Each firmware image checks its own result and writes zero to the SoC exit-value register on success. The C++ report also counts DPIM transactions, compute commands, and clock edges spent servicing those transactions. In `memory_sweep`, six partial weight stores each require one extra edge, giving `3 × 8206 + 6 = 24624` bus edges. [Run report](./results/cpu-dpim.txt)

| Firmware | What the CPU checks | DPIM transactions | Compute commands | DPIM bus edges |
|---|---|---:|---:|---:|
| `row_switch` | Reset, boundary rows, ignored partial command, result retention | 18 | 3 | 54 |
| `vector_mul` | All 32 row-127 products against the CPU's own overflowing multiply | 98 | 1 | 294 |
| `bit_masks` | AND, OR, and XOR across all 32 lanes | 166 | 3 | 498 |
| `memory_sweep` | Every weight word, byte and halfword writes, signed and unsigned loads | 8,206 | 0 | 24,624 |

To compare an individual access, generate a waveform in a configured Lab 2 checkout. From the Lab 3 directory, run the following commands with Python 3.8+ and GTKWave's `fst2vcd` on your path. Pass the hex image and FST from the **same RTL run**. The checker executes that image in C++, pairs each granted transaction with its response in both runs, and stops at the first difference in direction, address, byte enables, data, or response latency. [Waveform checker](./tools/compare_obi.py) · [Lab 2 setup](../lab2-CPU+PIM/README.md)

```sh
make -C ../lab2-CPU+PIM test-row_switch
python3 tools/compare_obi.py ../lab2-CPU+PIM/build/tests/row_switch.hex ../lab2-CPU+PIM/build/tests/row_switch.fst
```

Expect `PASS row_switch matched 18 DPIM transactions and response latencies`. To reproduce the comparison for all four images, run the full RTL suite and check each image against its waveform.

```sh
(
    set -e
    make -C ../lab2-CPU+PIM test
    rtl_tests=../lab2-CPU+PIM/build/tests
    for bench in row_switch vector_mul bit_masks memory_sweep; do
        python3 tools/compare_obi.py "$rtl_tests/$bench.hex" "$rtl_tests/$bench.fst"
    done
)
```

Across the four reference runs, all **8,488 DPIM transactions** matched in order, including the grant-to-response latency for each access. Consider what would happen if you wrote each partial weight store directly in `Access`. The firmware could read back the correct bytes, yet the waveform comparison would catch the first partial store completing one edge early.
