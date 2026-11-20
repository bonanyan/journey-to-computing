#include "trace_simulator.hpp"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Options {
    std::string trace_path = "examples/demo.trace";
    std::string output_prefix = "results/cpu_pim";
    cpupim::SystemConfig config;
};

void printUsage(const char* executable) {
    std::cout
        << "Usage: " << executable << " [options]\n"
        << "Simulate a dependency-aware CPU/DMA/PIM operation trace.\n\n"
        << "  --trace FILE                 Input trace (default: examples/demo.trace)\n"
        << "  --output-prefix PREFIX       Report prefix (default: results/cpu_pim)\n"
        << "  --cpu-ipc N                  CPU instructions per cycle (default: 4)\n"
        << "  --dma-bytes-per-cycle N      DMA bandwidth (default: 16)\n"
        << "  --dma-setup-cycles N         Cycles per DMA transfer (default: 2)\n"
        << "  --max-active-rows N          PIM rows sensed per group (default: 64)\n"
        << "  --adc-count N                Parallel ideal ADCs (default: 8)\n"
        << "  --adc-cycles N               Cycles per ADC batch (default: 2)\n"
        << "  --accumulate-cycles N        Digital accumulation per group (default: 1)\n"
        << "  --write-ports N              Parallel PIM write ports (default: 32)\n"
        << "  --write-setup-cycles N       PIM write setup cycles (default: 1)\n"
        << "  --vmm-setup-cycles N         VMM setup cycles (default: 2)\n"
        << "  --logic-setup-cycles N       Logic setup cycles (default: 1)\n"
        << "  --boolean-sense-cycles N     AND/OR sense cycles (default: 1)\n"
        << "  --xor-sense-cycles N         XOR sense cycles (default: 2)\n"
        << "  --logic-write-cycles N       Logic result write cycles (default: 1)\n"
        << "  --help                       Show this help\n\n"
        << "Trace records: OP ID [key=value ...]\n"
        << "  cpu ID cycles=N|instructions=N [stall_cycles=N] [deps=a,b]\n"
        << "  sync ID [deps=a,b]\n"
        << "  h2p|p2h ID bytes=N [deps=a,b]\n"
        << "  pim_load ID rows=R channels=C weight_bits=B [deps=a,b]\n"
        << "  pim_vmm ID rows=R channels=C weight_bits=B activation_bits=A [deps=a,b]\n"
        << "  pim_write ID columns=N [deps=a,b]\n"
        << "  pim_logic ID columns=N function=and|or|xor [deps=a,b]\n";
}

cpupim::Word parseUnsigned(const std::string& text,
                           const std::string& description) {
    if (text.empty() || text.front() == '-') {
        throw std::invalid_argument("invalid " + description + ": " + text);
    }
    std::size_t consumed = 0;
    unsigned long long value = 0;
    try {
        value = std::stoull(text, &consumed, 0);
    } catch (const std::exception&) {
        throw std::invalid_argument("invalid " + description + ": " + text);
    }
    if (consumed != text.size()) {
        throw std::invalid_argument("invalid " + description + ": " + text);
    }
    return static_cast<cpupim::Word>(value);
}

std::size_t parseSize(const std::string& text, const std::string& description) {
    const cpupim::Word value = parseUnsigned(text, description);
    if (value > std::numeric_limits<std::size_t>::max()) {
        throw std::invalid_argument(description + " is too large");
    }
    return static_cast<std::size_t>(value);
}

double parsePositiveDouble(const std::string& text,
                           const std::string& description) {
    std::size_t consumed = 0;
    double value = 0.0;
    try {
        value = std::stod(text, &consumed);
    } catch (const std::exception&) {
        throw std::invalid_argument("invalid " + description + ": " + text);
    }
    if (consumed != text.size() || !std::isfinite(value) || value <= 0.0) {
        throw std::invalid_argument("invalid " + description + ": " + text);
    }
    return value;
}

std::string requireValue(int argc, char** argv, int& index) {
    if (index + 1 >= argc) {
        throw std::invalid_argument(std::string("missing value after ") + argv[index]);
    }
    return argv[++index];
}

