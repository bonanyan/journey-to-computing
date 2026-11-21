// Step 1. Drive the CIM array directly from a numeric workload.
#include "cim_array.hpp"
#include "timing.hpp"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

static std::vector<std::int8_t> readValues(std::istream& in, std::size_t count) {
    std::vector<std::int8_t> values(count);
    for (auto& value : values) {
        int n;
        if (!(in >> n) || n < -128 || n > 127)
            throw std::runtime_error("expected signed 8-bit decimal value");
        value = static_cast<std::int8_t>(n);
    }
    return values;
}

class StandaloneSimulator {
    TimingModel timing_;
    static unsigned dimension(std::istream& in, const char* name) {
        int n;
        if (!(in >> n) || n < 1 || n > 65536)
            throw std::runtime_error(std::string(name) + " must be in 1..65536");
        return static_cast<unsigned>(n);
    }
public:
    explicit StandaloneSimulator(TimingModel timing = {}) : timing_(timing) {
        if (!timing_.tileRows || !timing_.tileCols || !timing_.busBytes)
            throw std::invalid_argument("tile and bus dimensions must be positive");
    }
    void run(std::istream& in, std::ostream& out) const {
        const auto r = dimension(in, "rows");
        const auto c = dimension(in, "columns");
        const auto count = dimension(in, "vector count");
        if (Cycles(r) * c > (1u << 26)) throw std::runtime_error("matrix exceeds 64 MiB");
        CimArray array(r, c, readValues(in, std::size_t(r) * c));
        Cycles elapsed = timing_.program(r, c);
        out << "array=" << r << 'x' << c << " tile=" << timing_.tileRows << 'x'
            << timing_.tileCols << " bus_bytes=" << timing_.busBytes
            << "\nprogram_cycles=" << elapsed << '\n';
        const auto load = timing_.input(r), compute = timing_.compute(r, c),
                   read = timing_.readout(c), latency = load + compute + read;
        for (unsigned i = 0; i < count; ++i) {
            const auto y = array.multiply(readValues(in, r));
            out << "vector[" << i << "] output:";
            for (auto v : y) out << ' ' << v;
            out << " | input=" << load << " compute=" << compute << " readout=" << read
                << " latency=" << latency << " cycles\n";
            elapsed += latency;
        }
        std::string extra;
        if (in >> extra) throw std::runtime_error("unexpected trailing input: " + extra);
        out << "total_cycles=" << elapsed << '\n';
    }
};

int main(int argc, char** argv) {
    try {
        if (argc > 2 || (argc == 2 && std::string(argv[1]) == "--help")) {
            std::cout << "Usage: cim [workload.txt|-]\nNo arguments: run built-in example. -: read stdin.\n"
                         "Format (whitespace separated): rows columns vector_count, then row-major\n"
                         "signed int8 weights, then vector_count signed int8 vectors of length rows.\n"
                         "Output per vector: y[c] = sum_r x[r] * W[r,c]. All phases are serial.\n";
            return argc > 2 ? 1 : 0;
        }
        StandaloneSimulator simulator;
        if (argc == 1) {
            std::istringstream demo("3 2 2\n1 -2\n3 4\n-5 6\n2 0 -1\n-1 2 3\n");
            simulator.run(demo, std::cout);
        } else if (std::string(argv[1]) == "-") {
            simulator.run(std::cin, std::cout);
        } else {
            std::ifstream file(argv[1]);
            if (!file) throw std::runtime_error("cannot open workload file");
            simulator.run(file, std::cout);
        }
    } catch (const std::exception& e) {
        std::cerr << "cim: " << e.what() << '\n';
        return 1;
    }
}
