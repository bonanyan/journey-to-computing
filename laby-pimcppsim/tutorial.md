# Tutorial: Writing the Trace-Driven CPU–PIM Simulator

This tutorial explains how to build the simulator in this repository from its
major components. The goal is not to simulate every transistor or CPU instruction.
Instead, we will build a useful **operation-level performance model** with:

- a text trace as the workload input;
- explicit data/control dependencies;
- CPU, DMA, and PIM resources that can overlap;
- analytical operation timing;
- reusable, bit-accurate CIM kernels for functional verification;
- deterministic JSON and CSV reports;
- unit and randomized tests.

The most important design decision is to separate **what an operation computes**
from **when its resources are occupied**. The trace simulator handles the latter.
The standalone CIM library handles exact arithmetic and verifies the kernel timing
formulas used by the trace simulator.

## 1. Define the abstraction and its limits

Start with three resources:

| Resource | Work assigned to it |
|---|---|
| CPU | Fixed-cycle work, instruction-based work, synchronization |
| DMA | Host-to-PIM and PIM-to-host byte transfers |
| PIM | Weight loads, VMMs, row writes, Boolean logic |

Each trace record has a unique ID and optional dependencies. For example:

```text
cpu prepare instructions=40
h2p input bytes=64 deps=prepare
pim_vmm multiply rows=4 channels=3 weight_bits=4 activation_bits=4 deps=input
p2h output bytes=24 deps=multiply
sync wait deps=output
cpu consume instructions=12 deps=wait
```

This says that data transfer waits for preparation, VMM waits for its input, the
readback waits for VMM, and CPU result consumption waits for readback. It does not
say that every CPU operation must wait for every PIM operation. That distinction is
what permits overlap.

Write down the exclusions before implementing anything. This model has no cache
hierarchy, out-of-order CPU pipeline, DRAM controller, analog noise, energy model,
or automatically inferred memory dependencies. Those can be added later, but they
should not be hidden inside unexplained constants.

## 2. Organize the project

Use the following layout:

```text
Makefile
examples/
  demo.trace
simulators/
  main.cpp
  trace_simulator.cpp
  trace_simulator.hpp
  cim_simulator.cpp
  cim_simulator.hpp
  tests.cpp
build/
results/
```

`build/` and `results/` contain generated files. Keep source files in
`simulators/`, workload inputs in `examples/`, and reports in `results/`.

The dependency graph between source components is intentionally small:

```text
cim_simulator.hpp/.cpp
        ↑
trace_simulator.hpp/.cpp
        ↑
      main.cpp

tests.cpp → both simulator libraries
```

`cim_simulator` knows nothing about traces. `trace_simulator` reuses its PIM timing
functions. `main.cpp` performs only CLI parsing, execution, and reporting.

## 3. Write the Makefile first

A root Makefile provides one stable interface even though sources and outputs are
in subdirectories:

```makefile
CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic

BUILD_DIR = build
RESULTS_DIR = results
SIMULATOR = $(BUILD_DIR)/cpu_pim_sim
TESTS = $(BUILD_DIR)/cpu_pim_tests
COMMON_OBJECTS = $(BUILD_DIR)/cim_simulator.o $(BUILD_DIR)/trace_simulator.o

all: $(SIMULATOR) $(TESTS)

$(SIMULATOR): $(COMMON_OBJECTS) $(BUILD_DIR)/main.o
	$(CXX) $(CXXFLAGS) -o $@ $^

$(TESTS): $(COMMON_OBJECTS) $(BUILD_DIR)/tests.o
	$(CXX) $(CXXFLAGS) -o $@ $^

$(BUILD_DIR)/%.o: simulators/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR):
	mkdir -p $@

$(RESULTS_DIR):
	mkdir -p $@

run: $(SIMULATOR) | $(RESULTS_DIR)
	./$(SIMULATOR)

test: $(TESTS)
	./$(TESTS)

clean:
	rm -rf $(BUILD_DIR)
```

The repository's actual Makefile also lists header dependencies explicitly and
provides `clean-results`. Explicit header dependencies ensure that editing an
interface rebuilds every affected object.

The default executable arguments are:

```text
--trace examples/demo.trace
--output-prefix results/cpu_pim
```

Therefore, `make run` needs no additional environment variables or paths.

## 4. Build the exact CIM kernel layer

Although the system executable is performance-only, a scalar formula alone cannot
check that a proposed CIM mapping actually implements a VMM. Keep a small exact
kernel model in `cim_simulator.hpp/.cpp`.

### 4.1 Types and array state

Use fixed-width types:

