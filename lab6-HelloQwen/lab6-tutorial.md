<p align="center">
  <img src="../assets/journey-computing-jc_v2.png" alt="Journey to Computing logo" width="144" height="86" />
</p>

# Lab 6 · Run one Qwen3-4B decoder layer with Verilator

<p align="center">
  <strong>Author:</strong> Dr. Bonan Yan (<a href="mailto:bonanyan@pku.edu.cn">bonanyan@pku.edu.cn</a>) · November 22, 2026
</p>

## What to do

Take two token vectors through **layer 0 of Qwen3-4B**, one token at a time. The first token leaves behind a key and a value. The second token reads both positions and mixes their values according to its attention scores. The output at each step is a 2,560-element vector suitable as input to layer 1. A tokenizer, the other 35 layers, the final normalization and the vocabulary head lie outside this one-layer experiment.

## How to run

In a terminal in `lab6-HelloQwen`, use the Python environment from Lab 5. The two labs are siblings. The Makefile uses the operator source from Lab 5 and builds the Verilator executables against Lab 6's own copy of the design. [Lab 5 setup](../lab5-HelloPIMOperator/lab5-tutorial.md#how-to-run) · [Lab 6 Makefile](Makefile)

### 1. Get the small layer running

If your Python environment is already active, start at the `make` command.

```sh
. ../lab5-HelloPIMOperator/.venv/bin/activate
make smoke
```

Read the two position lines. You should see `output=32 cache=1 projections=7 MPU_tiles=7`, then `output=32 cache=2 projections=7 MPU_tiles=7`. This run keeps the full operator sequence and shrinks the tensors. It includes grouped-query attention, a nonzero rotary position and cache reuse. For an additional cache append, run `make smoke TOKENS=3`.

