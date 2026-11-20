#include "cim_simulator.hpp"
#include "trace_simulator.hpp"

#include <functional>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
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
    require(simulator.statistics().operations().at(2).cells_written == 4 &&
                simulator.statistics().operations().at(4).cells_written == 4 &&
                simulator.statistics().operations().at(6).cells_written == 4,
            "logic result write counters mismatch");
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

std::vector<cpupim::TraceOperation> parseTestTrace(const std::string& text) {
    std::istringstream input(text);
    return cpupim::parseTrace(input, "test.trace");
}

const cpupim::TraceEvent& findEvent(const cpupim::TraceResult& result,
                                    const std::string& id) {
    for (const auto& event : result.events) {
        if (event.id == id) {
            return event;
        }
    }
    throw std::runtime_error("missing trace event: " + id);
}

void testTraceEndToEndSchedule() {
    const auto operations = parseTestTrace(
        "cpu prepare instructions=40\n"
        "cpu overlap instructions=32\n"
        "h2p input bytes=48 deps=prepare\n"
        "pim_load weights rows=4 channels=3 weight_bits=4 deps=input\n"
        "pim_vmm vmm rows=4 channels=3 weight_bits=4 activation_bits=4 deps=weights\n"
        "p2h result bytes=24 deps=vmm\n"
        "sync wait deps=result\n"
        "cpu consume instructions=8 deps=wait\n");
    cpupim::SystemConfig config;
    config.pim.max_active_rows = 2;
    config.pim.adc_count = 2;
    config.pim.write_ports = 4;
    const cpupim::TraceSimulator simulator(config);
    const auto result = simulator.run(operations);

    require(result.summary.total_cycles == 140,
            "end-to-end trace makespan mismatch");
    require(result.summary.cpu_busy_cycles == 20 &&
                result.summary.dma_busy_cycles == 9 &&
                result.summary.pim_busy_cycles == 119,
            "resource busy-cycle mismatch");
    require(result.summary.cpu_instructions == 80 &&
                result.summary.h2p_bytes == 48 &&
                result.summary.p2h_bytes == 24 &&
                result.summary.pim_adc_conversions == 96,
            "trace summary counter mismatch");
    require(result.summary.operation_count == 8,
            "trace operation count mismatch");

    const auto& overlap = findEvent(result, "overlap");
    require(overlap.start_cycle == 10 && overlap.end_cycle == 18,
            "independent CPU work did not overlap DMA/PIM work");
    const auto& load = findEvent(result, "weights");
    require(load.start_cycle == 15 && load.end_cycle == 28 &&
                load.cycles == 13 && load.cells_written == 48,
            "trace weight-load schedule mismatch");
    const auto& vmm = findEvent(result, "vmm");
    require(vmm.start_cycle == 28 && vmm.end_cycle == 134 &&
                vmm.adc_conversions == 96 && vmm.row_groups == 2,
            "trace VMM schedule mismatch");
    const auto& output = findEvent(result, "result");
    require(output.dependency_ready_cycle == 134 &&
                output.resource_ready_cycle == 15 &&
                output.start_cycle == 134 && output.end_cycle == 138,
            "DMA dependency/resource readiness mismatch");
    const auto& consume = findEvent(result, "consume");
    require(consume.start_cycle == 138 && consume.end_cycle == 140,
            "CPU synchronization mismatch");
}

void testTraceResourceOverlapAndAllPimOperations() {
    const auto operations = parseTestTrace(
        "cpu work cycles=7\n"
        "h2p send bytes=1\n"
        "pim_write write columns=8\n"
        "pim_logic xor columns=8 function=xor deps=write\n"
        "p2h receive bytes=1 deps=xor\n"
        "sync done deps=work,receive\n");
    cpupim::SystemConfig config;
    config.pim.write_ports = 4;
    const auto result = cpupim::TraceSimulator(config).run(operations);

    require(result.summary.total_cycles == 10,
            "independent resources were serialized incorrectly");
    require(findEvent(result, "work").start_cycle == 0 &&
                findEvent(result, "send").start_cycle == 0 &&
                findEvent(result, "write").start_cycle == 0,
            "independent operations did not start together");
    require(findEvent(result, "write").cycles == 3 &&
                findEvent(result, "xor").cycles == 4,
            "PIM write or XOR timing mismatch");
    require(result.summary.cpu_busy_cycles == 7 &&
                result.summary.dma_busy_cycles == 6 &&
                result.summary.pim_busy_cycles == 7,
            "overlap resource totals mismatch");
}

void testTraceForwardDependenciesAndFractionalIpc() {
    const auto operations = parseTestTrace(
        "cpu later instructions=10 stall_cycles=3 deps=first\n"
        "pim_logic first columns=4 function=and\n");
    cpupim::SystemConfig config;
    config.cpu_ipc = 2.5;
    const auto result = cpupim::TraceSimulator(config).run(operations);

    require(result.events.at(0).id == "first",
            "forward dependency was not topologically scheduled");
    require(findEvent(result, "first").end_cycle == 3 &&
                findEvent(result, "later").start_cycle == 3 &&
                findEvent(result, "later").cycles == 7 &&
                result.summary.total_cycles == 10,
            "forward dependency or fractional IPC timing mismatch");
}

