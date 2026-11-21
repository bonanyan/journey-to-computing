// The part of the X-HEEP address map used by the Lab 2 firmware.
#pragma once

#include "dpim.hpp"

#include <charconv>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

class Lab2System {
public:
    Dpim dpim;
    bool exitValid = false;
    std::uint32_t exitValue = 0;

    explicit Lab2System(std::ostream* trace) : trace_(trace) {}

    // Lab 2 elf2hex.py emits @byte_address followed by little-endian bytes.
    void loadImage(const std::string& path) {
        std::ifstream file(path);
        if (!file) throw std::runtime_error("cannot open " + path);
        std::uint32_t address = 0;
        for (std::string token; file >> token;) {
            if (token.front() == '@') {
                address = hex(std::string_view(token).substr(1));
            } else {
                const auto value = hex(token);
                if (value > 255 || address >= sram.size())
                    throw std::runtime_error("firmware byte outside SRAM or byte range");
                sram[address++] = static_cast<std::uint8_t>(value);
            }
        }
        if (file.bad()) throw std::runtime_error("error reading " + path);
    }

    std::uint32_t fetch16(std::uint32_t pc) const {
        if ((pc & 1) || pc >= sram.size())
            throw std::runtime_error("invalid instruction address");
        return sram[pc] | (static_cast<std::uint32_t>(sram[pc + 1]) << 8);
    }

    // CPU accesses are little-endian and naturally aligned in the lab firmware.
    std::uint32_t access(std::uint32_t addr, unsigned bytes, bool write, std::uint32_t value = 0) {
        if (addr & (bytes - 1)) throw std::runtime_error("unaligned data access");
        if (addr < sram.size()) {
            std::uint32_t word = 0;
            for (unsigned i = 0; i < bytes; ++i) {
                if (write) sram[addr + i] = static_cast<std::uint8_t>(value >> (8 * i));
                else word |= static_cast<std::uint32_t>(sram[addr + i]) << (8 * i);
            }
            return word;
        }
        if (addr >= Dpim::base && addr - Dpim::base < Dpim::window) {
            const unsigned lane = addr & 3;
            const unsigned shift = 8 * lane;
            const unsigned be = ((1u << bytes) - 1) << lane;
            const auto wdata = shift ? (value << shift) | (value >> (32 - shift)) : value;
            const ObiRequest request{addr & ~3u, wdata, be, write};
            const auto word = transfer(request) >> shift;
            return bytes == 4 ? word : word & ((1u << (8 * bytes)) - 1);
        }
        if (write && bytes == 4 && addr == 0x20000004) exitValue = value;
        else if (write && bytes == 4 && addr == 0x20000000) exitValid = (value & 1) != 0;
        else throw std::runtime_error("unmapped data access at " + std::to_string(addr));
        return 0;
    }

private:
    std::vector<std::uint8_t> sram = std::vector<std::uint8_t>(128 * 1024);
    std::ostream* trace_;

    static std::uint32_t hex(std::string_view token) {
        if (token.empty()) throw std::runtime_error("missing hex address");
        std::uint32_t value;
        const auto end = token.data() + token.size();
        auto parsed = std::from_chars(token.data(), end, value, 16);
        if (parsed.ec != std::errc{} || parsed.ptr != end)
            throw std::runtime_error("invalid firmware token: " + std::string(token));
        return value;
    }

    std::uint32_t transfer(const ObiRequest& request) {
        while (!dpim.tick(&request, trace_).grant) {}
        const auto grant = dpim.edges;
        ObiResponse response{};
        do {
            response = dpim.tick(nullptr, trace_);
        } while (!response.valid);
        if (trace_) {
            *trace_ << (request.write ? "store" : "load") << " addr=0x" << std::hex << request.addr
                    << " be=0x" << request.be << " data=0x"
                    << (request.write ? request.wdata : response.data) << std::dec
                    << " grant=" << grant << " response=" << dpim.edges << '\n';
        }
        return response.data;
    }
};