```cpp
using Bit = std::uint8_t;
using Word = std::uint64_t;
using Matrix = std::vector<std::vector<Word>>;
using BitRow = std::vector<Bit>;
```

`TimingConfig` contains write ports, active-row limits, ADC count, and all setup,
sense, conversion, and accumulation costs. `ArrayConfig` contains rows, physical
columns, and weight bits.

`VmmArray` stores a row-major `std::vector<Bit>`, a reference to a `Simulator`, the
number of output channels, and a flag showing whether weights are loaded.
`LogicArray` similarly stores bits and shares the simulator timeline. The
`Simulator` must outlive arrays that hold references to it.

### 4.2 Map weights into bit planes

For a matrix with `R` rows and `C` output channels, each `B`-bit weight occupies
`B` adjacent physical columns:

```cpp
const std::size_t column = output * weight_bits + bit;
new_cells[row * physical_columns + column] =
    static_cast<Bit>((weight >> bit) & Word{1});
```

Bit zero is stored first. Weight 10 in a four-bit field becomes `0,1,0,1`, which
represents `0×1 + 1×2 + 0×4 + 1×8`.

Validate matrix shape, value widths, and physical capacity before committing the
new cell image. Building a temporary vector and swapping it into the array avoids
partially replacing valid old weights after a validation error.

### 4.3 Implement bit-serial multiplication

For output channel `o`, the mathematical result is

\[
y_o = \sum_r w_{r,o}x_r.
\]

Expand the unsigned activation and weight bits:

\[
x_r = \sum_a x_{r,a}2^a,\qquad
w_{r,o} = \sum_b w_{r,o,b}2^b.
\]

Then

\[
y_o = \sum_a\sum_b2^{a+b}
      \left(\sum_r x_{r,a}w_{r,o,b}\right).
\]

The inner sum is an ideal integer popcount. Implement it with nested loops over:

1. activation bit;
2. row group;
3. output channel;
4. weight bit;
5. rows within the group.

For each bit plane, calculate:

```cpp
Word active_cells = 0;
for (std::size_t row = row_begin; row < row_end; ++row) {
    const Word input_enabled = (activations[row] >> input_bit) & Word{1};
    active_cells += input_enabled * cells_[row * columns + column];
}
addShiftedContribution(outputs[output], active_cells,
                       input_bit + weight_bit);
```

This is the functional counterpart of the timing model. It is also why the timing
model counts `A × G × C × B` ideal ADC conversions.

### 4.4 Reject overflow instead of wrapping

C++ unsigned arithmetic wraps by default, which would silently invalidate a
performance or functional result. Add checked helpers for addition,
multiplication, and allocation dimensions.

For a shifted VMM contribution, check all three operations:

```cpp
if (count == 0) return;
if (shift >= 64 || count > (UINT64_MAX >> shift)) {
    throw std::overflow_error("VMM result exceeds 64 bits");
}
const Word contribution = count << shift;
if (contribution > UINT64_MAX - output) {
    throw std::overflow_error("VMM result exceeds 64 bits");
}
output += contribution;
```

Special-case 64-bit values in `fitsInBits`; shifting a 64-bit integer by 64 is
undefined behavior. Use quotient-plus-remainder for ceiling division rather than
`value + divisor - 1`, which can overflow.

## 5. Share the PIM timing functions

Do not implement one VMM formula in `cim_simulator.cpp` and another in
`trace_simulator.cpp`. Eventually they will diverge. Expose small timing structs
and functions from the CIM header:

```cpp
struct WeightLoadTiming {
    Word cycles;
    Word cells_written;
};

struct VmmTiming {
    Word cycles;
    Word adc_conversions;
    Word row_groups;
    Word input_bits;
};

WeightLoadTiming estimateWeightLoadTiming(...);
VmmTiming estimateVmmTiming(...);
Word estimateRowWriteCycles(...);
Word estimateLogicCycles(...);
```

Both the exact array methods and trace scheduler call these functions.

For `R` rows, `C` channels, `B` weight bits, and `A` activation bits, define:

\[
P=CB,\qquad
G=\left\lceil\frac{R}{R_{active}}\right\rceil,\qquad
Q=\left\lceil\frac{P}{N_{ADC}}\right\rceil.
\]

The weight-load cost is

\[
T_{load}=S_{write}+\left\lceil\frac{RP}{W_{ports}}\right\rceil.
\]

The VMM cost is

\[
T_{VMM}=S_{VMM}+AG\left(QT_{ADC}+T_{accumulate}\right).
\]

Charge accumulation once per activation-bit/row-group pair, after all ADC batches
in that pair. Record:

