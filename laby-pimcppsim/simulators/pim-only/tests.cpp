#include "cim_simulator.hpp"

#include <functional>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::size_t assertions = 0;

void require(bool condition, const std::string& message) {
    ++assertions;
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Exception, typename Function>
void requireThrows(Function&& function, const std::string& message) {
    ++assertions;
    try {
        function();
    } catch (const Exception&) {
        return;
    }
    throw std::runtime_error(message);
}

void testVmmFunctionAndTiming() {
    cim::TimingConfig timing;
    timing.max_active_rows = 2;
    timing.adc_count = 2;
    timing.write_ports = 4;
    cim::Simulator simulator(timing);
    cim::VmmArray array(simulator, {4, 12, 4});
    array.loadWeights({{1, 2, 3}, {4, 5, 6}, {7, 8, 9}, {10, 11, 12}});
    const auto output = array.multiply({1, 2, 3, 4}, 4);

    require(output == std::vector<cim::Word>({70, 80, 90}),
            "VMM functional result mismatch");
    require(simulator.statistics().totalCycles() == 119,
            "VMM latency mismatch");
    require(simulator.statistics().operations().at(0).cycles == 13,
            "weight-load latency mismatch");
    require(simulator.statistics().operations().at(1).cycles == 106,
            "VMM operation latency mismatch");
    require(simulator.statistics().operations().at(1).adc_conversions == 96,
            "ADC conversion count mismatch");
}

void testBitPlaneMapping() {
    cim::Simulator simulator;
    cim::VmmArray array(simulator, {1, 8, 4});
    array.loadWeights({{10}});
    require(array.readCell(0, 0) == 0 && array.readCell(0, 1) == 1 &&
                array.readCell(0, 2) == 0 && array.readCell(0, 3) == 1,
            "weight bit-plane mapping mismatch");
    require(array.multiply({3}, 2).at(0) == 30,
            "single-cell multiplication mismatch");
}

void testLogicOperationsAndAliasing() {
    cim::TimingConfig timing;
    timing.write_ports = 4;
    cim::Simulator simulator(timing);
    cim::LogicArray array(simulator, 2, 4);
    array.writeRow(0, {1, 0, 1, 0});
    array.writeRow(1, {1, 1, 0, 0});

    require(array.apply(cim::LogicOperation::And, 0, 0, 1) ==
                cim::BitRow({1, 0, 0, 0}),
            "AND result or destination aliasing mismatch");
    array.writeRow(0, {1, 0, 1, 0});
    require(array.apply(cim::LogicOperation::Or, 0, 0, 1) ==
                cim::BitRow({1, 1, 1, 0}),
            "OR result mismatch");
    array.writeRow(0, {1, 0, 1, 0});
    require(array.apply(cim::LogicOperation::Xor, 0, 0, 1) ==
                cim::BitRow({0, 1, 1, 0}),
            "XOR result mismatch");
    require(simulator.statistics().totalCycles() == 18,
            "logic timing mismatch");
}

void testAgainstScalarReference() {
    std::mt19937_64 generator(20261120);
    for (std::size_t trial = 0; trial < 100; ++trial) {
        const std::size_t rows = 1 + trial % 7;
        const std::size_t outputs = 1 + (trial * 3) % 5;
        const std::size_t weight_bits = 1 + trial % 6;
        const std::size_t activation_bits = 1 + (trial * 5) % 6;
        const cim::Word weight_mask = (cim::Word{1} << weight_bits) - 1;
        const cim::Word activation_mask = (cim::Word{1} << activation_bits) - 1;

        cim::Matrix weights(rows, std::vector<cim::Word>(outputs));
        std::vector<cim::Word> activations(rows);
        std::vector<cim::Word> expected(outputs, 0);
        for (std::size_t row = 0; row < rows; ++row) {
            activations[row] = generator() & activation_mask;
            for (std::size_t output = 0; output < outputs; ++output) {
                weights[row][output] = generator() & weight_mask;
                expected[output] += weights[row][output] * activations[row];
            }
        }

        cim::TimingConfig timing;
        timing.max_active_rows = 1 + trial % 4;
        timing.adc_count = 1 + (trial * 7) % 4;
        timing.write_ports = 1 + (trial * 11) % 8;
        cim::Simulator simulator(timing);
        cim::VmmArray array(simulator,
                            {rows, outputs * weight_bits, weight_bits});
        array.loadWeights(weights);
        const auto actual = array.multiply(activations, activation_bits);
        require(actual == expected, "randomized scalar reference mismatch");

        const cim::Word planes = outputs * weight_bits;
        const cim::Word groups = rows / timing.max_active_rows +
            static_cast<cim::Word>(rows % timing.max_active_rows != 0);
        const cim::Word batches = planes / timing.adc_count +
            static_cast<cim::Word>(planes % timing.adc_count != 0);
        const cim::Word load_cycles = timing.write_setup_cycles +
            (rows * planes) / timing.write_ports +
            static_cast<cim::Word>((rows * planes) % timing.write_ports != 0);
        const cim::Word vmm_cycles = timing.vmm_setup_cycles +
            activation_bits * groups *
                (batches * timing.adc_cycles + timing.accumulate_cycles);
        require(simulator.statistics().totalCycles() == load_cycles + vmm_cycles,
                "randomized timing reference mismatch");
    }
}

void testZeroAndMaximumValues() {
    cim::Simulator simulator;
    cim::VmmArray array(simulator, {2, 8, 8});
    array.loadWeights({{255}, {1}});
    require(array.multiply({0, 255}, 8).at(0) == 255,
            "zero or maximum input handling mismatch");
}

void testValidationAndOverflow() {
    cim::Simulator simulator;
    cim::VmmArray array(simulator, {1, 4, 4});
    requireThrows<std::logic_error>([&] { array.multiply({1}, 1); },
                                    "missing weight-load validation");
    array.loadWeights({{15}});
    requireThrows<std::invalid_argument>([&] { array.multiply({2}, 1); },
                                         "activation width validation missing");
    requireThrows<std::invalid_argument>([&] { array.multiply({1, 2}, 2); },
                                         "activation length validation missing");
    requireThrows<std::invalid_argument>([&] { array.loadWeights({{16}}); },
                                         "weight width validation missing");

    cim::Simulator wide_simulator;
    cim::VmmArray wide_array(wide_simulator, {1, 64, 64});
    wide_array.loadWeights({{std::numeric_limits<cim::Word>::max()}});
    requireThrows<std::overflow_error>(
        [&] {
            wide_array.multiply({std::numeric_limits<cim::Word>::max()}, 64);
        },
        "64-bit result overflow was not detected");
}

void testStatisticsClear() {
    cim::Simulator simulator;
    cim::LogicArray array(simulator, 1, 1);
    array.writeRow(0, {1});
    require(simulator.statistics().totalCycles() == 2,
            "unexpected row-write timing");
    simulator.statistics().clear();
    require(simulator.statistics().operations().empty() &&
                simulator.statistics().totalCycles() == 0,
            "statistics reset mismatch");
}

}  // namespace

int main() {
    try {
        testVmmFunctionAndTiming();
        testBitPlaneMapping();
        testLogicOperationsAndAliasing();
        testAgainstScalarReference();
        testZeroAndMaximumValues();
        testValidationAndOverflow();
        testStatisticsClear();
        std::cout << "All tests passed (" << assertions << " assertions).\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}