void testTraceInOrderResourcePolicy() {
    const auto operations = parseTestTrace(
        "h2p slow bytes=1568\n"
        "pim_logic fast columns=1 function=and\n"
        "cpu late cycles=1 deps=slow\n"
        "cpu early cycles=1 deps=fast\n"
        "p2h early_result bytes=1 deps=early\n");
    const auto result = cpupim::TraceSimulator().run(operations);

    require(findEvent(result, "late").start_cycle == 100 &&
                findEvent(result, "late").end_cycle == 101 &&
                findEvent(result, "early").start_cycle == 101,
            "same-resource trace/topological order was not preserved");
    require(findEvent(result, "early_result").start_cycle == 102 &&
                result.summary.total_cycles == 105,
            "in-order resource reservation makespan mismatch");
}

void testTraceValidation() {
    requireThrows<std::invalid_argument>([] { parseTestTrace(""); },
                                         "empty trace was accepted");
    requireThrows<std::invalid_argument>(
        [] { parseTestTrace("cpu bad cycles=1 instructions=2\n"); },
        "CPU with two duration modes was accepted");
    requireThrows<std::invalid_argument>(
        [] { parseTestTrace("cpu bad cycles=1 imaginary=2\n"); },
        "unknown trace field was accepted");
    requireThrows<std::invalid_argument>(
        [] { parseTestTrace("pim_vmm bad rows=1 channels=1 weight_bits=4\n"); },
        "incomplete PIM VMM was accepted");
    requireThrows<std::invalid_argument>(
        [] { parseTestTrace("h2p bad bytes=0\n"); },
        "empty DMA transfer was accepted");
    requireThrows<std::invalid_argument>(
        [] { parseTestTrace("pim_logic bad columns=8 function=nand\n"); },
        "unknown logic function was accepted");
    requireThrows<std::invalid_argument>(
        [] { parseTestTrace("cpu same cycles=1\ncpu same cycles=2\n"); },
        "duplicate operation ID was accepted");

    const auto missing = parseTestTrace("cpu a cycles=1 deps=missing\n");
    requireThrows<std::invalid_argument>(
        [&] { cpupim::TraceSimulator().run(missing); },
        "missing dependency was accepted");
    const auto cycle = parseTestTrace(
        "cpu a cycles=1 deps=b\ncpu b cycles=1 deps=a\n");
    requireThrows<std::invalid_argument>(
        [&] { cpupim::TraceSimulator().run(cycle); },
        "dependency cycle was accepted");
    const auto self = parseTestTrace("cpu a cycles=1 deps=a\n");
    requireThrows<std::invalid_argument>(
        [&] { cpupim::TraceSimulator().run(self); },
        "self dependency was accepted");

    cpupim::SystemConfig bad_config;
    bad_config.dma_bytes_per_cycle = 0;
    requireThrows<std::invalid_argument>(
        [&] { cpupim::TraceSimulator simulator(bad_config); },
        "zero DMA bandwidth was accepted");

    std::vector<cpupim::TraceOperation> manual(1);
    manual[0].id = "manual";
    manual[0].kind = cpupim::OperationKind::Cpu;
    requireThrows<std::invalid_argument>(
        [&] { cpupim::TraceSimulator().run(manual); },
        "manual CPU operation without a duration was accepted");
    manual[0].kind = cpupim::OperationKind::H2P;
    requireThrows<std::invalid_argument>(
        [&] { cpupim::TraceSimulator().run(manual); },
        "manual DMA operation without bytes was accepted");
    manual[0].kind = cpupim::OperationKind::PimLogic;
    requireThrows<std::invalid_argument>(
        [&] { cpupim::TraceSimulator().run(manual); },
        "manual logic operation without columns was accepted");
}

void testTraceArithmeticOverflow() {
    const auto operations = parseTestTrace(
        "cpu huge cycles=18446744073709551615 stall_cycles=1\n");
    requireThrows<std::overflow_error>(
        [&] { cpupim::TraceSimulator().run(operations); },
        "trace cycle overflow was not detected");

    const auto maximum = parseTestTrace(
        "cpu maximum instructions=18446744073709551615\n");
    cpupim::SystemConfig config;
    config.cpu_ipc = 1.0;
    require(cpupim::TraceSimulator(config).run(maximum).summary.total_cycles ==
                std::numeric_limits<cpupim::Word>::max(),
            "maximum exact CPU cycle count was rejected");

    const auto precise = parseTestTrace(
        "cpu precise instructions=9007199254740993\n");
    require(cpupim::TraceSimulator().run(precise).summary.total_cycles ==
                2251799813685249ULL,
            "CPU instruction conversion lost precision above 2^53");

    config.cpu_ipc = 0.5;
    requireThrows<std::overflow_error>(
        [&] { cpupim::TraceSimulator(config).run(maximum); },
        "CPU issue-cycle overflow was not detected");
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
        testTraceEndToEndSchedule();
        testTraceResourceOverlapAndAllPimOperations();
        testTraceForwardDependenciesAndFractionalIpc();
        testTraceInOrderResourcePolicy();
        testTraceValidation();
        testTraceArithmeticOverflow();
        std::cout << "All tests passed (" << assertions << " assertions).\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}
