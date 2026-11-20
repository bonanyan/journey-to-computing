# Abstract Computing-in-Memory Simulator

A self-contained C++17, object-oriented simulator for deterministic functional
verification and cycle estimation of a bit-serial, weight-stationary CIM macro.
It uses no external libraries and performs exact unsigned integer arithmetic.

## Included models

- `cim::VmmArray`: unsigned matrix-vector multiplication with multi-bit weights
  and bit-serial activations.
- `cim::LogicArray`: row-parallel bitwise AND, OR, and XOR, including safe
  in-place destinations.
- `cim::Statistics`: per-operation cycle trace and total elapsed cycles.
- Ideal ADC popcount conversion, configurable row grouping, ADC batching, write
  bandwidth, setup, and accumulation timing.

The model does not simulate analog noise, ADC quantization, device variation,
sparsity, pipelining between commands, or energy. Latencies are defined by the
timing model below; they are not claimed to match a particular circuit.

## Build and run

```sh
make
make test
./cim_sim
```

The default demo calculates:

```text
W = [[1, 2, 3], [4, 5, 6], [7, 8, 9], [10, 11, 12]]
x = [1, 2, 3, 4]
W^T x = [70, 80, 90]
```

It also demonstrates Boolean operations. Reports are written directly into the
current directory:

- `results.json`: configuration, functional results, operation trace, and total
  cycles.
- `results.csv`: scalar VMM and Boolean outputs.
- `results_trace.csv`: per-operation start, end, and elapsed cycles.

Run `./cim_sim --help` for all command-line timing options.

## Custom inputs

Weights are a rectangular matrix with one row per activation and one column per
output channel. Values can be comma-separated or whitespace-separated; blank
lines and text after `#` are ignored. Decimal and `0x` hexadecimal values are
accepted. The activation file can place values on one line or multiple lines.

Example `weights.csv`:

```csv
1,2,3
4,5,6
7,8,9
10,11,12
```

Example `input.csv`:

```csv
1,2,3,4
```

Run:

```sh
./cim_sim --weights weights.csv --input input.csv \
  --weight-bits 4 --activation-bits 4 \
  --max-active-rows 2 --adc-count 2 --write-ports 4 \
  --output-prefix custom_results
```

The array row count and output-channel count are inferred from the matrix. The
physical column count is `output_channels * weight_bits`. Results are checked
for 64-bit overflow.

## Timing model

For `R` input rows, `C` output channels, `B` weight bits, and `A` activation
bits:

```text
G = ceil(R / max_active_rows)
P = C * B
Q = ceil(P / adc_count)

weight_load = write_setup_cycles + ceil(R * P / write_ports)
vmm = vmm_setup_cycles + A * G * (Q * adc_cycles + accumulate_cycles)
```

Each ADC conversion ideally resolves one output bit plane for one row group.
The simulator performs all conversions deterministically, even when the stored
data would produce zero. `adc_conversions` in the report is `A * G * P`.

Logic operations use:

```text
row_write = write_setup_cycles + ceil(columns / write_ports)
AND/OR = logic_setup_cycles + boolean_sense_cycles + logic_write_cycles
XOR = logic_setup_cycles + xor_sense_cycles + logic_write_cycles
```

Operations execute sequentially, so `total_cycles` is the sum of all recorded
weight loads, row writes, VMM operations, and logic operations.

## Library use

Include `cim_simulator.hpp` and link `cim_simulator.cpp`. A minimal example:

```cpp
cim::Simulator simulator;
cim::VmmArray array(simulator, {2, 8, 4});
array.loadWeights({{3, 5}, {2, 7}});
auto output = array.multiply({1, 2}, 4);
auto cycles = simulator.statistics().totalCycles();
```

The configuration `{2, 8, 4}` means two rows, eight physical bit-line columns,
and four weight bits per output channel. The example therefore supports two
output channels.