Run `make layer` for the full-width test with synthetic weights. To use published weights, export layer 0 from a local snapshot of the base `Qwen/Qwen3-4B` checkpoint with its `config.json`, `model.safetensors.index.json` and the shards identified by the index. [Checkpoint files](https://huggingface.co/Qwen/Qwen3-4B/tree/main)

```sh
python export_layer.py /path/to/Qwen3-4B build/weights-layer0 --layer 0
make layer WEIGHTS=build/weights-layer0
```

To measure cycles for one token and for two consecutive tokens, run the following commands.

```sh
make perf TOKENS=1
make perf TOKENS=2
```

## Inside a Qwen3-4B decoder layer

Each of the model’s 36 decoder layers makes two updates to a token’s hidden state. **Grouped-query self-attention** mixes information from the current and earlier tokens. The **feed-forward network** then transforms the features of the current token. Each half starts with root mean square normalization (RMSNorm) and adds its saved input back at the end. Both residual additions operate on 2,560-element vectors, so the layer output has the width the next layer expects.

![Operator graph for one token. Input RMSNorm feeds the query, key and value projections. The query and key heads each pass through RMSNorm and rotary position embedding. Scaled query–key scores and a causal softmax weight the visible values. Head concatenation, output projection and residual addition complete attention. A second RMSNorm feeds the gate and up projections. SiLU on the gate, elementwise multiplication, down projection and a second residual addition produce the layer output.](./figs/qwen3-4b-1-layer.svg)

**Reading the attention branches.** At position `p`, each query compares against keys at positions `0` through `p`. Rotary position embedding (RoPE) rotates each query and key according to its own token position. Four query heads share one key/value head, but each query head computes its own softmax over positions. Earlier keys and values come from the cache. The current key participates in scoring, and the current value participates in the weighted sum. Once the layer output is ready, the lab appends this key/value pair to the cache for the next token.

**Reading the feed-forward branches.** The gate and up projections read the same normalized vector. At each of their 9,728 coordinates, SiLU computes `gate × sigmoid(gate)`, and an elementwise multiply scales the corresponding up value by that result. Together these operations form the SwiGLU activation. The down projection returns to 2,560 features so the second residual addition can combine vectors of the same width. [Layer implementation](layer.py)

**What actually runs.** Make assembles the lane firmware, compiles two Verilator executables, and starts `layer.py`. Python generates the inputs, computes independent reference values, then starts both executables. One executable accepts signed INT4 tiles and clocks the four ACIM macros until each tile finishes. The other runs RV32I programs on eight modeled DarkRISCV cores. The Python host schedules jobs, stages weights, computes quantization scales and rotary angles, and owns the KV cache. Its calls to a lane complete in program order. This arrangement verifies the layer's numerical dataflow. Measuring parallel issue or whole-chip latency would require a shared clock and an RTL dispatcher. [Layer runner](layer.py) · [MPU bus](../lab5-HelloPIMOperator/operators/pim_bus.cpp) · [DarkRISCV harness](../lab5-HelloPIMOperator/operators/lane_bus.cpp)

## 2. Identify the tensors before connecting them

The **hidden state** is the vector passed from one layer to the next. A **head** is one slice of a projection that computes its own attention scores. Several query heads can read the same key and value head. A **projection** is a matrix-vector product that changes which features the vector represents.

| Dimension | Small run | Qwen3-4B |
|---|---:|---:|
| Hidden width | 32 | 2,560 |
| Query heads × width | 4 × 8 = 32 | 32 × 128 = 4,096 |
| Key/value heads × width | 2 × 8 = 16 | 8 × 128 = 1,024 |
| MLP intermediate width | 64 | 9,728 |
| Query heads sharing one KV head | 2 | 4 |

The trained model sets the head width explicitly to 128. Dividing the hidden width by the query-head count would give 80 and the wrong query and output matrices. Qwen3 also learns four normalization vectors per layer. The two hidden-state vectors have length 2,560. The Q and K normalization vectors have length 128 and are reused across heads. [Published Qwen3-4B configuration](https://huggingface.co/Qwen/Qwen3-4B/blob/main/config.json) · [Qwen3 decoder implementation, Transformers v4.51.3](https://github.com/huggingface/transformers/blob/v4.51.3/src/transformers/models/qwen3/modeling_qwen3.py)

Every linear weight uses the layout `W[output,input]`. The hardware executes 128 input columns by 128 output rows at a time. Here is the full schedule for one token.

| Projection | Weight shape | Input groups × output blocks | MPU jobs |
|---|---:|---:|---:|
| Q | 4,096 × 2,560 | 20 × 32 | 640 |
| K | 1,024 × 2,560 | 20 × 8 | 160 |
| V | 1,024 × 2,560 | 20 × 8 | 160 |
| O | 2,560 × 4,096 | 32 × 20 | 640 |
| Gate | 9,728 × 2,560 | 20 × 76 | 1,520 |
| Up | 9,728 × 2,560 | 20 × 76 | 1,520 |
| Down | 2,560 × 9,728 | 76 × 20 | 1,520 |
| **Total** | | | **6,160** |

A job returns 128 raw INT32 row sums. The host multiplies them by the input-group and per-row weight scales, then accumulates in FP32. It re-quantizes the FP32 input to each new projection. The checkpoint has no bias for these seven matrices. [Projection implementation](../lab5-HelloPIMOperator/operators/runtime.py)

## 3. Wire the attention half

Follow `decode` in the runner with the small shape first. Keep the original input `x` alive until the first residual addition. [Executable layer schedule](layer.py)

1. **Normalize the input.** Compute its mean square, add `1e-6`, take the reciprocal square root and multiply each input by this factor and its learned gain. Send the same resulting vector to Q, K and V.
2. **Form the heads.** Apply the three PIM projections. Reshape Q into `[query_heads, head_dim]` and K and V into `[kv_heads, head_dim]`. Independently RMS-normalize each Q head and each K head using their respective learned gains.
3. **Encode the position.** For position `p` and pair index `j`, compute `angle = p * 1,000,000^(-2*j/head_dim)`. Rotate coordinates `j` and `j+head_dim/2` together. At full width, coordinate 0 pairs with 64. Rotate Q and K. V goes directly to the cache.
4. **Gather visible keys and values.** Read the previous entries and include the current rotated K and unrotated V. The host represents each cache as a list of arrays with shape `[kv_heads,head_dim]`, one per position. A full-size entry costs `2 × 8 × 128 × 4 = 8,192` bytes for the two FP32 caches together. The runner commits the new entry after the token's final output is ready.
5. **Compute attention.** For query head `h`, select KV head `h // 4` at full width. Heads 0–3 read KV head 0, and heads 4–7 read KV head 1. For every visible position, compute the dot product of Q and K and divide by `sqrt(128)`. Apply stable softmax across **positions for this head**. Starting from zero, call `axpy` to add each probability times its corresponding cached value vector.
6. **Return to the hidden width.** Concatenate the 32 heads in head order to get 4,096 numbers. Apply the O projection to get 2,560 and add the original `x`. Keep this result, `residual`, for the second half of the layer.

At position zero there is one visible key, so every head's softmax returns one. Its context must equal that head's selected V. Position one is the first test that can reveal an omitted cache read or a softmax taken along the wrong axis. The current query is temporary. Only K and V survive into the next call.

## 4. Wire the feed-forward half

1. Apply the learned post-attention RMSNorm to `residual`. Feed the same normalized vector to the gate and up projections.
2. Run both PIM projections. Each produces 9,728 values. Multiply `SiLU(gate[i])` by `up[i]` at each index. The runner splits these independent indices across eight lane scratchpads. Each slice contains 1,216 elements.
3. Send the product through the down projection and add the saved `residual`. This is the layer output. The next **position** processed by this same layer brings a new hidden-state input. The current **layer output** would instead feed the next layer for the same position.

The host can release Q, attention scores and head contexts after the O projection. It can release the gate and up buffers after forming their product. Retain the two cache arrays when processing subsequent positions. [Lane operator implementations](../lab5-HelloPIMOperator/operators/vector.c)

## 5. Run at Qwen3-4B width and check the result

The default `make layer` run uses seeded synthetic weights and two seeded FP32 inputs. At each position all seven projections run on the MPU, including gate, up and down. For two positions, look for this final line.

```text
PASS one decoder layer: 12320 MPU jobs, 468 DarkRISCV dispatches
```

The checker takes a second route through the same mathematics. NumPy processes both inputs as a batch, builds a causal mask that hides future positions, and computes quantized matrix products directly in software. The Verilator path processes one position at a time and reads its cache. The test compares each of the seven projections, the attention context, the stored K and V, and all 2,560 final outputs per position. This is a comparison to the quantized layer. To measure the modeling error from INT4 itself, the comparison would instead need the original floating-point projections. [Independent numerical reference](reference.py)

When a check fails, read its position and tensor name. A Q mismatch at position zero narrows the problem to input normalization, weight layout or tile scaling. Matching Q/K/V with a bad `key_cache` points to head-wise normalization or RoPE. If only position one fails at `context`, inspect which KV head and which time indices that query used. For a mismatch at `down`, inspect the gate activation, elementwise multiply and input groups before the last residual addition.

## 6. Run with the published layer weights

For layer 0 all eleven tensors are in `model-00001-of-00003.safetensors`. The exporter reads only the selected tensor ranges. It expands the checkpoint's BF16 values to FP32, preserves row order and writes eleven NumPy files. The runtime applies the Lab 5 INT4 scheme to the seven projection matrices. The learned normalization gains stay in FP32. [Exporter](export_layer.py)

In a two-position test using the published layer 0 weights, the largest absolute difference across the 5,120 final values was `7.15e-7` against the quantized reference. The runner prints the weight directory, the cache length and the maximum absolute output error for each position. Both the hardware and reference receive the same two deterministic hidden-state inputs, so this experiment can be reproduced without downloading a tokenizer or embedding table. The result is the hidden-state output of the selected decoder layer under the lab's quantization rule. [Layer driver](layer.py) · [Design and license notices](design/THIRD_PARTY.md)

## 7. Measure layer cycles and calculate tokens per second

### Run one token, then two

Both performance commands start with an empty KV cache. The two-token run processes consecutive positions in the same sequence, so position 1 can attend to the key and value from position 0.

The first command builds cycle-counted simulators in `build/perf/`. Both commands run the full 2,560-wide layer and check its outputs against the NumPy reference. Look for `PASS one decoder layer`, followed by the MPU count, the eight lane counts and their sum, and the total. These measurements use the default synthetic weights (seed 304), inputs from seed 2026, Verilator 5.054, and RV32I firmware compiled with Zig 0.14.1. To repeat the experiment with the weights from Section 6, add `WEIGHTS=build/weights-layer0` to each command. Firmware and input values can affect the lane count.

| Run | MPU cycles | Eight lanes combined | Total cycles |
|---|---:|---:|---:|
| One token (position 0) | 178,362,800 | 168,007,205 | 346,370,005 |
| Two tokens (positions 0 and 1) | 356,725,600 | 347,014,010 | 703,739,610 |

### Count the clock edges

One rising clock edge counts as one cycle. The MPU counter starts after its one-time reset and includes input staging, weight reads, computation, status polling, and output reads. Each lane counts its eight reset cycles per dispatch and the firmware cycles through completion. The host waits for each request to finish before issuing the next, so add the MPU and all eight lane counts. Host-side quantization, rescaling, KV-cache management, and Python execution time fall outside these RTL counters. [Clock counters](cycle_counter.sv) · [Measurement script](performance.py)

For position 0, the seven projections issue **6,160 MPU tile jobs**. A single 128 × 128 tile measured **28,955 cycles**. Multiply to cross-check the MPU report, then add the lane sum for the 202 firmware dispatches.

```text
MPU cycles   = 6,160 × 28,955             = 178,362,800
Layer cycles = 178,362,800 + 168,007,205 = 346,370,005
```

To isolate position 1, subtract the one-token total from the two-token total. It took `703,739,610 − 346,370,005 = 357,369,605` cycles. Its seven projections still require 6,160 tiles. With two visible positions, each of the 32 query heads issues one additional dot product and one additional weighted-value update. Those 64 extra dispatches bring the second position to 266.

### Convert cycles to throughput at 1 GHz

A 1 GHz clock supplies 1,000,000,000 cycles per second, so **one cycle takes 1 ns**. For a run that completes `N` token positions in `C` total cycles, elapsed seconds equal `C / 1,000,000,000`. Divide the number of completed positions by that time.

```text
One token:   346,370,005 cycles × 1 ns/cycle = 0.346370005 s
             1 token / 0.346370005 s         = 2.887 layer tokens/s

Two tokens:  703,739,610 cycles × 1 ns/cycle = 0.703739610 s
             2 tokens / 0.703739610 s        = 2.842 layer tokens/s
```

**Estimate generated output tokens per second.** Each layer pass produces a hidden vector for one position. Generating one token requires all 36 decoder layers, final normalization, the vocabulary projection, and token selection. If all 36 layers run serially and each costs the measured position-0 count, the decoder stack alone takes `36 × 346,370,005 = 12,469,320,180` cycles per token. At 1 GHz that is **12.469 seconds per token**, or **0.0802 tokens/s**. For an end-to-end measurement, include the remaining model and data-movement time and divide the number of newly generated tokens by the elapsed decode time. Use the context length of the intended workload because attention cost grows with the visible prefix.
