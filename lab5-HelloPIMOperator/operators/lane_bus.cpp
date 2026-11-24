// Eight independently addressable Verilated cores. The host serializes dispatches.
#include "Vdarkriscv.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <vector>

struct Lane {
    Vdarkriscv cpu;
    std::vector<uint32_t> ram = std::vector<uint32_t>(0x50000 / 4);
    uint32_t instruction = 0;  // synchronous instruction RAM, one cycle from fetch to decode
    void tick() {
        cpu.CLK = 0;
        cpu.eval();
        cpu.IDATA = instruction;
        instruction = cpu.IADDR / 4 < ram.size() ? ram[cpu.IADDR / 4] : 0;
        cpu.eval();
        const bool access = cpu.DDREQ && !cpu.RES;
        const uint32_t addr = cpu.DADDR / 4;
        if (access && addr >= ram.size()) throw std::runtime_error("lane address outside scratchpad");
        cpu.DATAI = access ? ram[addr] : 0;
        cpu.eval();
        if (access && cpu.DWR) {
            uint32_t mask = 0;
            for (unsigned i = 0; i < 4; ++i)
                if (cpu.DBE & (1u << i)) mask |= 255u << (8*i);
            ram[addr] = (ram[addr] & ~mask) | (cpu.DATAO & mask);
        }
        cpu.CLK = 1;
        cpu.eval();
    }
    explicit Lane(const std::vector<char>& firmware) {
        std::memcpy(ram.data(), firmware.data(), firmware.size());
        cpu.IDACK = cpu.DDACK = 1;
        cpu.IBERR = cpu.DBERR = cpu.IRQ = 0;
    }
    void run(uint32_t op, uint32_t count, uint32_t alpha) {
        cpu.RES = 1;
        for (int i = 0; i < 8; ++i) tick();
        auto *job = &ram[0x10000/4];
        job[0] = op; job[1] = count; job[2] = 0; job[3] = alpha;
        cpu.RES = 0;
        for (unsigned i = 0; !job[2]; ++i) {
            if (i == 300000000) throw std::runtime_error("vector firmware timeout");
            tick();
        }
    }
};

int main(int argc, char **argv) {
    if (argc != 2) return 1;
    std::ifstream file(argv[1], std::ios::binary);
    std::vector<char> firmware{std::istreambuf_iterator<char>(file), {}};
    if (firmware.empty() || firmware.size() >= 0xe000) return 1;
    std::array<std::unique_ptr<Lane>, 8> lanes;
    for (auto &lane : lanes) lane.reset(new Lane(firmware));
    while (true) {
        uint32_t header[4];
        auto got = std::fread(header, 1, sizeof(header), stdin);
        if (!got && std::feof(stdin)) break;
        if (got != sizeof(header)) return 1;
        auto op = header[0], count = header[1], lane_id = header[2];
        if (!count || count > 16384 || lane_id >= 8 || op < 1 || op > 7 || (op == 3 && count % 2)) return 1;
        auto &lane = *lanes[lane_id];
        for (unsigned addr : {0x20000, 0x30000})
            if (std::fread(&lane.ram[addr/4], 4, count, stdin) != count) return 1;
        lane.run(op, count, header[3]);
        size_t nout = op == 4 ? 1 : count;
        if (std::fwrite(&lane.ram[0x40000/4], 4, nout, stdout) != nout) return 1;
        std::fflush(stdout);
    }
    for (auto &lane : lanes) lane->cpu.final();
}
