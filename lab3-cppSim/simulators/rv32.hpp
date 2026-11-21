// Instruction-level model of the RV32IMC instructions used by Lab 2.
#pragma once

#include "lab2_system.hpp"

#include <sstream>

class Rv32 {
public:
    std::uint32_t pc = 0x180;  // _start in X-HEEP's SRAM linker script
    std::uint64_t retired = 0;

    explicit Rv32(Lab2System& system) : bus(system) {}

    void step() {
        std::uint32_t insn = bus.fetch16(pc);
        if ((insn & 3) == 3) {
            insn |= bus.fetch16(pc + 2) << 16;
            pc = execute(insn);
        } else {
            pc = compressed(insn);
        }
        regs[0] = 0;
        ++retired;
    }

private:
    using Word = std::uint32_t;
    Lab2System& bus;
    std::array<Word, 32> regs{};

    static Word bits(Word value, unsigned high, unsigned low) {
        return (value >> low) & ((1u << (high - low + 1)) - 1);
    }

    static Word signExtend(Word value, unsigned width) {
        const Word sign = 1u << (width - 1);
        return (value ^ sign) - sign;
    }

    [[noreturn]] static void unsupported(Word insn) {
        std::ostringstream msg;
        msg << "unsupported instruction 0x" << std::hex << insn;
        throw std::runtime_error(msg.str());
    }

    Word execute(Word insn) {
        const unsigned rd = bits(insn, 11, 7), func = bits(insn, 14, 12);
        const Word a = regs[bits(insn, 19, 15)], b = regs[bits(insn, 24, 20)];
        const Word imm = signExtend(insn >> 20, 12);
        const Word next = pc + 4;
        switch (insn & 0x7f) {
        case 0x37: regs[rd] = insn & 0xfffff000; break;                 // LUI
        case 0x17: regs[rd] = pc + (insn & 0xfffff000); break;          // AUIPC
        case 0x67:                                                     // JALR
            if (func != 0) unsupported(insn);
            regs[rd] = next;
            return (a + imm) & ~1u;
        case 0x6f: {                                                   // JAL
            const Word offset = (bits(insn, 31, 31) << 20) | (bits(insn, 19, 12) << 12)
                              | (bits(insn, 20, 20) << 11) | (bits(insn, 30, 21) << 1);
            regs[rd] = next;
            return pc + signExtend(offset, 21);
        }
        case 0x63: {                                                   // Branch
            const Word offset = (bits(insn, 31, 31) << 12) | (bits(insn, 7, 7) << 11)
                              | (bits(insn, 30, 25) << 5) | (bits(insn, 11, 8) << 1);
            bool take;
            switch (func) {
            case 0: take = a == b; break;
            case 1: take = a != b; break;
            case 6: take = a < b; break;
            case 7: take = a >= b; break;
            default: unsupported(insn);
            }
            return take ? pc + signExtend(offset, 13) : next;
        }
        case 0x03: {                                                   // LB/LH/LW/LBU/LHU
            if (func > 5 || func == 3) unsupported(insn);
            const unsigned bytes = 1u << (func & 3);
            const Word value = bus.access(a + imm, bytes, false);
            regs[rd] = func < 2 ? signExtend(value, 8 * bytes) : value;
            break;
        }
        case 0x23: {                                                   // SB/SH/SW
            if (func > 2) unsupported(insn);
            const Word offset = (bits(insn, 31, 25) << 5) | bits(insn, 11, 7);
            bus.access(a + signExtend(offset, 12), 1u << func, true, b);
            break;
        }
        case 0x13:
            switch (func) {
            case 0: regs[rd] = a + imm; break;
            case 1:
                if ((insn >> 25) != 0) unsupported(insn);
                regs[rd] = a << bits(insn, 24, 20);
                break;
            case 6: regs[rd] = a | imm; break;
            case 7: regs[rd] = a & imm; break;
            default: unsupported(insn);
            }
            break;
        case 0x33:
            switch (((insn >> 25) << 3) | func) {
            case 0x000: regs[rd] = a + b; break;
            case 0x100: regs[rd] = a - b; break;
            case 0x008: regs[rd] = a * b; break;
            case 0x004: regs[rd] = a ^ b; break;
            default: unsupported(insn);
            }
            break;
        default: unsupported(insn);
        }
        return next;
    }

