# Lab 1 · Simulate a processing-in-memory macro

What can you get back from a memory array on one rising clock edge? Start with a single 32-bit read (MEM). Then use the same digital processing-in-memory macro (DPIM) to operate on 32 stored words in parallel. Finally, program a 4 × 8 weight matrix into an analog processing-in-memory macro (APIM) and watch eight dot products become six-bit output codes.

The supplied PISLIB has two RTL modules. MEM uses the scalar port on `DPIM`. APIM uses `Basic_GeMM_CIM`, an unsigned integer model of an ideal analog multiply–accumulate and analog-to-digital converter (ADC). Each trace shows the clock edges visible to a macro user. The 10 ns testbench clock is a stimulus setting. [Digital RTL](pislib_verilog-main/src/DPIM.v) · [Analog RTL](pislib_verilog-main/src/APIM.v)

## Build and run

Use Verilator 5 with timing support, a C++20 compiler with coroutines, GNU Make, and GTKWave. On Ubuntu 24.04, for example, install them with `sudo apt install build-essential verilator gtkwave`. From the directory containing this tutorial, run the following commands.

```sh
verilator --version
make
```

Look for three lines starting with **PASS MEM**, **PASS DPIM**, and **PASS APIM**. A mismatch stops the simulation and reports the expected and observed values. Verilator writes one VCD (Value Change Dump) file per experiment. The simulations can run on a machine without a display.

| Experiment | Run again | Open the trace with signals selected | Trace |
|---|---|---|---|
| MEM | `make mem` | `make wave-mem` | `build/mem.vcd` |
| DPIM | `make dpim` | `make wave-dpim` | `build/dpim.vcd` |
| APIM | `make apim` | `make wave-apim` | `build/apim.vcd` |

In GTKWave, use **Zoom Fit** to see the whole run. Click just after a rising edge to place the time marker and read the signal values. The benches drive controls on a falling edge, the macro acts on the next rising edge, and the bench checks the result 1 ns later. A gap of five nanoseconds between an input change and an output change is visible in these traces. Both benches program every word they use. [Makefile](Makefile) · [MEM and DPIM stimulus](tb/tb_dpim.sv) · [APIM stimulus](tb/tb_apim.sv)

## 1. MEM · Read one stored word

Run `make wave-mem`. The digital macro contains 4,096 words of 32 bits. With `en=1`, a rising edge writes `d` to `mem[addr]` when `we=1`. A scalar read uses `we=0, cme=1` and copies the selected word to `q`. A low `en` disables both operations.

Find the first two writes. Addresses `0x012` and `0x092` differ in just bit 7. Predict what will appear on `q` when the bench comes back to each address.

| Rising edge | Transaction | What to look for |
|---:|---|---|
| 15 ns | Write `1234abcd` to `012` | `en=1`, `we=1`, write data on `d` |
| 25 ns | Write `cafe0055` to `092` | A different word is selected |
| 35 ns | Read `012` | `q=1234abcd` |
| 45 ns | Attempt to write `ffffffff` to `092` with `en=0` | `q` holds `1234abcd` |
| 55 ns | Read `092` | `q=cafe0055`, confirming the disabled write had no effect |
| 65 ns | Overwrite `092` with `deadbeef` | `d=deadbeef` while `q=cafe0055` |
| 75 ns and 85 ns | Read `092`, then `012` | `q=deadbeef`, then `1234abcd` |

**Check your trace.** Put the cursor at 66 ns and again at 76 ns. Which edge first makes the overwritten value visible on the read port? Keep addresses `012` and `092` in mind for the digital compute experiment.

## 2. DPIM · Operate on 32 words at once

Run `make wave-dpim`. On a scalar access, all 12 address bits select one word. On a compute edge (`en=1, we=0, cme=0`), each lane supplies its own upper five address bits. Follow the `if (cme)` branch in the RTL to see the mode polarity. The 32 lanes share the low seven address bits and the function selector.

```text
word_address(lane) = 128 × lane + addr[6:0]
cmOut[lane]        = operation(cmIn[lane], mem[word_address(lane)])
```

The bench writes `0x80000010 + lane` at offset `0x12` in each lane, then drives `cmIn[lane] = 0x13 + lane`. At 335 ns a scalar read of `0x092` returns lane 1's stored word, `0x80000011`. At 345 ns the address bus reads `0xf92`. Its low seven bits still select offset `0x12`, so lane 0 uses `0x012`, lane 1 uses `0x092`, and lane 31 uses `0xf92` on the same edge. Read the results on `cmOut_00` and `cmOut_01`. [Digital address map](pislib_verilog-main/doc/DPIM_AddressMap.pdf)

