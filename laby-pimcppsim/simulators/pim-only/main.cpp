#include "cim_simulator.hpp"

#include <cctype>
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
    std::string weights_path;
    std::string input_path;
    std::string output_prefix = "results";
    std::size_t weight_bits = 4;
    std::size_t activation_bits = 4;
    cim::TimingConfig timing;
    bool custom_input = false;
    bool max_active_rows_set = false;
    bool adc_count_set = false;
    bool write_ports_set = false;
};

struct LogicResults {
    cim::BitRow and_result;
    cim::BitRow or_result;
    cim::BitRow xor_result;
};

void printUsage(const char* executable) {
    std::cout
        << "Usage: " << executable << " [options]\n"
        << "Runs the built-in demo unless --weights and --input are supplied.\n\n"
        << "  --weights FILE             CSV/whitespace weight matrix\n"
        << "  --input FILE               CSV/whitespace activation vector\n"
        << "  --weight-bits N            Weight width, 1..64 (default: 4)\n"
        << "  --activation-bits N        Input width, 1..64 (default: 4)\n"
        << "  --max-active-rows N        Rows sensed per group (default: 64)\n"
        << "  --adc-count N              Parallel ideal ADCs (default: 8)\n"
        << "  --adc-cycles N             Cycles per ADC batch (default: 2)\n"
        << "  --accumulate-cycles N      Digital accumulation per group (default: 1)\n"
        << "  --write-ports N            Parallel cell write ports (default: 32)\n"
        << "  --vmm-setup-cycles N       VMM setup cycles (default: 2)\n"
        << "  --write-setup-cycles N     Write setup cycles (default: 1)\n"
        << "  --output-prefix PREFIX     Report prefix (default: results)\n"
        << "  --help                     Show this help\n";
}

cim::Word parseUnsigned(const std::string& text, const std::string& description) {
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
    return static_cast<cim::Word>(value);
}

std::size_t parseSize(const std::string& text, const std::string& description) {
    const cim::Word value = parseUnsigned(text, description);
    if (value > std::numeric_limits<std::size_t>::max()) {
        throw std::invalid_argument(description + " is too large");
    }
    return static_cast<std::size_t>(value);
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
        } else if (argument == "--weights") {
            options.weights_path = requireValue(argc, argv, index);
        } else if (argument == "--input") {
            options.input_path = requireValue(argc, argv, index);
        } else if (argument == "--output-prefix") {
            options.output_prefix = requireValue(argc, argv, index);
        } else if (argument == "--weight-bits") {
            options.weight_bits = parseSize(requireValue(argc, argv, index),
                                            "weight bit width");
        } else if (argument == "--activation-bits") {
            options.activation_bits = parseSize(requireValue(argc, argv, index),
                                                "activation bit width");
        } else if (argument == "--max-active-rows") {
            options.timing.max_active_rows = parseSize(
                requireValue(argc, argv, index), "maximum active row count");
            options.max_active_rows_set = true;
        } else if (argument == "--adc-count") {
            options.timing.adc_count = parseSize(requireValue(argc, argv, index),
                                                 "ADC count");
            options.adc_count_set = true;
        } else if (argument == "--write-ports") {
            options.timing.write_ports = parseSize(requireValue(argc, argv, index),
                                                   "write port count");
            options.write_ports_set = true;
        } else if (argument == "--adc-cycles") {
            options.timing.adc_cycles = parseUnsigned(
                requireValue(argc, argv, index), "ADC cycle count");
        } else if (argument == "--accumulate-cycles") {
            options.timing.accumulate_cycles = parseUnsigned(
                requireValue(argc, argv, index), "accumulation cycle count");
        } else if (argument == "--vmm-setup-cycles") {
            options.timing.vmm_setup_cycles = parseUnsigned(
                requireValue(argc, argv, index), "VMM setup cycle count");
        } else if (argument == "--write-setup-cycles") {
            options.timing.write_setup_cycles = parseUnsigned(
                requireValue(argc, argv, index), "write setup cycle count");
        } else {
            throw std::invalid_argument("unknown option: " + argument);
        }
    }

    if (options.weights_path.empty() != options.input_path.empty()) {
        throw std::invalid_argument("--weights and --input must be used together");
    }
    options.custom_input = !options.weights_path.empty();
    if (options.output_prefix.empty()) {
        throw std::invalid_argument("output prefix must not be empty");
    }
    return options;
}