Options parseOptions(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help") {
            printUsage(argv[0]);
            std::exit(0);
        } else if (argument == "--trace") {
            options.trace_path = requireValue(argc, argv, index);
        } else if (argument == "--output-prefix") {
            options.output_prefix = requireValue(argc, argv, index);
        } else if (argument == "--cpu-ipc") {
            options.config.cpu_ipc = parsePositiveDouble(
                requireValue(argc, argv, index), "CPU IPC");
        } else if (argument == "--dma-bytes-per-cycle") {
            options.config.dma_bytes_per_cycle = parseSize(
                requireValue(argc, argv, index), "DMA bytes per cycle");
        } else if (argument == "--dma-setup-cycles") {
            options.config.dma_setup_cycles = parseUnsigned(
                requireValue(argc, argv, index), "DMA setup cycles");
        } else if (argument == "--max-active-rows") {
            options.config.pim.max_active_rows = parseSize(
                requireValue(argc, argv, index), "maximum active row count");
        } else if (argument == "--adc-count") {
            options.config.pim.adc_count = parseSize(
                requireValue(argc, argv, index), "ADC count");
        } else if (argument == "--adc-cycles") {
            options.config.pim.adc_cycles = parseUnsigned(
                requireValue(argc, argv, index), "ADC cycle count");
        } else if (argument == "--accumulate-cycles") {
            options.config.pim.accumulate_cycles = parseUnsigned(
                requireValue(argc, argv, index), "accumulation cycle count");
        } else if (argument == "--write-ports") {
            options.config.pim.write_ports = parseSize(
                requireValue(argc, argv, index), "write port count");
        } else if (argument == "--write-setup-cycles") {
            options.config.pim.write_setup_cycles = parseUnsigned(
                requireValue(argc, argv, index), "write setup cycle count");
        } else if (argument == "--vmm-setup-cycles") {
            options.config.pim.vmm_setup_cycles = parseUnsigned(
                requireValue(argc, argv, index), "VMM setup cycle count");
        } else if (argument == "--logic-setup-cycles") {
            options.config.pim.logic_setup_cycles = parseUnsigned(
                requireValue(argc, argv, index), "logic setup cycle count");
        } else if (argument == "--boolean-sense-cycles") {
            options.config.pim.boolean_sense_cycles = parseUnsigned(
                requireValue(argc, argv, index), "Boolean sense cycle count");
        } else if (argument == "--xor-sense-cycles") {
            options.config.pim.xor_sense_cycles = parseUnsigned(
                requireValue(argc, argv, index), "XOR sense cycle count");
        } else if (argument == "--logic-write-cycles") {
            options.config.pim.logic_write_cycles = parseUnsigned(
                requireValue(argc, argv, index), "logic write cycle count");
        } else {
            throw std::invalid_argument("unknown option: " + argument);
        }
    }
    if (options.trace_path.empty()) {
        throw std::invalid_argument("trace path must not be empty");
    }
    if (options.output_prefix.empty()) {
        throw std::invalid_argument("output prefix must not be empty");
    }
    return options;
}

std::string jsonEscape(const std::string& text) {
    std::ostringstream escaped;
    for (const unsigned char character : text) {
        switch (character) {
            case '"': escaped << "\\\""; break;
            case '\\': escaped << "\\\\"; break;
            case '\b': escaped << "\\b"; break;
            case '\f': escaped << "\\f"; break;
            case '\n': escaped << "\\n"; break;
            case '\r': escaped << "\\r"; break;
            case '\t': escaped << "\\t"; break;
            default:
                if (character < 0x20) {
                    escaped << "\\u00" << std::hex << std::setw(2)
                            << std::setfill('0') << static_cast<unsigned>(character)
                            << std::dec << std::setfill(' ');
                } else {
                    escaped << static_cast<char>(character);
                }
        }
    }
    return escaped.str();
}

std::string csvEscape(const std::string& text) {
    if (text.find_first_of(",\"\r\n") == std::string::npos) {
        return text;
    }
    std::string escaped = "\"";
    for (const char character : text) {
        if (character == '"') {
            escaped += "\"\"";
        } else {
            escaped += character;
        }
    }
    escaped += '"';
    return escaped;
}

std::string joinDependencies(const std::vector<std::string>& dependencies,
                             const std::string& separator) {
    std::string joined;
    for (std::size_t index = 0; index < dependencies.size(); ++index) {
        if (index != 0) {
            joined += separator;
        }
        joined += dependencies[index];
    }
    return joined;
}