| Rising edge | `func` | Operation | Lane 0 (hex) | Lane 1 (hex) |
|---:|---|---|---|---|
| 345 ns | `00` | Multiply, low 32 bits | `80000130` | `00000154` |
| 355 ns | `01` | Bitwise AND | `00000010` | `00000010` |
| 365 ns | `10` | Bitwise OR | `80000013` | `80000015` |
| 375 ns | `11` | Bitwise XOR | `80000003` | `80000005` |

**Check your trace.** Predict lane 31's XOR result from its weight and input. Expand `tb_dpim → dut` in the signal tree, add `cmOut_31`, and check at 376 ns. Compare it with `q`, which still holds the scalar read from 335 ns. The testbench checks all 128 lane results across the four compute edges.

## 3. APIM · Quantize eight dot products

Run `make wave-apim`. This macro stores 1,024 unsigned bytes. With `cs=1`, set `web=0` to write one byte. Set `web=1, cimeb=1` to read one byte on `q`, or `web=1, cimeb=0` to compute. The write-enable polarity is the opposite of the digital macro's `we`.

A ten-bit programming address packs four fields. During compute the address bus supplies the **group** and **slot**. The macro visits all four input rows and all eight output columns in that group and slot.

| Address bits | Field | How the experiment uses it |
|---|---|---|
| `[9:8]` | Input row | One weight for each of `cim_in0` through `cim_in3` |
| `[7:5]` | Group | `2` |
| `[4:2]` | Output column | One four-weight dot product for each of eight outputs |
| `[1:0]` | Slot | `1` |

The first 32 writes set `weight[row][col] = 16 × (row + 1) + 24 × col`. For column 0, the bytes at `0x041`, `0x141`, `0x241`, and `0x341` hold 16, 32, 48, and 64. At 335 ns, a scalar read from `0x35d` returns `0xe8` (row 3, column 7). Set a second marker just after 345 ns, when the macro accepts the input vector `[1, 2, 3, 4]`. [Analog address map](pislib_verilog-main/doc/PIMAddressMap.pdf)

```text
column 0 sum  = 1×16 + 2×32 + 3×48 + 4×64 = 480
column 0 code = floor(480 / 256) = 1
column j sum  = 480 + 240×j
output codes  = [1, 2, 3, 4, 5, 6, 7, 8]
```

The six-bit ADC output takes bits `[13:8]` of the unsigned sum, dropping the low eight bits. The supplied APIM source originally declared its intermediate sums six bits wide, which put the output slice outside the register. The lab widens the eight `cim_out*_tmp` registers to 14 bits, matching the reference calculation in the supplied APIM testbench. The GTKWave view shows the full column 0 sum and all eight ADC codes in decimal. [APIM RTL](pislib_verilog-main/src/APIM.v) · [Original reference calculation](pislib_verilog-main/src/APIM_tb.v)

| Rising edge | Column 0 operation | `cim_out0_tmp` | `cim_out0` |
|---:|---|---:|---:|
| 345 ns | Dot product with the first vector | 480 | 1 |
| 375 ns | After programming two weights to 255 and 1, apply `[1, 0, 0, 0]` | 255 | 0 |
| 385 ns | Keep the weights and apply `[1, 1, 0, 0]` | 256 | 1 |
| 435 ns | Set all four weights to 255 and all four inputs to 15 | 15,300 | 59 |

**Check your trace.** At 375 and 385 ns, the raw sums differ by one. Which bit changes the ADC code? Use the largest possible sum at 435 ns to explain why codes 60 through 63 are unreachable for these operand widths.

## Try a new vector

In `tb/tb_apim.sv`, insert `compute(0, 0, 0, 4);` immediately after `compute(1, 2, 3, 4);`. This selects the fourth stored row and scales each weight by four. Before running it, calculate the first and last output codes from that row's weights, 64 and 232. Then run `make wave-apim` and read the new result at 355 ns. The bench computes its expected values from the programmed matrix, so it checks the new vector automatically. Later edges move forward by 10 ns. Remove the extra call to return to the timing table above.

For your lab notes, record one scalar transaction with its address and read data, one DPIM lane with both operands and its result, and one APIM column with its full sum and ADC code. For each, mark the rising edge where the result becomes visible.

## Takeaway messages
Now you know how a PIM (actually work).


