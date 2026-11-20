# Trace-Driven CPU–PIM Simulator

A self-contained C++17 simulator for end-to-end performance estimation of workloads
split across a CPU, a DMA engine, and an abstract compute-in-memory (CIM/PIM)
array. It reads an operation trace, resolves dependencies, schedules operations on
three resources, and writes cycle-level reports.

The project also retains a deterministic, bit-accurate CIM kernel library for
functional verification. The trace executable is a **performance simulator**: its
PIM records describe dimensions and operation types, not weight or activation
values.

## What is modeled

- CPU work expressed as fixed cycles or instructions executed at a configurable
  instructions-per-cycle (IPC) rate, plus optional stalls.
- One DMA engine shared by host-to-PIM (`h2p`) and PIM-to-host (`p2h`) transfers.
- PIM weight loads, bit-serial vector-matrix multiplication (VMM), row writes, and
  row-wide AND/OR/XOR operations.
- Explicit operation dependencies, forward references, synchronization points,
  and dependency-cycle detection.
- Overlap between independent CPU, DMA, and PIM work. Operations sharing a
  resource execute serially.
- Per-operation timing and utilization counters in JSON and CSV reports.

The model does not include a detailed CPU pipeline, caches, DRAM timing, analog
noise, ADC quantization, device variation, sparsity-dependent timing, energy, or
multiple concurrent PIM/DMA engines. Cycle counts are analytical estimates, not
measurements from hardware.

## Project layout

```text
.
├── Makefile
├── README.md
├── tutorial.md
├── examples/
│   └── demo.trace
├── simulators/
│   ├── main.cpp                 CLI and report writers
│   ├── trace_simulator.hpp      Trace format and system-model interface
│   ├── trace_simulator.cpp      Parser, DAG scheduler, and resource model
│   ├── cim_simulator.hpp        CIM kernel and shared timing interface
│   ├── cim_simulator.cpp        Bit-accurate kernels and timing formulas
│   ├── tests.cpp                Unit and randomized tests
│   └── pim-only/                Preserved legacy snapshot; not built
├── build/                       Generated objects and executables
└── results/                     Generated reports; legacy reports in results/legacy/
```

## Build, test, and run

Requirements: a C++17 compiler and `make`. There are no external library
dependencies.

```sh
make          # Build build/cpu_pim_sim and build/cpu_pim_tests
make test     # Run the regression suite
make run      # Simulate examples/demo.trace
```

The current regression suite reports:

```text
All tests passed (255 assertions).
```

Run the executable directly to override the trace, report prefix, or timing
parameters:

```sh
./build/cpu_pim_sim \
  --trace examples/demo.trace \
  --output-prefix results/custom \
  --cpu-ipc 4 \
  --dma-bytes-per-cycle 16 \
  --max-active-rows 2 \
  --adc-count 2 \
  --write-ports 4
```

Use `./build/cpu_pim_sim --help` for every option. Additional maintenance targets
are:

```sh
make clean           # Remove build/ only
make clean-results   # Remove generated .json and .csv files in results/
```

## Trace format

Each nonempty line contains one operation:

```text
OPERATION ID [key=value ...] [# comment]
```

- `ID` is unique within the trace.
- `deps=a,b` lists operation IDs that must finish first. Commas separate IDs;
  whitespace is not needed. Forward references are allowed.
- Blank lines and text after `#` are ignored.
- Numeric values accept decimal and `0x` hexadecimal notation.
- Unknown operations, unknown or duplicate fields, missing fields, duplicate IDs,
  missing dependencies, self-dependencies, and dependency cycles are errors.

### Operations

| Operation | Required fields | Optional fields | Resource |
|---|---|---|---|
| `cpu` | Exactly one of `cycles=N` or `instructions=N` | `stall_cycles=N`, `deps` | CPU |
| `sync` | None | `deps` | CPU, zero cycles |
| `h2p` | `bytes=N` | `deps` | DMA |
| `p2h` | `bytes=N` | `deps` | DMA |
| `pim_load` | `rows=R channels=C weight_bits=B` | `deps` | PIM |
| `pim_vmm` | `rows=R channels=C weight_bits=B activation_bits=A` | `deps` | PIM |
| `pim_write` | `columns=N` | `deps` | PIM |
| `pim_logic` | `columns=N function=and\|or\|xor` | `deps` | PIM |

Bit widths must be in `1..64`. Dimensions and transfer sizes must be positive.
A `cpu` operation may use zero cycles; an instruction-based operation calculates
`ceil(instructions / cpu_ipc)` and then adds `stall_cycles`.

### Scheduling rules

For operation `i`:

```text
dependency_ready[i] = max(end_cycle[dependency])
resource_ready[i]   = time at which its resource becomes free
start[i]            = max(dependency_ready[i], resource_ready[i])
end[i]              = start[i] + duration[i]
```

There are no implicit dependencies between different resources. Two records can
overlap when their dependencies permit it and they use different resources.
Records using the same resource are serialized in deterministic trace/topological
order. The scheduler reserves resources in that order and does not backfill an
earlier idle gap with a later-ready operation; it is an in-order resource policy,
not a minimum-makespan or earliest-start optimizer. `total_cycles` is the maximum
operation end time—the workload makespan—not the sum of all durations.

