#include "pim_linear_i4.h"

static uint32_t mmio_read(void *context, unsigned offset)
{
    return ((volatile uint32_t *)context)[offset / 4];
}

static void mmio_write(void *context, unsigned offset, uint32_t value)
{
    ((volatile uint32_t *)context)[offset / 4] = value;
}

struct pim_device pim_mmio_device(volatile uint32_t *registers)
{
    struct pim_device dev = {(void *)registers, mmio_read, mmio_write};
    return dev;
}

static int stage_input(const struct pim_device *dev, const int8_t *x, size_t k,
                       size_t n, uint32_t weight_base)
{
    if (!dev || !dev->read || !dev->write || !x || !k || !n || k % 4 || n % 32 ||
        k > UINT16_MAX || n > UINT16_MAX || (uint64_t)weight_base + k * n / 2 > (1ULL << 32))
        return -1;
    if (dev->read(dev->context, PIM_SHAPE) != ((uint32_t)k << 16 | (uint32_t)n) ||
        (dev->read(dev->context, PIM_STATUS) & (PIM_BUSY | PIM_ERROR)))
        return -1;
    for (size_t i = 0; i < k; ++i)
        if (x[i] < -8 || x[i] > 7)
            return -1;

    dev->write(dev->context, PIM_WEIGHT_BASE, weight_base);
    dev->write(dev->context, PIM_ACT_ADDR, 0);
    for (size_t i = 0; i < k; i += 8) {
        uint32_t word = 0;
        for (size_t j = 0; j < 8 && i + j < k; ++j)
            word |= ((uint32_t)x[i + j] & 15u) << (4 * j);
        dev->write(dev->context, PIM_ACT_DATA, word);
    }
    return 0;
}

static int project_range(const struct pim_device *dev, size_t first, size_t end,
                         size_t n, int32_t *y)
{
    dev->write(dev->context, PIM_K_RANGE, (uint32_t)(end << 16) | (uint32_t)first);
    dev->write(dev->context, PIM_COMMAND, PIM_LINEAR);
    dev->write(dev->context, PIM_COMMAND, PIM_END);
    uint32_t status;
    do {
        status = dev->read(dev->context, PIM_STATUS);
        if (status & PIM_ERROR)
            return -1;
    } while (!(status & PIM_DONE));
    dev->write(dev->context, PIM_OUT_ADDR, 0);
    for (size_t i = 0; i < n; ++i)
        y[i] = (int32_t)dev->read(dev->context, PIM_OUT_DATA);
    return 0;
}

int pim_linear_i4(const struct pim_device *dev, const int8_t *x, int32_t *y,
                  size_t k, size_t n, uint32_t weight_base)
{
    if (!y || stage_input(dev, x, k, n, weight_base))
        return -1;
    return project_range(dev, 0, k, n, y);
}

int pim_linear_i4_scaled(const struct pim_device *dev, const int8_t *x, float *y,
                         size_t k, size_t n, uint32_t weight_base, size_t group_size,
                         const float *scales, const float *bias, int32_t *scratch)
{
    if (!y || !scratch || !scales || !group_size || group_size > k || group_size % 4 ||
        stage_input(dev, x, k, n, weight_base))
        return -1;
    const size_t groups = (k + group_size - 1) / group_size;
    for (size_t col = 0; col < n; ++col)
        y[col] = bias ? bias[col] : 0;
    for (size_t g = 0; g < groups; ++g) {
        const size_t end = (g + 1) * group_size;
        if (project_range(dev, g * group_size, end < k ? end : k, n, scratch))
            return -1;
        for (size_t col = 0; col < n; ++col)
            y[col] += (float)scratch[col] * scales[col * groups + g];
    }
    return 0;
}
