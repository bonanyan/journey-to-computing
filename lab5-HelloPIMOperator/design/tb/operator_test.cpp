#include "Vmpu.h"
#include "pim_linear_i4.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

static void check(bool ok, const char *message)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL %s\n", message);
        std::exit(1);
    }
}

// One outstanding weight read. Exercise delayed grants and variable response
// latency independently of host MMIO polling and output backpressure.
struct Simulation {
    Vmpu rtl;
    static constexpr uint32_t base = 0x1357;
    std::vector<uint8_t> weights;
    uint64_t cycles = 0, reads = 0, outputs = 0, response_cycle = 0;
    uint8_t pending_data = 0;
    unsigned n = 0;

    void tick()
    {
        rtl.clk = 0;
        rtl.weight_gnt = cycles % 7 != 0;
        rtl.weight_rvalid = response_cycle && response_cycle == cycles;
        rtl.weight_rdata = pending_data;
        rtl.result_ready = cycles % 5 != 0;
        rtl.eval();
        if (rtl.weight_rvalid) response_cycle = 0;
        if (!rtl.rst && rtl.weight_req && rtl.weight_gnt) {
            const uint32_t offset = rtl.weight_byte_addr - base;
            check(offset < weights.size() && !response_cycle, "weight read outside model or overlapping");
            pending_data = weights[offset];
            response_cycle = cycles + 1 + (reads % 3);
            ++reads;
        }
        if (!rtl.rst && rtl.result_valid && rtl.result_ready) {
            check(rtl.result_index == outputs % n, "output order");
            ++outputs;
        }
        rtl.clk = 1;
        rtl.eval();
        rtl.clk = 0;
        rtl.eval();
        check(++cycles < 150000000, "simulation timeout");
    }

    static uint32_t read(void *context, unsigned addr)
    {
        auto &sim = *static_cast<Simulation *>(context);
        // Let the accelerator run between software status polls.
        if (addr == PIM_STATUS && sim.rtl.busy)
            for (int i = 0; i < 64 && sim.rtl.busy; ++i) sim.tick();
        sim.rtl.host_addr = addr;
        sim.rtl.host_valid = 1;
        sim.rtl.host_write = 0;
        sim.rtl.eval();
        const uint32_t value = sim.rtl.host_rdata;
        sim.tick();
        sim.rtl.host_valid = 0;
        return value;
    }

    static void write(void *context, unsigned addr, uint32_t value)
    {
        auto &sim = *static_cast<Simulation *>(context);
        sim.rtl.host_addr = addr;
        sim.rtl.host_wdata = value;
        sim.rtl.host_write = 1;
        sim.rtl.host_valid = 1;
        sim.rtl.eval();
        while (!sim.rtl.host_ready) sim.tick();
        sim.tick();
        sim.rtl.host_valid = 0;
        sim.rtl.host_write = 0;
    }
};

static uint32_t next_random(uint32_t &state)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

int main()
{
    Simulation sim;
    sim.rtl.rst = 1;
    sim.tick();
    sim.rtl.rst = 0;
    pim_device dev{&sim, Simulation::read, Simulation::write};
    const auto shape = dev.read(&sim, PIM_SHAPE);
    const unsigned k = shape >> 16, n = shape & 0xffff;
    sim.n = n;
    const unsigned group_size = std::min(128u, k);
    const unsigned groups = (k + group_size - 1) / group_size;
    std::vector<int8_t> x(k), w(k * n);
    std::vector<int32_t> reference(n), raw(n);
    std::vector<float> scales(n * groups), bias(n), expected(n), scaled(n);
    uint32_t random = 0x43f94c1u;
    for (auto &val : x) val = int(next_random(random) >> 28) - 8;
    for (auto &val : w) val = int(next_random(random) >> 28) - 8;
    sim.weights.resize(w.size() / 2);
    for (size_t i = 0; i < w.size(); i += 2)
        sim.weights[i / 2] = (uint8_t(w[i]) & 15) | ((uint8_t(w[i + 1]) & 15) << 4);

    for (unsigned out = 0; out < n; ++out) {
        bias[out] = float(int(out % 13) - 6) / 8;
        expected[out] = bias[out];
        for (unsigned g = 0; g < groups; ++g) {
            int32_t dot = 0;
            for (unsigned i = g * group_size; i < std::min(k, (g + 1) * group_size); ++i)
                dot += int(x[i]) * int(w[out * k + i]);
            reference[out] += dot;
            scales[out * groups + g] = float((out + g) % 7 + 1) / 32;
            expected[out] += float(dot) * scales[out * groups + g];
        }
    }
    check(!pim_linear_i4(&dev, x.data(), raw.data(), k, n, sim.base), "C operator failed");
    check(reference == raw, "signed INT32 projection mismatch");
    check(sim.reads == k * n / 2 && sim.outputs == n, "raw projection transfer count");
    std::printf("PASS pim_linear_i4 K=%u N=%u checked=%u weight_bytes=%llu cycles=%llu\n",
                k, n, n, (unsigned long long)sim.reads, (unsigned long long)sim.cycles);

    check(!pim_linear_i4_scaled(&dev, x.data(), scaled.data(), k, n, sim.base, group_size,
                                scales.data(), bias.data(), raw.data()), "scaled C operator failed");
    check(expected == scaled, "groupwise projection mismatch");
    check(sim.reads == k * n && sim.outputs == uint64_t(n) * (groups + 1), "group transfer count");
    check(dev.read(&sim, PIM_PC) == 2 * (groups + 1), "retired instruction count");
    std::printf("PASS grouped projection groups=%u outputs=%u\n", groups, n);
    sim.rtl.final();
}
