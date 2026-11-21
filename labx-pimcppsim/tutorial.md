# How to write a CPU–PIM performance simulator

Directly run RTL simulation might be too slow for large-scale integrations. Therefore, we need to develop a way to quickly simulate the behavior of CPU and PIM for performance and functions.

A CPU submits a matrix–vector multiply to a processing-in-memory (PIM) accelerator, does six cycles of independent work, and reads the answer. When can the CPU use it? In this simulator the multiply takes nine cycles, the result reaches shared memory at cycle 18, and the CPU finishes its load at cycle 19. Writing the simulator means accounting for each of those times as well as computing the right answer.

Build it in two stages. First implement the computing-in-memory (CIM) macro and give its operations a cycle cost. Then add a CPU that executes a trace, a direct memory access (DMA) engine that moves the operands and results, and one memory port shared by both. Run the commands below from the directory containing the Makefile. The C++17 reference implementations are [simulators/cim.cpp](./simulators/cim.cpp) and [simulators/cosim.cpp](./simulators/cosim.cpp).

## 1. Get one multiply right

Let the macro hold an `R × C` matrix of signed 8-bit weights in row-major order. One job consumes `R` signed input bytes and produces `C` signed 64-bit sums. Each **column** produces one output. The inner arithmetic of `CimArray::multiply` can be written as follows when `x` and `weights` already hold signed bytes.

```cpp
std::vector<std::int64_t> y(cols, 0);
for (std::size_t r = 0; r < rows; ++r) {
    for (std::size_t c = 0; c < cols; ++c) {
        y[c] += std::int64_t(x[r]) * weights[r * cols + c];
    }
}
```

Use a rectangular matrix with negative entries to catch indexing and signedness errors. The sample stores weights `1 -2 3 4 -5 6`, representing three rows of two columns. For input `[2, 0, -1]`, the first sum is `2×1 + 0×3 + (-1)×(-5) = 7`. The second is `2×(-2) + 0×4 + (-1)×6 = -10`. Keep the programmed weights in the macro so another input vector can use them.

## 2. Put a cycle cost on the macro

Give `TimingModel` a 64-row × 32-column tile and a 16-byte-per-cycle bus. A transfer of `n` bytes takes `beats(n) = (n + 15) / 16` cycles using integer division. Round each transfer separately. Programming starts a fresh transfer for each row, so even a two-byte row costs one cycle. Put these formulas in a shared header so that changes to the tile or bus apply to both simulators. [simulators/timing.hpp](./simulators/timing.hpp)

| Phase | Cycles in the standalone model | For the 3 × 2 example |
|---|---|---:|
| Program weights once | `R × beats(C)` | 3 |
| Transfer one input vector | `beats(R)` | 1 |
| Compute one vector | `ceil(R / 64) × ceil(C / 32) × 9` | 9 |
| Transfer one output vector | `beats(8 × C)` | 1 |

The nine cycles represent eight activation-bit steps and one merge per tile. Columns in a tile operate in parallel, and the standalone controller visits tiles sequentially. The C++ loop computes the values directly. The timing formula captures the modeled hardware parallelism.

From this directory, build and run the standalone checkpoint.

```sh
make build/cim
./build/cim
```

The built-in demo runs two vectors against the same weights. Expect `7 -10` and `-10 28`, each with 11 cycles of latency. The run takes `3 + 2 × 11 = 25` cycles. When you move to the CPU model, restrict each programmed matrix to one tile, as `cosim.cpp` does. Every legal job there has a nine-cycle compute phase.

## 3. Schedule a job and decide when its result is visible

Implement `SharedMemory` as 1 MiB of zero-initialized bytes. Validate each nonempty range before indexing it. A job reads `R` input bytes and writes `8 × C` output bytes. When multiplying bytes from memory, decode each value above 127 as `value - 256`. Encode each 64-bit result least-significant byte first so the CPU can read negative sums back correctly.

Represent the DMA engine's single active job with an optional `DmaJob`. Store its ID, its input and output ranges, its computed result, and four cycle timestamps. At launch, after the CPU has spent two cycles issuing the command, initialize the timestamps with this schedule.

```text
inputEnd   = launch + beats(R)
computeEnd = inputEnd + 9
writeStart = computeEnd
writeEnd   = writeStart + beats(8 × C)
```

`DmaEngine::launch` can read the input and calculate the result immediately in C++ because the DMA owns the port throughout input capture. Keep the result bytes in the job. Whenever simulated time advances to or past `writeEnd`, `DmaEngine::advance` copies them into shared memory and clears the job. This lets a single call such as `WORK 100` cross several phase boundaries while preserving the exact cycle of writeback.

The initial writeback time can move. Implement `DmaEngine::grantCpu` for a CPU load or store using these rules in order.

