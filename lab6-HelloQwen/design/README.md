# MPU stage 1 · Qwen3-4B query projection with four ACIM macros

The MPU is Lab 4’s processing-in-memory accelerator. Its hardware top is `mpu` and its first C operator is `pim_linear_i4`. Build it step by step in the [Lab 4 tutorial](../lab4-tutorial.md).

For one token, `pim_linear_i4` computes `y[n] = sum_k x[k] * W[n,k]` for a signed INT4 input and signed INT4 weights. Its default dimensions are 2,560 inputs and 4,096 outputs, corresponding to 32 query heads of width 128. The C operator returns one signed INT32 value per output. The example uses reproducible synthetic weights and activations. [Qwen3-4B configuration](https://huggingface.co/Qwen/Qwen3-4B/blob/main/config.json)

## Run the projection

From Lab 4, with Verilator 5, Make, and C/C++ compilers on the path, run:

```sh
make -C design test
```

`smoke` checks a 16-input, 64-output projection across two output tiles with result backpressure. `qwen3-q-projection` compiles the C operator and runs it against the Verilated RTL through the host register bus. The test generates a signed reference matrix first, then packs a copy into the simulated external weight memory. It checks every output, exercises delayed weight grants and responses, and repeats the projection with group-specific scales. [Small RTL bench](tb/tb_mpu.sv) · [C operator integration test](tb/operator_test.cpp)

## Follow one output through the accelerator

1. The CPU packs eight signed activations into each MMIO word, sets the byte address of the external weights, and queues `LINEAR` and `END`. A four-entry FIFO accepts the second instruction while the first executes. The accelerator PC counts retired 32-bit instructions from reset.
2. The controller reads a 4-input by 32-output tile from the packed matrix into a 64-byte buffer. Weights use output-major `W[N,K]` order. The low nibble of each byte holds the even input index. Each of four ACIM macros computes eight adjacent output columns.
3. For an activation bit and a weight bit, the controller drives the macro with values 0 or 8 and programs weights of 0 or 32. Each matching pair adds 256 to the analog model's internal sum. Its six-bit ADC returns bits 13 through 8, so its output is the count of matching rows, from zero through four.
4. Sixteen activation/weight bit-plane combinations reconstruct the signed INT4 dot product. The digital accumulator shifts each count by the sum of the bit positions and subtracts it when exactly one operand contributes its sign bit. After all input tiles, the chip writes 32 signed results to local memory and advances to the next output tile. [Accelerator RTL](rtl/mpu.sv) · [Lab 1 ACIM model](third_party/pislib/APIM.v)

For the full Q projection, 640 input tiles and 128 output tiles require 1,310,720 bank-wide compute pulses. Weight programming, transfers, control, and writeback consume additional cycles. Each complete projection reads 5,242,880 packed weight bytes.

## Use the C operator

Place the packed weight tensor in accelerator-visible memory. Create a `pim_device` with `pim_mmio_device` on a platform with ordered, uncached MMIO, or supply platform-specific read and write callbacks. One caller owns the device during a synchronous operator call. The following call assumes a 2,560-element signed input and storage for 4,096 output words. [C interface](sw/pim_linear_i4.h) · [Implementation](sw/pim_linear_i4.c)

```c
int rc = pim_linear_i4(&dev, input, output, 2560, 4096, weight_byte_address);
```

For symmetric groupwise quantization, `pim_linear_i4_scaled` computes one INT32 partial projection per input group. Host software multiplies each partial by its combined activation and weight scale and adds the optional bias once per output. Pass scales in `[output][group]` order, an optional FP32 bias vector, and an INT32 scratch vector of length N. Group size must be a multiple of four. The RTL parameters select the physical input and output dimensions at build time. K must be divisible by four and N by 32.

## Register and memory interfaces

| Byte offset | Register | Operation |
|---|---|---|
| 0x00 | STATUS | Bits 0, 1, and 2 report busy, done, and sticky fault. Bits 15 through 8 give FIFO occupancy. |
| 0x04 | COMMAND | A word write enqueues LINEAR (1) or END (7). END signals completion once preceding work retires. |
| 0x08, 0x0c | ACT_ADDR, ACT_DATA | Set the activation word index, then write packed words. Each data write advances the index. |
| 0x10, 0x14 | OUT_ADDR, OUT_DATA | Set the output index, then read signed INT32 results. Each data read advances the index. |
| 0x18 | PC | Number of retired instructions. On an illegal command, this identifies its zero-based position. Reset clears the fault and FIFO. |
| 0x20 | WEIGHT_BASE | Byte address of the first packed weight in external memory. |
| 0x24 | SHAPE | Upper 16 bits contain K. Lower 16 bits contain N. |
| 0x28 | K_RANGE | Upper 16 bits hold the exclusive end, lower 16 bits the first input. Both bounds align to four. Reset selects the full input. |

Host transactions transfer on a rising edge with `host_valid && host_ready`. The weight port holds its byte address until grant, supports one outstanding read and consumes the response when `weight_rvalid` is high. Each `result_valid && result_ready` handshake commits one local output word. Tie `result_ready` high when using MMIO readback alone.

Stage 1 uses this read-only weight transfer engine and local operand buffers. The programmable eight-core VPU, Transformer attention and KV cache belong to later stages. Source provenance and redistribution information are recorded alongside the design. [Third-party notices](THIRD_PARTY.md)
