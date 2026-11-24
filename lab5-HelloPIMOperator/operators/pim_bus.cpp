// One physical K=128, N=128 job per request. The host drives the Lab 4 C API.
#include "Vmpu.h"
#include "pim_linear_i4.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <stdexcept>

struct Bus {
    Vmpu rtl;
    std::array<uint8_t, 128 * 128 / 2> weights;
    unsigned cycles = 0, reads = 0, outputs = 0;
    void tick() {
        rtl.clk = 0;
        rtl.weight_gnt = 1;
        rtl.result_ready = 1;
        rtl.eval();
        rtl.weight_rvalid = rtl.weight_req;
        if (rtl.weight_req) {
            rtl.weight_rdata = weights.at(rtl.weight_byte_addr);
            ++reads;
        }
        if (rtl.result_valid) ++outputs;
        rtl.eval();
        rtl.clk = 1;
        rtl.eval();
        rtl.clk = 0;
        rtl.eval();
        if (++cycles > 100000) throw std::runtime_error("MPU timeout");
    }
    static uint32_t read(void *p, unsigned addr) {
        auto &b = *static_cast<Bus *>(p);
        b.rtl.host_addr = addr;
        b.rtl.host_valid = 1;
        b.rtl.host_write = 0;
        b.rtl.eval();
        auto value = b.rtl.host_rdata;
        b.tick();
        b.rtl.host_valid = 0;
        return value;
    }
    static void write(void *p, unsigned addr, uint32_t value) {
        auto &b = *static_cast<Bus *>(p);
        b.rtl.host_addr = addr;
        b.rtl.host_wdata = value;
        b.rtl.host_valid = b.rtl.host_write = 1;
        b.rtl.eval();
        while (!b.rtl.host_ready) b.tick();
        b.tick();
        b.rtl.host_valid = b.rtl.host_write = 0;
    }
};

int main() {
    Bus b;
    b.rtl.rst = 1;
    b.tick();
    b.rtl.rst = 0;
    pim_device dev{&b, Bus::read, Bus::write};
    std::array<int8_t, 128> x;
    std::array<int32_t, 128> out;
    uint32_t pc = 0;
    while (true) {
        size_t got = std::fread(x.data(), 1, x.size(), stdin);
        if (!got && std::feof(stdin)) break;
        if (got != x.size() || std::fread(b.weights.data(), 1, b.weights.size(), stdin) != b.weights.size()) return 1;
        b.cycles = b.reads = b.outputs = 0;
        pc += 2;
        if (pim_linear_i4(&dev, x.data(), out.data(), 128, 128, 0) ||
            Bus::read(&b, PIM_PC) != pc || b.reads != b.weights.size() || b.outputs != out.size()) {
            std::fprintf(stderr, "MPU command, retirement or transfer count failed\n");
            return 1;
        }
        if (std::fwrite(out.data(), sizeof(int32_t), out.size(), stdout) != out.size()) return 1;
        std::fflush(stdout);
    }
    b.rtl.final();
}