void writeJsonReport(const std::string& path,
                     const cpupim::SystemConfig& config,
                     const cpupim::TraceResult& result) {
    std::ofstream report(path);
    if (!report) {
        throw std::runtime_error("cannot write report: " + path);
    }
    const auto& summary = result.summary;
    report << std::setprecision(std::numeric_limits<double>::max_digits10)
           << "{\n"
           << "  \"model\": \"trace_cpu_pim_v1\",\n"
           << "  \"configuration\": {\n"
           << "    \"cpu_ipc\": " << config.cpu_ipc << ",\n"
           << "    \"dma_bytes_per_cycle\": " << config.dma_bytes_per_cycle << ",\n"
           << "    \"dma_setup_cycles\": " << config.dma_setup_cycles << ",\n"
           << "    \"pim_write_ports\": " << config.pim.write_ports << ",\n"
           << "    \"pim_max_active_rows\": " << config.pim.max_active_rows << ",\n"
           << "    \"pim_adc_count\": " << config.pim.adc_count << ",\n"
           << "    \"pim_adc_cycles\": " << config.pim.adc_cycles << ",\n"
           << "    \"pim_accumulate_cycles\": " << config.pim.accumulate_cycles << ",\n"
           << "    \"pim_write_setup_cycles\": " << config.pim.write_setup_cycles << ",\n"
           << "    \"pim_vmm_setup_cycles\": " << config.pim.vmm_setup_cycles << ",\n"
           << "    \"pim_logic_setup_cycles\": " << config.pim.logic_setup_cycles << ",\n"
           << "    \"pim_boolean_sense_cycles\": " << config.pim.boolean_sense_cycles << ",\n"
           << "    \"pim_xor_sense_cycles\": " << config.pim.xor_sense_cycles << ",\n"
           << "    \"pim_logic_write_cycles\": " << config.pim.logic_write_cycles << "\n"
           << "  },\n"
           << "  \"summary\": {\n"
           << "    \"total_cycles\": " << summary.total_cycles << ",\n"
           << "    \"operation_count\": " << summary.operation_count << ",\n"
           << "    \"cpu_busy_cycles\": " << summary.cpu_busy_cycles << ",\n"
           << "    \"dma_busy_cycles\": " << summary.dma_busy_cycles << ",\n"
           << "    \"pim_busy_cycles\": " << summary.pim_busy_cycles << ",\n"
           << "    \"cpu_instructions\": " << summary.cpu_instructions << ",\n"
           << "    \"h2p_bytes\": " << summary.h2p_bytes << ",\n"
           << "    \"p2h_bytes\": " << summary.p2h_bytes << ",\n"
           << "    \"pim_adc_conversions\": " << summary.pim_adc_conversions << "\n"
           << "  },\n"
           << "  \"operations\": [\n";

    for (std::size_t index = 0; index < result.events.size(); ++index) {
        const auto& event = result.events[index];
        report << "    {\"sequence\": " << event.sequence
               << ", \"id\": \"" << jsonEscape(event.id)
               << "\", \"operation\": \"" << cpupim::operationName(event.kind)
               << "\", \"resource\": \"" << cpupim::resourceName(event.resource)
               << "\", \"source_line\": " << event.source_line
               << ", \"dependencies\": [";
        for (std::size_t dependency = 0;
             dependency < event.dependencies.size(); ++dependency) {
            if (dependency != 0) {
                report << ", ";
            }
            report << "\"" << jsonEscape(event.dependencies[dependency]) << "\"";
        }
        report << "], \"start_cycle\": " << event.start_cycle
               << ", \"end_cycle\": " << event.end_cycle
               << ", \"cycles\": " << event.cycles
               << ", \"dependency_ready_cycle\": " << event.dependency_ready_cycle
               << ", \"resource_ready_cycle\": " << event.resource_ready_cycle
               << ", \"instructions\": " << event.instructions
               << ", \"bytes\": " << event.bytes
               << ", \"cells_written\": " << event.cells_written
               << ", \"adc_conversions\": " << event.adc_conversions
               << ", \"row_groups\": " << event.row_groups
               << ", \"input_bits\": " << event.input_bits;
        if (event.kind == cpupim::OperationKind::PimLogic) {
            report << ", \"logic_function\": \""
                   << cpupim::logicOperationName(event.logic_operation) << "\"";
        }
        report << '}';
        if (index + 1 != result.events.size()) {
            report << ',';
        }
        report << '\n';
    }
    report << "  ]\n}\n";
    if (!report) {
        throw std::runtime_error("failed while writing report: " + path);
    }
}

