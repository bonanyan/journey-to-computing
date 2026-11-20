#ifndef PIM_TIMING_HPP
#define PIM_TIMING_HPP

#include <cstdint>

using Cycles = std::uint64_t;

// Tiles execute sequentially. Columns within a tile operate in parallel.
struct TimingModel {
    unsigned tileRows = 64, tileCols = 32, busBytes = 16;

    static Cycles groups(Cycles n, unsigned width) { return (n + width - 1) / width; }
    Cycles beats(Cycles bytes) const { return groups(bytes, busBytes); }

    // Each logical weight row starts a fresh bus transfer.
    Cycles program(Cycles rows, Cycles cols) const { return rows * beats(cols); }
    Cycles input(Cycles rows) const { return beats(rows); }
    Cycles compute(Cycles rows, Cycles cols) const {
        // Eight activation-bit reductions followed by one merge per tile.
        return groups(rows, tileRows) * groups(cols, tileCols) * 9;
    }
    Cycles readout(Cycles cols) const { return beats(cols * sizeof(std::int64_t)); }
};

#endif