std::vector<std::vector<cim::Word>> readNumericTable(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open input file: " + path);
    }

    std::vector<std::vector<cim::Word>> table;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        const std::size_t comment = line.find('#');
        if (comment != std::string::npos) {
            line.erase(comment);
        }
        for (char& character : line) {
            if (character == ',' || character == '\t') {
                character = ' ';
            }
        }
        std::istringstream tokens(line);
        std::vector<cim::Word> row;
        std::string token;
        while (tokens >> token) {
            row.push_back(parseUnsigned(token, "number at " + path + ":" +
                                                  std::to_string(line_number)));
        }
        if (!row.empty()) {
            table.push_back(std::move(row));
        }
    }
    if (table.empty()) {
        throw std::invalid_argument("input file contains no numbers: " + path);
    }
    return table;
}

cim::Matrix readWeights(const std::string& path) {
    cim::Matrix weights = readNumericTable(path);
    const std::size_t columns = weights.front().size();
    for (const auto& row : weights) {
        if (row.size() != columns) {
            throw std::invalid_argument("weight matrix must be rectangular");
        }
    }
    return weights;
}

std::vector<cim::Word> readActivations(const std::string& path) {
    const auto table = readNumericTable(path);
    std::vector<cim::Word> activations;
    for (const auto& row : table) {
        activations.insert(activations.end(), row.begin(), row.end());
    }
    return activations;
}

std::string bitsToString(const cim::BitRow& bits) {
    std::string result;
    result.reserve(bits.size());
    for (const cim::Bit bit : bits) {
        result.push_back(bit == 0 ? '0' : '1');
    }
    return result;
}

template <typename T>
void printVector(const std::vector<T>& values) {
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            std::cout << ' ';
        }
        std::cout << values[index];
    }
    std::cout << '\n';
}

void writeJsonReport(const std::string& path,
                     const cim::VmmArray& array,
                     const cim::TimingConfig& timing,
                     std::size_t activation_bits,
                     const std::vector<cim::Word>& outputs,
                     const LogicResults& logic,
                     const cim::Statistics& statistics) {
    std::ofstream report(path);
    if (!report) {
        throw std::runtime_error("cannot write report: " + path);
    }

    const auto& config = array.config();
    report << "{\n"
           << "  \"model\": \"unsigned_bit_serial_cim_v1\",\n"
           << "  \"configuration\": {\n"
           << "    \"rows\": " << config.rows << ",\n"
           << "    \"columns\": " << config.columns << ",\n"
           << "    \"output_channels\": " << array.outputChannels() << ",\n"
           << "    \"weight_bits\": " << config.weight_bits << ",\n"
           << "    \"activation_bits\": " << activation_bits << ",\n"
           << "    \"write_ports\": " << timing.write_ports << ",\n"
           << "    \"max_active_rows\": " << timing.max_active_rows << ",\n"
           << "    \"adc_count\": " << timing.adc_count << ",\n"
           << "    \"adc_cycles\": " << timing.adc_cycles << ",\n"
           << "    \"accumulate_cycles\": " << timing.accumulate_cycles << ",\n"
           << "    \"write_setup_cycles\": " << timing.write_setup_cycles << ",\n"
           << "    \"vmm_setup_cycles\": " << timing.vmm_setup_cycles << "\n"
           << "  },\n"
           << "  \"functional_results\": {\n"
           << "    \"vmm\": [";
    for (std::size_t index = 0; index < outputs.size(); ++index) {
        if (index != 0) {
            report << ", ";
        }
        report << outputs[index];
    }
    report << "],\n"
           << "    \"logic_and\": \"" << bitsToString(logic.and_result) << "\",\n"
           << "    \"logic_or\": \"" << bitsToString(logic.or_result) << "\",\n"
           << "    \"logic_xor\": \"" << bitsToString(logic.xor_result) << "\"\n"
           << "  },\n"
           << "  \"operations\": [\n";

    const auto& operations = statistics.operations();
    for (std::size_t index = 0; index < operations.size(); ++index) {
        const auto& operation = operations[index];
        report << "    {\"operation\": \"" << operation.operation
               << "\", \"start_cycle\": " << operation.start_cycle
               << ", \"end_cycle\": " << operation.end_cycle
               << ", \"cycles\": " << operation.cycles
               << ", \"cells_written\": " << operation.cells_written
               << ", \"adc_conversions\": " << operation.adc_conversions
               << ", \"row_groups\": " << operation.row_groups
               << ", \"input_bits\": " << operation.input_bits << "}";
        if (index + 1 != operations.size()) {
            report << ',';
        }
        report << '\n';
    }
    report << "  ],\n"
           << "  \"total_cycles\": " << statistics.totalCycles() << "\n"
           << "}\n";
    if (!report) {
        throw std::runtime_error("failed while writing report: " + path);
    }
}