A zero-cycle `sync` on the CPU is useful when later CPU work must wait for PIM or
DMA completion. Dependencies still need to describe actual data/control
requirements; the scheduler does not infer memory dependencies.

### Example

`examples/demo.trace` contains:

```text
cpu prepare instructions=40
cpu background instructions=32
h2p input_transfer bytes=64 deps=prepare
pim_load weights rows=4 channels=3 weight_bits=4 deps=input_transfer
pim_vmm multiply rows=4 channels=3 weight_bits=4 activation_bits=4 deps=weights
p2h output_transfer bytes=24 deps=multiply
sync wait_for_output deps=output_transfer
cpu consume instructions=12 deps=wait_for_output
```

With the default configuration, `make run` produces:

| Operation | Resource | Interval | Cycles |
|---|---|---:|---:|
| `prepare` | CPU | `[0, 10)` | 10 |
| `background` | CPU | `[10, 18)` | 8 |
| `input_transfer` | DMA | `[10, 16)` | 6 |
| `weights` | PIM | `[16, 19)` | 3 |
| `multiply` | PIM | `[19, 41)` | 22 |
| `output_transfer` | DMA | `[41, 45)` | 4 |
| `wait_for_output` | CPU | `[45, 45)` | 0 |
| `consume` | CPU | `[45, 48)` | 3 |

The end-to-end total is 48 cycles. CPU, DMA, and PIM busy cycles are 21, 10, and
25 respectively. Their sum exceeds 48 because busy intervals on different
resources can overlap; per-resource busy time is not the workload makespan.

## Timing model

### CPU

For an instruction-based CPU record:

```text
cpu_cycles = ceil(instructions / cpu_ipc) + stall_cycles
```

For a fixed-cycle record:

```text
cpu_cycles = cycles + stall_cycles
```

Instruction-based division uses the exact binary representation of the configured
`double` IPC rather than converting the instruction count to `double`. This avoids
precision loss above 2^53 and detects results that exceed the 64-bit cycle range.

### DMA

Both directions share one engine and use:

```text
dma_cycles = dma_setup_cycles + ceil(bytes / dma_bytes_per_cycle)
```

### PIM VMM and weight loading

For `R` rows, `C` output channels, `B` weight bits, and `A` activation bits:

```text
P = C * B
G = ceil(R / max_active_rows)
Q = ceil(P / adc_count)

weight_load = write_setup_cycles + ceil(R * P / write_ports)
vmm         = vmm_setup_cycles + A * G * (Q * adc_cycles + accumulate_cycles)
adc_counted = A * G * P
```

One ideal ADC conversion resolves one weight bit plane for one row group and one
activation bit. Latency and conversion counts are data-independent. The final ADC
batch may be partially occupied; the conversion counter counts actual planes,
not full batch capacity.

### PIM logic

```text
row_write = write_setup_cycles + ceil(columns / write_ports)
AND/OR    = logic_setup_cycles + boolean_sense_cycles + logic_write_cycles
XOR       = logic_setup_cycles + xor_sense_cycles + logic_write_cycles
```

The logic command cost is independent of row width. That is an explicit
abstraction, not a circuit-level claim.

## Reports

The default run writes:

- `results/cpu_pim.json`: full configuration, summary counters, dependencies, and
  per-operation intervals;
- `results/cpu_pim.csv`: summary metrics as `metric,value` rows;
- `results/cpu_pim_trace.csv`: one row per scheduled operation, including
  dependency-ready and resource-ready times.

All intervals are half-open: `[start_cycle, end_cycle)`. Cycle and counter
addition/multiplication use checked 64-bit arithmetic where applicable.

## Functional CIM library

The trace scheduler and standalone CIM kernels share the same timing functions in
`cim_simulator.cpp`, preventing their PIM latency formulas from diverging. The
library additionally provides exact unsigned integer execution:

```cpp
#include "cim_simulator.hpp"

cim::Simulator simulator;
cim::VmmArray array(simulator, {2, 8, 4});
array.loadWeights({{3, 5}, {2, 7}});
const auto output = array.multiply({1, 2}, 4);  // {7, 19}
const auto cycles = simulator.statistics().totalCycles();
```

The array configuration is `{rows, physical_columns, weight_bits}`. The example
uses two rows, eight physical columns, four bits per weight, and two output
channels. Results are checked for 64-bit overflow. `LogicArray` supports safe
in-place AND/OR/XOR destinations.

## Tests

`simulators/tests.cpp` covers:

- known and randomized VMM results against a scalar reference;
- bit-plane mapping, zero/maximum values, invalid inputs, and overflow;
- logic results, in-place aliasing, and kernel timing;
- trace parsing and validation;
- CPU/DMA/PIM overlap and end-to-end makespan;
- forward dependencies, synchronization, and fractional IPC;
- exact instruction counts above 2^53 and 64-bit cycle boundaries;
- in-order reservation with different dependency-ready times;
- all PIM trace operations, dependency failures, and counter overflow.

See `tutorial.md` for a step-by-step explanation of how to implement and extend
this simulator.