\[
N_{ADC}=AGP.
\]

The conversion count uses `P`, not `Q × adc_count`, because the last batch may be
partially filled. Timing is data-independent, including for zero weights or zero
activations.

For Boolean arrays, use:

\[
T_{row\_write}=S_{write}+
\left\lceil\frac{columns}{W_{ports}}\right\rceil,
\]

\[
T_{AND/OR}=S_{logic}+T_{boolean\_sense}+T_{logic\_write},
\]

\[
T_{XOR}=S_{logic}+T_{xor\_sense}+T_{logic\_write}.
\]

The standalone `LogicArray::apply` computes a temporary result before writing its
destination, so an in-place destination cannot corrupt an input row.

## 6. Define a strict trace data model

In `trace_simulator.hpp`, define an operation kind and resource enum:

```cpp
enum class OperationKind {
    Cpu, Sync, H2P, P2H, PimLoad, PimVmm, PimWrite, PimLogic
};

enum class Resource { Cpu, Dma, Pim };
```

A generic `TraceOperation` is practical for this small instruction set. It stores:

- `id`, `kind`, and source line;
- a vector of dependency IDs;
- CPU `cycles`, `instructions`, and `stall_cycles`;
- DMA `bytes`;
- PIM dimensions and logic function.

Use presence flags for alternative CPU duration modes:

```cpp
bool cycles_set = false;
Word cycles = 0;
bool instructions_set = false;
Word instructions = 0;
```

A Boolean flag is necessary because `cycles=0` is a valid explicit value. Without
`cycles_set`, zero would be indistinguishable from an omitted field.

Define `SystemConfig` with:

```cpp
double cpu_ipc = 4.0;
std::size_t dma_bytes_per_cycle = 16;
Word dma_setup_cycles = 2;
cim::TimingConfig pim;
```

The output event should contain more than start and end time. Keep the dependency
ready time, resource ready time, source line, dependencies, instructions, bytes,
cell writes, ADC conversions, row groups, and input bits. These fields make a
schedule explainable and testable.

## 7. Write the trace parser

A strict parser catches workload errors before scheduling. Accept this grammar:

```text
OPERATION ID [key=value ...] [# comment]
```

Implement `parseTrace(std::istream&, source_name)` separately from `readTrace(path)`
so tests can parse an in-memory string without creating temporary files.

For each line:

1. Remove text after `#`.
2. Tokenize on whitespace with `std::istringstream`.
3. Skip blank lines.
4. Read the operation and unique ID.
5. Split every remaining token into exactly one `key=value` pair.
6. Reject unknown or duplicate fields.
7. Parse integers with full-token consumption.
8. Validate required fields and operation-specific combinations.
9. Save the source line for later diagnostics.

The operation fields are:

```text
cpu       cycles=N | instructions=N [stall_cycles=N] [deps=a,b]
sync      [deps=a,b]
h2p       bytes=N [deps=a,b]
p2h       bytes=N [deps=a,b]
pim_load  rows=R channels=C weight_bits=B [deps=a,b]
pim_vmm   rows=R channels=C weight_bits=B activation_bits=A [deps=a,b]
pim_write columns=N [deps=a,b]
pim_logic columns=N function=and|or|xor [deps=a,b]
```

Require **exactly one** of `cycles` and `instructions` for CPU work. Reject zero DMA
sizes, zero dimensions, bit widths outside `1..64`, duplicate dependency IDs, and
unknown logic functions.

Do not reject forward references during parsing. Dependency existence and graph
acyclicity are scheduler validations, not token-parsing validations.

Every error should include `source:line: message`. This is much more useful than a
generic “invalid trace” after reading a large workload.

## 8. Build and validate the dependency graph

After parsing, create an ID-to-index map. Reject empty or duplicate IDs again in
the scheduler because library users can construct `TraceOperation` objects
directly without using the parser.

For each dependency:

- require that its ID exists;
- reject self-dependencies;
- reject duplicate edges;
- increment the dependent operation's indegree;
- append the dependent to the prerequisite's adjacency list.

Use Kahn's topological algorithm:

```cpp
std::set<std::size_t> ready;
for (std::size_t i = 0; i < operations.size(); ++i) {
    if (indegree[i] == 0) ready.insert(i);
}

while (!ready.empty()) {
    const std::size_t index = *ready.begin();
    ready.erase(ready.begin());
    schedule(operations[index]);

    for (const auto dependent : dependents[index]) {
        if (--indegree[dependent] == 0) ready.insert(dependent);
    }
}

if (scheduled_count != operations.size()) {
    throw std::invalid_argument("trace dependency graph contains a cycle");
}
```

