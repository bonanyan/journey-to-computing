#ifndef PIM_LINEAR_I4_H
#define PIM_LINEAR_I4_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum pim_register {
    PIM_STATUS = 0x00, PIM_COMMAND = 0x04, PIM_ACT_ADDR = 0x08, PIM_ACT_DATA = 0x0c,
    PIM_OUT_ADDR = 0x10, PIM_OUT_DATA = 0x14, PIM_PC = 0x18,
    PIM_WEIGHT_BASE = 0x20, PIM_SHAPE = 0x24, PIM_K_RANGE = 0x28
};
enum { PIM_LINEAR = 1, PIM_END = 7 };
enum { PIM_BUSY = 1, PIM_DONE = 2, PIM_ERROR = 4 };

// The platform supplies ordered 32-bit MMIO transfers. For physical MMIO use
// pim_mmio_device() on an uncached device mapping. Give each job exclusive ownership.
struct pim_device {
    void *context;
    uint32_t (*read)(void *context, unsigned byte_offset);
    void (*write)(void *context, unsigned byte_offset, uint32_t value);
};
struct pim_device pim_mmio_device(volatile uint32_t *registers);

// x is an unpacked signed INT4 vector. weight_base addresses packed W[N,K]
// in accelerator-visible memory. Output contains the exact signed dot products.
int pim_linear_i4(const struct pim_device *dev, const int8_t *x, int32_t *y,
                  size_t k, size_t n, uint32_t weight_base);

// Symmetric groupwise quantization. Each scale[n,g] combines activation and
// weight scales. Optional FP32 bias has n entries. Scratch holds n INT32 values.
int pim_linear_i4_scaled(const struct pim_device *dev, const int8_t *x, float *y,
                         size_t k, size_t n, uint32_t weight_base, size_t group_size,
                         const float *scales, const float *bias, int32_t *scratch);

#ifdef __cplusplus
}
#endif
#endif
