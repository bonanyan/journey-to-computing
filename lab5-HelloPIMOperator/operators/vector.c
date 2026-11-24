/* The scalar C loops compile to the same RV32I soft-float ABI as kernels.S. */
#include <stdint.h>
extern void add_vector(const float *, const float *, float *, unsigned);
extern float dot_vector(const float *, const float *, unsigned);
enum { ADD = 1, RMS, ROPE, DOT, SOFTMAX, SWIGLU, AXPY };

static float exponential(float x)
{
    /* Range reduction by ln(2), then Taylor expansion on [-ln(2)/2, ln(2)/2]. */
    if (x < -80.0f) return 0;
    int k = (int)(x * 1.44269504f + (x >= 0 ? 0.5f : -0.5f));
    float r = (x - k * 0.693145751953125f) - k * 1.42860677e-6f;
    float sum = 1.0f / 5040.0f;
    sum = 1.0f/720.0f + r*sum;
    sum = 1.0f/120.0f + r*sum;
    sum = 1.0f/24.0f + r*sum;
    sum = 1.0f/6.0f + r*sum;
    sum = 0.5f + r*sum;
    sum = 1.0f + r*(1.0f + r*sum);
    union { uint32_t bits; float value; } scale = { (uint32_t)(127 + k) << 23 };
    return sum * scale.value;
}

static float inverse_sqrt(float x)
{
    union { float value; uint32_t bits; } guess = { x };
    guess.bits = 0x5f3759dfu - (guess.bits >> 1);
    float y = guess.value;
    for (int i = 0; i < 3; ++i)
        y *= 1.5f - 0.5f * x * y * y;
    return y;
}

void dispatch(void)
{
    volatile uint32_t *job = (volatile uint32_t *)0x10000;
    const unsigned n = job[1];
    const float *x = (const float *)0x20000, *y = (const float *)0x30000;
    float *out = (float *)0x40000;
    float sum = 0;
    switch (job[0]) {
    case ADD:
        add_vector(x, y, out, n);
        break;
    case RMS:
        for (unsigned i = 0; i < n; ++i) sum += x[i] * x[i];
        float factor = inverse_sqrt(sum / n + 1e-6f);
        for (unsigned i = 0; i < n; ++i) out[i] = (x[i] * factor) * y[i];
        break;
    case ROPE:
        /* y holds cosines in its first half, then sines. */
        for (unsigned i = 0; i < n / 2; ++i) {
            unsigned j = i + n / 2;
            out[i] = x[i] * y[i] - x[j] * y[j];
            out[j] = x[j] * y[i] + x[i] * y[j];
        }
        break;
    case DOT:
        out[0] = dot_vector(x, y, n);
        break;
    case SOFTMAX: {
        float max = x[0];
        for (unsigned i = 1; i < n; ++i) if (x[i] > max) max = x[i];
        for (unsigned i = 0; i < n; ++i) {
            out[i] = exponential(x[i] - max);
            sum += out[i];
        }
        for (unsigned i = 0; i < n; ++i) out[i] /= sum;
        break;
    }
    case SWIGLU:
        for (unsigned i = 0; i < n; ++i) {
            float e = exponential(x[i] < 0 ? x[i] : -x[i]);
            float sigmoid = (x[i] < 0 ? e : 1.0f) / (1.0f + e);
            out[i] = (x[i] * sigmoid) * y[i];
        }
        break;
    case AXPY: {
        const float alpha = *(volatile float *)0x1000c;
        for (unsigned i = 0; i < n; ++i) out[i] = x[i] + alpha * y[i];
        break;
    }
    }
}