A `std::set` of indices chooses the earliest trace record among ready operations,
which makes scheduling deterministic. Forward dependencies work because an
operation becomes ready only after all its prerequisites have been scheduled.

This also defines the resource policy: ready operations reserve their resource in
trace/topological order. The scheduler does not insert a later-ready operation
into an earlier idle gap left by a lower-index operation. It is therefore an
in-order resource model, not an earliest-start or minimum-makespan optimizer. If
that policy is not desired, select ready operations by earliest feasible start or
maintain resource-available gaps instead of one availability cursor.

## 9. Schedule resources and dependencies separately

Maintain:

```cpp
Word resource_available[3] = {0, 0, 0};
std::vector<Word> end_cycles(operations.size(), 0);
```

For each ready operation, calculate two independent constraints:

```cpp
dependency_ready = max(end_cycles[dep] for dep in dependencies);
resource_ready   = resource_available[resource];
start            = max(dependency_ready, resource_ready);
end              = checkedAdd(start, duration);
```

Then update:

```cpp
resource_available[resource] = end;
end_cycles[index] = end;
total_cycles = max(total_cycles, end);
```

This small calculation is the core of the system simulator.

### Why total time is not a sum

If the CPU runs from cycle 10 to 18 while DMA runs from 10 to 16, their busy times
add to 14 cycles, but the wall-clock interval is only eight cycles. Therefore:

- `total_cycles` is the maximum event end time;
- `cpu_busy_cycles`, `dma_busy_cycles`, and `pim_busy_cycles` are sums of durations
  on their respective resources;
- each resource is serial, but different resources can overlap.

### Why `sync` uses the CPU resource

`sync` has zero duration and depends on the operations being awaited. It advances
CPU availability to the synchronization point without adding busy cycles. A later
CPU operation with `deps=sync_id` then starts only after the awaited work. A CPU
operation with no such dependency remains independent and may overlap PIM work.

The scheduler does not infer dependencies from variable names, matrix dimensions,
or trace order alone. Trace authors must state them explicitly.

## 10. Add CPU and DMA timing

For a fixed-cycle CPU operation:

```text
duration = cycles + stall_cycles
```

For instruction-based work:

```text
duration = ceil(instructions / cpu_ipc) + stall_cycles
```

Validate that IPC is finite and positive, then use checked addition for stalls.
Do not convert a large instruction count to `double`: integers above 2^53 lose
precision, and converting an out-of-range floating-point result back to an integer
can be undefined.

Instead, decompose the positive finite `double` IPC with `std::frexp` into an
integer significand and a power of two. Compute `ceil(instructions / ipc)` using
integer quotient/remainder arithmetic. Scale the quotient with a checked shift
and scale the remainder with binary long division, checking every accumulation.
This preserves exact instruction counts above 2^53, permits an exact
`UINT64_MAX` result, and rejects larger results. The model remains an ideal
fixed-IPC abstraction, not a pipeline simulation.

For either DMA direction:

```text
duration = dma_setup_cycles + ceil(bytes / dma_bytes_per_cycle)
```

Both `h2p` and `p2h` use the same DMA availability slot, so they cannot overlap
each other in this version. They can overlap CPU or PIM operations when their
dependencies allow it.

Accumulate instruction counts, transferred bytes, and ADC conversions with checked
arithmetic. Validate nonzero DMA bandwidth and PIM resource counts when
constructing `TraceSimulator`.

## 11. Connect the CLI and report writers

`main.cpp` should not contain scheduling logic. Its control flow is only:

```cpp
const Options options = parseOptions(argc, argv);
const auto operations = cpupim::readTrace(options.trace_path);
const cpupim::TraceSimulator simulator(options.config);
const auto result = simulator.run(operations);
writeJsonReport(...);
writeSummaryCsv(...);
writeTraceCsv(...);
```

Expose CLI overrides for CPU IPC, DMA bandwidth/setup, and all PIM timing fields.
Keep the workload-specific dimensions in the trace rather than turning them into
global command-line options.

Write three outputs:

1. **JSON:** complete configuration, summary, and operation records;
2. **summary CSV:** `metric,value` pairs;
3. **trace CSV:** operation intervals, dependencies, readiness times, and counters.

Escape IDs and dependency strings in JSON and CSV rather than assuming they can
never contain quotes or separators. Check output streams when opening and after
writing JSON. Use half-open intervals `[start,end)` consistently.

## 12. Work through the example schedule