    Word compressed(Word insn) {
        const Word next = pc + 2;
        const unsigned quadrant = insn & 3, func = insn >> 13;
        const unsigned rd = bits(insn, 11, 7), rs2 = bits(insn, 6, 2);
        // Three-bit compressed register fields address x8 through x15.
        const unsigned compactRd = 8 + bits(insn, 9, 7), compactRs2 = 8 + bits(insn, 4, 2);
        const Word imm = signExtend((bits(insn, 12, 12) << 5) | rs2, 6);
        if (quadrant == 0 && (func == 2 || func == 6)) {               // C.LW / C.SW
            const Word offset = (bits(insn, 5, 5) << 6) | (bits(insn, 12, 10) << 3)
                              | (bits(insn, 6, 6) << 2);
            const Word addr = regs[compactRd] + offset;
            if (func == 2) regs[compactRs2] = bus.access(addr, 4, false);
            else bus.access(addr, 4, true, regs[compactRs2]);
            return next;
        }
        if (quadrant == 2 && func == 4) {
            const bool highBit = bits(insn, 12, 12);
            if (rs2 == 0) {                                            // C.JR / C.JALR
                if (rd == 0) unsupported(insn);
                const Word target = regs[rd] & ~1u;
                if (highBit) regs[1] = next;
                return target;
            }
            regs[rd] = highBit ? regs[rd] + regs[rs2] : regs[rs2];     // C.ADD / C.MV
            return next;
        }
        if (quadrant != 1) unsupported(insn);
        switch (func) {
        case 0: regs[rd] += imm; break;                                // C.ADDI
        case 2: regs[rd] = imm; break;                                 // C.LI
        case 3:                                                        // C.LUI
            if (rd == 0 || rd == 2 || imm == 0) unsupported(insn);
            regs[rd] = imm << 12;
            break;
        case 4:
            if (bits(insn, 11, 10) == 2) {                             // C.ANDI
                regs[compactRd] &= imm;
            } else if (bits(insn, 12, 10) == 3) {
                switch (bits(insn, 6, 5)) {
                case 0: regs[compactRd] -= regs[compactRs2]; break;
                case 1: regs[compactRd] ^= regs[compactRs2]; break;
                case 2: regs[compactRd] |= regs[compactRs2]; break;
                case 3: regs[compactRd] &= regs[compactRs2]; break;
                }
            } else unsupported(insn);
            break;
        case 1: case 5: {                                               // C.JAL / C.J
            const Word offset = (bits(insn, 12, 12) << 11) | (bits(insn, 8, 8) << 10)
                              | (bits(insn, 10, 9) << 8) | (bits(insn, 6, 6) << 7)
                              | (bits(insn, 7, 7) << 6) | (bits(insn, 2, 2) << 5)
                              | (bits(insn, 11, 11) << 4) | (bits(insn, 5, 3) << 1);
            if (func == 1) regs[1] = next;
            return pc + signExtend(offset, 12);
        }
        case 6: case 7: {                                               // C.BEQZ / C.BNEZ
            const Word offset = (bits(insn, 12, 12) << 8) | (bits(insn, 6, 5) << 6)
                              | (bits(insn, 2, 2) << 5) | (bits(insn, 11, 10) << 3)
                              | (bits(insn, 4, 3) << 1);
            const bool take = (regs[compactRd] == 0) == (func == 6);
            return take ? pc + signExtend(offset, 9) : next;
        }
        default: unsupported(insn);
        }
        return next;
    }
};