void writeSummaryCsv(const std::string& path, const cpupim::TraceSummary& summary) {
    std::ofstream report(path);
    if (!report) {
        throw std::runtime_error("cannot write report: " + path);
    }
    report << "metric,value\n"
           << "total_cycles," << summary.total_cycles << '\n'
           << "operation_count," << summary.operation_count << '\n'
           << "cpu_busy_cycles," << summary.cpu_busy_cycles << '\n'
           << "dma_busy_cycles," << summary.dma_busy_cycles << '\n'
           << "pim_busy_cycles," << summary.pim_busy_cycles << '\n'
           << "cpu_instructions," << summary.cpu_instructions << '\n'
           << "h2p_bytes," << summary.h2p_bytes << '\n'
           << "p2h_bytes," << summary.p2h_bytes << '\n'
           << "pim_adc_conversions," << summary.pim_adc_conversions << '\n';
}

void writeTraceCsv(const std::string& path, const cpupim::TraceResult& result) {
    std::ofstream report(path);
    if (!report) {
        throw std::runtime_error("cannot write report: " + path);
    }
    report << "sequence,id,operation,resource,source_line,dependencies,start_cycle,"
              "end_cycle,cycles,dependency_ready_cycle,resource_ready_cycle,"
              "instructions,bytes,cells_written,adc_conversions,row_groups,"
              "input_bits,logic_function\n";
    for (const auto& event : result.events) {
        report << event.sequence << ',' << csvEscape(event.id) << ','
               << cpupim::operationName(event.kind) << ',' << cpupim::resourceName(event.resource)
               << ',' << event.source_line << ','
               << csvEscape(joinDependencies(event.dependencies, ";")) << ','
               << event.start_cycle << ',' << event.end_cycle << ',' << event.cycles
               << ',' << event.dependency_ready_cycle << ','
               << event.resource_ready_cycle << ',' << event.instructions << ','
               << event.bytes << ',' << event.cells_written << ','
               << event.adc_conversions << ',' << event.row_groups << ','
               << event.input_bits << ',';
        if (event.kind == cpupim::OperationKind::PimLogic) {
            report << cpupim::logicOperationName(event.logic_operation);
        }
        report << '\n';
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parseOptions(argc, argv);
        const auto operations = cpupim::readTrace(options.trace_path);
        const cpupim::TraceSimulator simulator(options.config);
        const cpupim::TraceResult result = simulator.run(operations);

        const std::string json_path = options.output_prefix + ".json";
        const std::string summary_path = options.output_prefix + ".csv";
        const std::string trace_path = options.output_prefix + "_trace.csv";
        writeJsonReport(json_path, options.config, result);
        writeSummaryCsv(summary_path, result.summary);
        writeTraceCsv(trace_path, result);

        std::cout << "Trace-driven CPU-PIM simulation\n"
                  << "Trace: " << options.trace_path << "\n\n";
        for (const auto& event : result.events) {
            std::cout << "  " << std::left << std::setw(18) << event.id
                      << std::setw(9) << cpupim::resourceName(event.resource)
                      << std::right << std::setw(8) << event.cycles
                      << " cycles  [" << event.start_cycle << ", "
                      << event.end_cycle << ")\n";
        }
        const auto& summary = result.summary;
        std::cout << "\nTotal cycles: " << summary.total_cycles
                  << "\nCPU busy cycles: " << summary.cpu_busy_cycles
                  << "\nDMA busy cycles: " << summary.dma_busy_cycles
                  << "\nPIM busy cycles: " << summary.pim_busy_cycles
                  << "\nCPU instructions: " << summary.cpu_instructions
                  << "\nHost-to-PIM bytes: " << summary.h2p_bytes
                  << "\nPIM-to-host bytes: " << summary.p2h_bytes
                  << "\nPIM ADC conversions: " << summary.pim_adc_conversions
                  << "\nWrote " << json_path << ", " << summary_path << ", and "
                  << trace_path << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