The default configuration uses CPU IPC 4, DMA setup 2, DMA bandwidth 16 bytes per
cycle, 32 PIM write ports, 64 maximum active rows, and eight ADCs.

For `examples/demo.trace`:

1. `prepare`: `ceil(40/4) = 10` CPU cycles, `[0,10)`.
2. `background`: `ceil(32/4) = 8` CPU cycles, `[10,18)`.
3. `input_transfer`: `2 + ceil(64/16) = 6` DMA cycles. It depends only on
   `prepare`, so it runs at `[10,16)`, overlapping `background`.
4. `weights`: four rows, three channels, and four-bit weights write 48 cells.
   `1 + ceil(48/32) = 3` PIM cycles, `[16,19)`.
5. `multiply`: `P=12`, `G=1`, and `Q=ceil(12/8)=2`, so
   `2 + 4×1×(2×2+1) = 22` PIM cycles, `[19,41)`.
6. `output_transfer`: `2 + ceil(24/16) = 4` DMA cycles, `[41,45)`.
7. `wait_for_output`: zero cycles at cycle 45.
8. `consume`: `ceil(12/4) = 3` CPU cycles, `[45,48)`.

The final makespan is 48 cycles. The summary also reports:

```text
CPU busy cycles:      21
DMA busy cycles:      10
PIM busy cycles:      25
CPU instructions:     84
Host-to-PIM bytes:    64
PIM-to-host bytes:    24
PIM ADC conversions:  48
```

Hand calculations like this are essential: they give the tests an independent
expected schedule instead of merely checking that the program produces output.

## 13. Test at three levels

A lightweight test harness is sufficient. Use `require(condition, message)` and
`requireThrows<Exception>(function, message)` and count assertions.

### 13.1 Kernel tests

Test the exact CIM implementation against known values and a simple scalar oracle:

```cpp
expected[output] += weights[row][output] * activations[row];
```

Cover:

- the known `W^T x = [70,80,90]` example;
- bit-plane mapping;
- zero and maximum values;
- 100 deterministic randomized matrices and activation vectors;
- invalid dimensions, widths, and missing weight loads;
- 64-bit result overflow;
- AND/OR/XOR and in-place destinations;
- independent timing calculations.

### 13.2 Trace and scheduler tests

Build a small trace with known intervals and assert the entire summary. The test
in `simulators/tests.cpp` uses modified PIM resources to produce:

```text
prepare    [0,10)
overlap    [10,18)
input      [10,15)
weights    [15,28)
vmm        [28,134)
result     [134,138)
wait       [138,138)
consume    [138,140)
```

Check the 140-cycle makespan, per-resource busy times, instruction/byte/ADC counts,
and dependency/resource-ready fields. Also test:

- independent CPU, DMA, and PIM operations starting at cycle zero;
- all PIM operation kinds;
- forward dependency references;
- fractional IPC, exact instruction counts above 2^53, and 64-bit boundaries;
- in-order reservation when same-resource operations become ready at different times;
- synchronization;
- empty and malformed traces;
- missing, duplicate, self, and cyclic dependencies;
- invalid resource configuration;
- cycle-counter overflow.

### 13.3 Executable and report tests

Finally, run the real interface:

```sh
make test
make run
```

The current suite reports `All tests passed (255 assertions).` Validate that
`results/cpu_pim.json` parses as JSON and that both CSV files have the expected
number of records. This catches report-only errors that library tests cannot see.

## 14. Extend the simulator deliberately

Useful extensions follow naturally from the architecture:

- **More resources:** replace the three-slot availability array with named
  resources and per-resource configuration, such as several PIM arrays or DMA
  channels.
- **Richer CPU model:** add instruction classes, cache events, or a timing backend
  while retaining the trace/dependency interface.
- **Data placement:** introduce named buffers and capacity checks; infer only the
  dependencies whose semantics are explicitly defined.
- **Pipelined PIM kernels:** replace a single PIM availability time with separate
  write, sensing, ADC, and accumulation resource timelines.
- **Energy:** accumulate operation-specific energy counters. Do not use cycles as a
  universal energy proxy.
- **CPU-only comparison traces:** represent the same workload without PIM/DMA
  records and compare makespans, ensuring both traces use consistent assumptions.
- **Functional trace execution:** add named tensors and actual values if the trace
  must produce outputs, while keeping scheduling and numerical state separate.

For each extension, preserve the workflow used throughout this tutorial:

1. define the abstraction and its exclusions;
2. implement the functional semantics;
3. derive resource timing explicitly;
4. report enough state to explain the result;
5. verify with independent hand calculations and reference models.