void writeResultsCsv(const std::string& path,
                     const std::vector<cim::Word>& outputs,
                     const LogicResults& logic) {
    std::ofstream report(path);
    if (!report) {
        throw std::runtime_error("cannot write report: " + path);
    }
    report << "kind,index,value\n";
    for (std::size_t index = 0; index < outputs.size(); ++index) {
        report << "vmm," << index << ',' << outputs[index] << '\n';
    }
    const std::vector<std::pair<std::string, const cim::BitRow*>> logic_rows = {
        {"logic_and", &logic.and_result},
        {"logic_or", &logic.or_result},
        {"logic_xor", &logic.xor_result}
    };
    for (const auto& entry : logic_rows) {
        for (std::size_t index = 0; index < entry.second->size(); ++index) {
            report << entry.first << ',' << index << ','
                   << static_cast<unsigned>((*entry.second)[index]) << '\n';
        }
    }
}

void writeTraceCsv(const std::string& path, const cim::Statistics& statistics) {
    std::ofstream report(path);
    if (!report) {
        throw std::runtime_error("cannot write report: " + path);
    }
    report << "sequence,operation,start_cycle,end_cycle,cycles,cells_written,"
              "adc_conversions,row_groups,input_bits\n";
    const auto& operations = statistics.operations();
    for (std::size_t index = 0; index < operations.size(); ++index) {
        const auto& operation = operations[index];
        report << index << ',' << operation.operation << ','
               << operation.start_cycle << ',' << operation.end_cycle << ','
               << operation.cycles << ',' << operation.cells_written << ','
               << operation.adc_conversions << ',' << operation.row_groups << ','
               << operation.input_bits << '\n';
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parseOptions(argc, argv);
        cim::Matrix weights = {
            {1, 2, 3},
            {4, 5, 6},
            {7, 8, 9},
            {10, 11, 12}
        };
        std::vector<cim::Word> activations = {1, 2, 3, 4};
        if (options.custom_input) {
            weights = readWeights(options.weights_path);
            activations = readActivations(options.input_path);
        }

        if (weights.empty() || weights.front().empty()) {
            throw std::invalid_argument("weight matrix must not be empty");
        }
        if (weights.front().size() >
            std::numeric_limits<std::size_t>::max() / options.weight_bits) {
            throw std::overflow_error("weight matrix is too wide");
        }

        cim::TimingConfig timing = options.timing;
        if (!options.custom_input) {
            if (!options.max_active_rows_set) {
                timing.max_active_rows = 2;
            }
            if (!options.adc_count_set) {
                timing.adc_count = 2;
            }
            if (!options.write_ports_set) {
                timing.write_ports = 4;
            }
        }

        cim::Simulator simulator(timing);
        const cim::ArrayConfig config = {
            weights.size(),
            weights.front().size() * options.weight_bits,
            options.weight_bits
        };
        cim::VmmArray vmm(simulator, config);
        vmm.loadWeights(weights);
        const std::vector<cim::Word> outputs =
            vmm.multiply(activations, options.activation_bits);

        LogicResults logic;
        if (!options.custom_input) {
            cim::LogicArray logic_array(simulator, 5, 8);
            logic_array.writeRow(0, {1, 0, 1, 0, 1, 0, 1, 0});
            logic_array.writeRow(1, {1, 1, 0, 0, 1, 1, 0, 0});
            logic.and_result = logic_array.apply(cim::LogicOperation::And, 2, 0, 1);
            logic.or_result = logic_array.apply(cim::LogicOperation::Or, 3, 0, 1);
            logic.xor_result = logic_array.apply(cim::LogicOperation::Xor, 4, 0, 1);
        }

        const std::string json_path = options.output_prefix + ".json";
        const std::string results_path = options.output_prefix + ".csv";
        const std::string trace_path = options.output_prefix + "_trace.csv";
        writeJsonReport(json_path, vmm, timing, options.activation_bits,
                        outputs, logic, simulator.statistics());
        writeResultsCsv(results_path, outputs, logic);
        writeTraceCsv(trace_path, simulator.statistics());

        std::cout << "CIM abstract simulation\n";
        std::cout << "VMM outputs: ";
        printVector(outputs);
        if (!options.custom_input) {
            std::cout << "Logic AND: " << bitsToString(logic.and_result) << '\n';
            std::cout << "Logic OR:  " << bitsToString(logic.or_result) << '\n';
            std::cout << "Logic XOR: " << bitsToString(logic.xor_result) << '\n';
        }
        std::cout << "\nCycle report:\n";
        for (const auto& operation : simulator.statistics().operations()) {
            std::cout << "  " << std::left << std::setw(14) << operation.operation
                      << std::right << std::setw(8) << operation.cycles
                      << " cycles  [" << operation.start_cycle << ", "
                      << operation.end_cycle << ")\n";
        }
        std::cout << "Total cycles: " << simulator.statistics().totalCycles() << '\n';
        std::cout << "Wrote " << json_path << ", " << results_path << ", and "
                  << trace_path << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
