// Step 2. Execute Lab 2 firmware against the C++ model of its DPIM peripheral.
#include "rv32.hpp"

#include <iostream>

int main(int argc, char** argv) {
    if ((argc != 2 && argc != 3) || (argc == 3 && std::string_view(argv[2]) != "--trace")) {
        std::cerr << "usage: cosim firmware.hex [--trace]\n";
        return 2;
    }
    Lab2System system(argc == 3 ? &std::cout : nullptr);
    Rv32 cpu(system);
    try {
        system.loadImage(argv[1]);
        while (!system.exitValid && cpu.retired < 1000000) cpu.step();
        if (!system.exitValid) throw std::runtime_error("instruction limit reached without EXIT_VALID");
        std::cout << (system.exitValue ? "FAIL " : "PASS ") << argv[1]
                  << " exit_value=" << system.exitValue << " instructions=" << cpu.retired
                  << " dpim_transactions=" << system.dpim.transactions
                  << " commands=" << system.dpim.commands
                  << " dpim_bus_edges=" << system.dpim.edges << '\n';
        return system.exitValue ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << argv[1] << " at pc=0x" << std::hex << cpu.pc << ": " << error.what() << '\n';
        return 2;
    }
}