1. If the DMA is idle, grant the port immediately. If any requested byte overlaps the pending output, wait until `writeEnd`. This orders both reads and overwrites of that output.
2. For any other address, the earliest start is `max(now, inputEnd)`. The DMA input burst occupies the whole port, including for an access to an unrelated address.
3. If that start is at or after `computeEnd` and the job is active, give writeback priority and wait until `writeEnd`. Otherwise grant the CPU its whole burst. If the burst ends after the scheduled `writeStart`, move `writeStart` to the end of the burst and recompute `writeEnd`.

## 4. Drive the clock from CPU instructions

In `SystemSimulator`, keep a 64-bit cycle counter `now` and a count of blocked CPU cycles. Implement `advance(t)` to commit any due DMA writeback and set `now = t`. Implement `block(t)` to add `t - now` to the blocked count before calling `advance(t)`. For a CPU memory access, block until the port grant, advance by the transfer cost, then perform the read or write. This charges a port wait and a transfer to distinct parts of the CPU timeline.

Let `TraceCpu` strip text after `#`, split each remaining line into words, and pass each instruction to `SystemSimulator::execute`. Use decimal byte addresses. The example trace exercises every instruction. [examples/example.trace](./examples/example.trace)

```text
STORE 0 1 -2 3 4 -5 6    # Write six weight bytes, starting at address 0
STORE 64 2 0 -1          # Write three input bytes
PROGRAM 0 3 2            # Read the weights into a 3 × 2 macro
SUBMIT first 64 128      # Read input at 64 and send the result to 128
WORK 6                   # Advance six CPU cycles without using the port
WAIT first               # Block until this job's output is visible
LOAD 128 2               # Read two 64-bit sums, 16 bytes in total
```

`PROGRAM` waits for any active job, charges the programming cost as blocked time, and copies the weights. `SUBMIT` requires programmed weights and a fresh job ID. It waits for the previous job to finish, spends two issue cycles, and launches the DMA. In overlap mode, the CPU can execute the next instruction immediately. To implement `--blocking`, add a wait for writeback immediately after launch. An explicit `WAIT` on an already completed job costs zero cycles.

At end of trace, save `now` as `cpu_finished`, then advance through any pending writeback to obtain `total_cycles`. Use `advance` for this final drain so it adds no CPU stalls.

## 5. Check the timeline by hand

```sh
make build/cosim
./build/cosim examples/example.trace
./build/cosim examples/example.trace --blocking
```

In overlap mode, each interval below starts at its first cycle boundary and ends at its second. At boundary 18, writeback has finished and the CPU can begin its load.

| Cycles | CPU | DMA or macro |
|---|---|---|
| 0–2 | Two one-cycle stores | Idle |
| 2–5 | Blocked on `PROGRAM` | Program three rows |
| 5–7 | Issue `SUBMIT` | Job launches at 7 |
| 7–8 | First cycle of `WORK 6` | Capture three input bytes |
| 8–13 | Remaining five cycles of work | Compute |
| 13–17 | Blocked on `WAIT` | Finish computing |
| 17–18 | Still blocked | Write 16 result bytes |
| 18–19 | `LOAD` returns `7 -10` | Idle |

The CPU reports eight blocked cycles, three for programming and five for `WAIT`. In blocking mode it waits the full 11 cycles at `SUBMIT`, does its six cycles of work at 18–24, and finishes the load at 25. Both runs make the same result visible at 18. Overlap saves six cycles for this trace. Run `make run` to regenerate the standalone and CPU reports under `results/`. Compare the `load@` and `cpu_blocked` fields in [cosim-overlap.txt](./results/cosim-overlap.txt) and [cosim-blocking.txt](./results/cosim-blocking.txt).

## 6. Test the scheduler with small trace changes

Start each check with `cp examples/example.trace my.trace`, edit `my.trace`, and run `./build/cosim my.trace`.

| Change | Expected observation | What it checks |
|---|---|---|
| Remove `WAIT first` | The load still returns `7 -10` at 19. | The output range itself forces the load to wait. |
| End the trace immediately after `SUBMIT` | `cpu_finished=7 cpu_blocked=3 total_cycles=18` | DMA can outlive the CPU trace. |
| Replace `WORK 6` with `WORK 9` followed by `LOAD 256 4` on a new line | `compute_done=17 write_start=18 visible=19`. The final load finishes at 20. | A CPU burst can delay DMA writeback. |

In the last check, the CPU requests four 64-bit values at cycle 16. Those 32 bytes occupy the port for two cycles. When the macro finishes at 17, the CPU still has one cycle left on the port. If your simulator reports the original visibility time of 18, inspect where it updates the pending job after granting the CPU a burst.
