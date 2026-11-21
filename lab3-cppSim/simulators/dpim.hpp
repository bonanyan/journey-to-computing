// Clocked C++ model of Lab 2's dpim_obi wrapper and DPIM macro.
#pragma once

#include <array>
#include <cstdint>
#include <ostream>

struct ObiRequest {
    std::uint32_t addr, wdata;
    unsigned be;
    bool write;
};

struct ObiResponse {
    bool grant, valid;
    std::uint32_t data;
};

class Dpim {
public:
    static constexpr std::uint32_t base = 0xc0030000;
    static constexpr std::uint32_t window = 0x8000;
    std::uint64_t edges = 0;
    unsigned transactions = 0, commands = 0;

    // Sample the pre-edge response, then update flops as on one rising edge.
    // A null request drives req low. Each new instance starts just after reset.
    ObiResponse tick(const ObiRequest* request, std::ostream* trace = nullptr) {
        ++edges;
        ObiResponse response{};
        switch (state) {
        case State::Idle:
            if (request) {
                response.grant = true;
                latched = *request;
                offset = (latched.addr - base) & (window - 4);
                if (commandStore()) done = false;
                ++transactions;
                state = State::Access;
            }
            break;
        case State::Access:
            state = State::Response;
            if (offset < command) {
                if (latched.write && latched.be == 15) {
                    weights[offset / 4] = latched.wdata;
                } else {
                    scalarQ = weights[offset / 4];
                    if (latched.write) state = State::MergeWrite;
                }
            } else if (latched.write && inBank(input)) {
                operands[lane()] = merged(operands[lane()]);
            } else if (commandStore()) {
                compute();
                done = true;
                ++commands;
                if (trace) {
                    *trace << "compute edge=" << edges << " op=" << ((latched.wdata >> 8) & 3)
                           << " row=" << (latched.wdata & 127) << " lane31=" << results[31] << '\n';
                }
            }
            break;
        case State::MergeWrite:
            weights[offset / 4] = merged(scalarQ);
            state = State::Response;
            break;
        case State::Response:
            response.valid = true;
            if (!latched.write) response.data = readback();
            state = State::Idle;
            break;
        }
        return response;
    }

private:
    enum class State { Idle, Access, MergeWrite, Response };
    static constexpr unsigned command = 0x4000, status = 0x4004;
    static constexpr unsigned input = 0x4100, result = 0x4200;
    State state = State::Idle;
    ObiRequest latched{};
    unsigned offset = 0;
    bool done = false;
    std::uint32_t scalarQ = 0;
    // Deterministic two-state power-up. Firmware programs the weights it uses.
    std::array<std::uint32_t, 4096> weights{};
    std::array<std::uint32_t, 32> operands{}, results{};

    bool commandStore() const { return latched.write && offset == command && latched.be == 15; }
    bool inBank(unsigned start) const { return offset >= start && offset < start + 128; }
    unsigned lane() const { return (offset >> 2) & 31; }

    std::uint32_t merged(std::uint32_t previous) const {
        std::uint32_t mask = 0;
        for (unsigned byte = 0; byte < 4; ++byte)
            if (latched.be & (1u << byte)) mask |= 0xffu << (8 * byte);
        return (previous & ~mask) | (latched.wdata & mask);
    }

    void compute() {
        const unsigned row = latched.wdata & 127;
        const unsigned op = (latched.wdata >> 8) & 3;
        for (unsigned i = 0; i < results.size(); ++i) {
            const auto w = weights[128 * i + row];
            const auto x = operands[i];
            switch (op) {
            case 0: results[i] = x * w; break;
            case 1: results[i] = x & w; break;
            case 2: results[i] = x | w; break;
            case 3: results[i] = x ^ w; break;
            }
        }
    }

    std::uint32_t readback() const {
        if (offset < command) return scalarQ;
        if (offset == status) return done;
        if (inBank(input)) return operands[lane()];
        if (inBank(result) && done) return results[lane()];
        return 0;
    }
};
