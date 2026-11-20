#include "trace_simulator.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace cpupim {
namespace {

[[noreturn]] void traceError(const std::string& source,
                             std::size_t line,
                             const std::string& message) {
    throw std::invalid_argument(source + ":" + std::to_string(line) +
                                ": " + message);
}

Word parseUnsigned(const std::string& text,
                   const std::string& source,
                   std::size_t line,
                   const std::string& field) {
    if (text.empty() || text.front() == '-') {
        traceError(source, line, "invalid " + field + ": " + text);
    }
    std::size_t consumed = 0;
    unsigned long long value = 0;
    try {
        value = std::stoull(text, &consumed, 0);
    } catch (const std::exception&) {
        traceError(source, line, "invalid " + field + ": " + text);
    }
    if (consumed != text.size()) {
        traceError(source, line, "invalid " + field + ": " + text);
    }
    return static_cast<Word>(value);
}

std::size_t parseSize(const std::string& text,
                      const std::string& source,
                      std::size_t line,
                      const std::string& field) {
    const Word value = parseUnsigned(text, source, line, field);
    if (value > std::numeric_limits<std::size_t>::max()) {
        traceError(source, line, field + " is too large");
    }
    return static_cast<std::size_t>(value);
}

OperationKind parseOperationKind(const std::string& name,
                                 const std::string& source,
                                 std::size_t line) {
    if (name == "cpu") return OperationKind::Cpu;
    if (name == "sync") return OperationKind::Sync;
    if (name == "h2p") return OperationKind::H2P;
    if (name == "p2h") return OperationKind::P2H;
    if (name == "pim_load") return OperationKind::PimLoad;
    if (name == "pim_vmm") return OperationKind::PimVmm;
    if (name == "pim_write") return OperationKind::PimWrite;
    if (name == "pim_logic") return OperationKind::PimLogic;
    traceError(source, line, "unknown operation: " + name);
}

std::set<std::string> allowedFields(OperationKind kind) {
    switch (kind) {
        case OperationKind::Cpu:
            return {"cycles", "instructions", "stall_cycles", "deps"};
        case OperationKind::Sync:
            return {"deps"};
        case OperationKind::H2P:
        case OperationKind::P2H:
            return {"bytes", "deps"};
        case OperationKind::PimLoad:
            return {"rows", "channels", "weight_bits", "deps"};
        case OperationKind::PimVmm:
            return {"rows", "channels", "weight_bits", "activation_bits",
                    "deps"};
        case OperationKind::PimWrite:
            return {"columns", "deps"};
        case OperationKind::PimLogic:
            return {"columns", "function", "deps"};
    }
    throw std::invalid_argument("unknown operation kind");
}

std::vector<std::string> parseDependencies(const std::string& text,
                                           const std::string& source,
                                           std::size_t line) {
    std::vector<std::string> dependencies;
    std::unordered_set<std::string> seen;
    std::size_t begin = 0;
    while (begin <= text.size()) {
        const std::size_t comma = text.find(',', begin);
        const std::string dependency = text.substr(
            begin, comma == std::string::npos ? std::string::npos : comma - begin);
        if (dependency.empty()) {
            traceError(source, line, "dependencies must be comma-separated IDs");
        }
        if (!seen.insert(dependency).second) {
            traceError(source, line, "duplicate dependency: " + dependency);
        }
        dependencies.push_back(dependency);
        if (comma == std::string::npos) {
            break;
        }
        begin = comma + 1;
    }
    return dependencies;
}

void requireField(bool present,
                  const std::string& field,
                  const TraceOperation& operation,
                  const std::string& source) {
    if (!present) {
        traceError(source, operation.source_line,
                   std::string(operationName(operation.kind)) +
                       " requires " + field);
    }
}

Word checkedAdd(Word left, Word right) {
    if (right > std::numeric_limits<Word>::max() - left) {
        throw std::overflow_error("trace cycle or counter overflow");
    }
    return left + right;
}

Word ceilingDivide(Word value, Word divisor) {
    return value / divisor + static_cast<Word>(value % divisor != 0);
}

Word checkedShiftLeft(Word value, int shift) {
    if (value == 0) {
        return 0;
    }
    if (shift >= std::numeric_limits<Word>::digits ||
        value > (std::numeric_limits<Word>::max() >> shift)) {
        throw std::overflow_error("CPU cycle count exceeds 64 bits");
    }
    return value << shift;
}

Word cpuIssueCycles(Word instructions, double ipc) {
    if (instructions == 0) {
        return 0;
    }

    // A finite double is an integer significand multiplied by a power of two.
    // Using that representation avoids rounding instructions to double, which
    // would lose integer precision above 2^53.
    constexpr int significand_digits = std::numeric_limits<double>::digits;
    static_assert(significand_digits < std::numeric_limits<Word>::digits,
                  "CPU IPC decomposition requires a wider cycle counter");
    int exponent = 0;
    const double fraction = std::frexp(ipc, &exponent);
    const Word significand = static_cast<Word>(
        std::ldexp(fraction, significand_digits));
    const int binary_shift = exponent - significand_digits;

    if (binary_shift >= 0) {
        // ipc = significand * 2^binary_shift. If the denominator does not
        // fit in Word, it is larger than any possible instruction count.
        if (binary_shift >= std::numeric_limits<Word>::digits ||
            significand >
                (std::numeric_limits<Word>::max() >> binary_shift)) {
            return 1;
        }
        const Word divisor = significand << binary_shift;
        return ceilingDivide(instructions, divisor);
    }

    // ipc = significand / 2^binary_scale. Divide first, then scale the
    // quotient and remainder separately so neither multiplication can wrap.
    const int binary_scale = -binary_shift;
    const Word quotient = instructions / significand;
    Word remainder = instructions % significand;
    const Word whole_cycles = checkedShiftLeft(quotient, binary_scale);

    Word fractional_cycles = 0;
    for (int bit = 0; bit < binary_scale; ++bit) {
        // remainder < significand <= 2^53, so this doubling fits in Word.
        remainder <<= 1;
        const Word next_bit = remainder / significand;
        remainder %= significand;
        if (fractional_cycles >
            (std::numeric_limits<Word>::max() - next_bit) / 2) {
            throw std::overflow_error("CPU cycle count exceeds 64 bits");
        }
        fractional_cycles = fractional_cycles * 2 + next_bit;
    }
    if (remainder != 0) {
        fractional_cycles = checkedAdd(fractional_cycles, 1);
    }
    return checkedAdd(whole_cycles, fractional_cycles);
}

Resource resourceFor(OperationKind kind) {
    switch (kind) {
        case OperationKind::Cpu:
        case OperationKind::Sync:
            return Resource::Cpu;
        case OperationKind::H2P:
        case OperationKind::P2H:
            return Resource::Dma;
        case OperationKind::PimLoad:
        case OperationKind::PimVmm:
        case OperationKind::PimWrite:
        case OperationKind::PimLogic:
            return Resource::Pim;
    }
    throw std::invalid_argument("unknown operation kind");
}

}  // namespace

const char* operationName(OperationKind kind) {
    switch (kind) {
        case OperationKind::Cpu:
            return "cpu";
        case OperationKind::Sync:
            return "sync";
        case OperationKind::H2P:
            return "h2p";
        case OperationKind::P2H:
            return "p2h";
        case OperationKind::PimLoad:
            return "pim_load";
        case OperationKind::PimVmm:
            return "pim_vmm";
        case OperationKind::PimWrite:
            return "pim_write";
        case OperationKind::PimLogic:
            return "pim_logic";
    }
    throw std::invalid_argument("unknown operation kind");
}

const char* resourceName(Resource resource) {
    switch (resource) {
        case Resource::Cpu:
            return "cpu";
        case Resource::Dma:
            return "dma";
        case Resource::Pim:
            return "pim";
    }
    throw std::invalid_argument("unknown resource");
}

const char* logicOperationName(cim::LogicOperation operation) {
    switch (operation) {
        case cim::LogicOperation::And:
            return "and";
        case cim::LogicOperation::Or:
            return "or";
        case cim::LogicOperation::Xor:
            return "xor";
    }
    throw std::invalid_argument("unknown logic operation");
}

std::vector<TraceOperation> parseTrace(std::istream& input,
                                       const std::string& source_name) {
    std::vector<TraceOperation> operations;
    std::unordered_set<std::string> identifiers;
    std::string line;
    std::size_t line_number = 0;

    while (std::getline(input, line)) {
        ++line_number;
        const std::size_t comment = line.find('#');
        if (comment != std::string::npos) {
            line.erase(comment);
        }
        std::istringstream tokens(line);
        std::string kind;
        std::string id;
        if (!(tokens >> kind)) {
            continue;
        }
        if (!(tokens >> id)) {
            traceError(source_name, line_number,
                       "operation must be followed by a unique ID");
        }
        if (id.find('=') != std::string::npos) {
            traceError(source_name, line_number, "invalid operation ID: " + id);
        }
        if (!identifiers.insert(id).second) {
            traceError(source_name, line_number, "duplicate operation ID: " + id);
        }

        TraceOperation operation;
        operation.id = id;
        operation.kind = parseOperationKind(kind, source_name, line_number);
        operation.source_line = line_number;
        const std::set<std::string> allowed = allowedFields(operation.kind);
        std::unordered_set<std::string> seen;
        bool function_set = false;

        std::string token;
        while (tokens >> token) {
            const std::size_t equal = token.find('=');
            if (equal == std::string::npos || equal == 0 ||
                equal + 1 == token.size() ||
                token.find('=', equal + 1) != std::string::npos) {
                traceError(source_name, line_number,
                           "expected key=value field, got: " + token);
            }
            const std::string key = token.substr(0, equal);
            const std::string value = token.substr(equal + 1);
            if (allowed.count(key) == 0) {
                traceError(source_name, line_number,
                           "field " + key + " is not valid for " + kind);
            }
            if (!seen.insert(key).second) {
                traceError(source_name, line_number, "duplicate field: " + key);
            }

            if (key == "deps") {
                operation.dependencies =
                    parseDependencies(value, source_name, line_number);
            } else if (key == "cycles") {
                operation.cycles =
                    parseUnsigned(value, source_name, line_number, key);
                operation.cycles_set = true;
            } else if (key == "instructions") {
                operation.instructions =
                    parseUnsigned(value, source_name, line_number, key);
                operation.instructions_set = true;
            } else if (key == "stall_cycles") {
                operation.stall_cycles =
                    parseUnsigned(value, source_name, line_number, key);
            } else if (key == "bytes") {
                operation.bytes =
                    parseUnsigned(value, source_name, line_number, key);
            } else if (key == "rows") {
                operation.rows = parseSize(value, source_name, line_number, key);
            } else if (key == "channels") {
                operation.output_channels =
                    parseSize(value, source_name, line_number, key);
            } else if (key == "weight_bits") {
                operation.weight_bits =
                    parseSize(value, source_name, line_number, key);
            } else if (key == "activation_bits") {
                operation.activation_bits =
                    parseSize(value, source_name, line_number, key);
            } else if (key == "columns") {
                operation.columns =
                    parseSize(value, source_name, line_number, key);
            } else if (key == "function") {
                if (value == "and") {
                    operation.logic_operation = cim::LogicOperation::And;
                } else if (value == "or") {
                    operation.logic_operation = cim::LogicOperation::Or;
                } else if (value == "xor") {
                    operation.logic_operation = cim::LogicOperation::Xor;
                } else {
                    traceError(source_name, line_number,
                               "logic function must be and, or, or xor");
                }
                function_set = true;
            }
        }

        switch (operation.kind) {
            case OperationKind::Cpu:
                if (operation.cycles_set == operation.instructions_set) {
                    traceError(source_name, line_number,
                               "cpu requires exactly one of cycles or instructions");
                }
                break;
            case OperationKind::Sync:
                break;
            case OperationKind::H2P:
            case OperationKind::P2H:
                requireField(seen.count("bytes") != 0, "bytes", operation,
                             source_name);
                if (operation.bytes == 0) {
                    traceError(source_name, line_number, "bytes must be positive");
                }
                break;
            case OperationKind::PimLoad:
            case OperationKind::PimVmm:
                requireField(seen.count("rows") != 0, "rows", operation,
                             source_name);
                requireField(seen.count("channels") != 0, "channels", operation,
                             source_name);
                requireField(seen.count("weight_bits") != 0, "weight_bits",
                             operation, source_name);
                if (operation.kind == OperationKind::PimVmm) {
                    requireField(seen.count("activation_bits") != 0,
                                 "activation_bits", operation, source_name);
                }
                if (operation.rows == 0 || operation.output_channels == 0) {
                    traceError(source_name, line_number,
                               "rows and channels must be positive");
                }
                if (operation.weight_bits == 0 || operation.weight_bits > 64 ||
                    (operation.kind == OperationKind::PimVmm &&
                     (operation.activation_bits == 0 ||
                      operation.activation_bits > 64))) {
                    traceError(source_name, line_number,
                               "bit widths must be in the range 1..64");
                }
                break;
            case OperationKind::PimWrite:
                requireField(seen.count("columns") != 0, "columns", operation,
                             source_name);
                if (operation.columns == 0) {
                    traceError(source_name, line_number,
                               "columns must be positive");
                }
                break;
            case OperationKind::PimLogic:
                requireField(seen.count("columns") != 0, "columns", operation,
                             source_name);
                requireField(function_set, "function", operation, source_name);
                if (operation.columns == 0) {
                    traceError(source_name, line_number,
                               "columns must be positive");
                }
                break;
        }
        operations.push_back(std::move(operation));
    }

    if (operations.empty()) {
        throw std::invalid_argument(source_name + ": trace contains no operations");
    }
    return operations;
}

std::vector<TraceOperation> readTrace(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open trace: " + path);
    }
    return parseTrace(input, path);
}

TraceSimulator::TraceSimulator(SystemConfig config) : config_(config) {
    if (!std::isfinite(config_.cpu_ipc) || config_.cpu_ipc <= 0.0) {
        throw std::invalid_argument("cpu_ipc must be finite and positive");
    }
    if (config_.dma_bytes_per_cycle == 0) {
        throw std::invalid_argument("dma_bytes_per_cycle must be positive");
    }
    if (config_.pim.write_ports == 0 || config_.pim.max_active_rows == 0 ||
        config_.pim.adc_count == 0) {
        throw std::invalid_argument(
            "PIM write_ports, max_active_rows, and adc_count must be positive");
    }
}

const SystemConfig& TraceSimulator::config() const {
    return config_;
}

TraceResult TraceSimulator::run(
    const std::vector<TraceOperation>& operations) const {
    if (operations.empty()) {
        throw std::invalid_argument("cannot simulate an empty trace");
    }

    std::unordered_map<std::string, std::size_t> indices;
    for (std::size_t index = 0; index < operations.size(); ++index) {
        if (operations[index].id.empty()) {
            throw std::invalid_argument("trace operation IDs must not be empty");
        }
        if (!indices.emplace(operations[index].id, index).second) {
            throw std::invalid_argument("duplicate operation ID: " +
                                        operations[index].id);
        }
    }

    std::vector<std::size_t> indegree(operations.size(), 0);
    std::vector<std::vector<std::size_t>> dependents(operations.size());
    for (std::size_t index = 0; index < operations.size(); ++index) {
        std::unordered_set<std::string> seen;
        for (const std::string& dependency : operations[index].dependencies) {
            const auto found = indices.find(dependency);
            if (found == indices.end()) {
                throw std::invalid_argument("operation " + operations[index].id +
                                            " has unknown dependency: " + dependency);
            }
            if (found->second == index) {
                throw std::invalid_argument("operation " + operations[index].id +
                                            " depends on itself");
            }
            if (!seen.insert(dependency).second) {
                throw std::invalid_argument("operation " + operations[index].id +
                                            " has duplicate dependency: " + dependency);
            }
            ++indegree[index];
            dependents[found->second].push_back(index);
        }
    }

    std::set<std::size_t> ready;
    for (std::size_t index = 0; index < indegree.size(); ++index) {
        if (indegree[index] == 0) {
            ready.insert(index);
        }
    }

    TraceResult result;
    result.summary.operation_count = operations.size();
    result.events.reserve(operations.size());
    std::vector<Word> end_cycles(operations.size(), 0);
    Word resource_available[] = {0, 0, 0};

    while (!ready.empty()) {
        const std::size_t index = *ready.begin();
        ready.erase(ready.begin());
        const TraceOperation& operation = operations[index];

        TraceEvent event;
        event.sequence = result.events.size();
        event.id = operation.id;
        event.kind = operation.kind;
        event.resource = resourceFor(operation.kind);
        event.source_line = operation.source_line;
        event.dependencies = operation.dependencies;
        event.instructions = operation.instructions_set
                                 ? operation.instructions
                                 : 0;
        event.logic_operation = operation.logic_operation;

        for (const std::string& dependency : operation.dependencies) {
            event.dependency_ready_cycle = std::max(
                event.dependency_ready_cycle, end_cycles[indices.at(dependency)]);
        }
        const std::size_t resource_index =
            static_cast<std::size_t>(event.resource);
        event.resource_ready_cycle = resource_available[resource_index];
        event.start_cycle = std::max(event.dependency_ready_cycle,
                                     event.resource_ready_cycle);

        switch (operation.kind) {
            case OperationKind::Cpu:
                if (operation.cycles_set == operation.instructions_set) {
                    throw std::invalid_argument(
                        "cpu operation " + operation.id +
                        " requires exactly one of cycles or instructions");
                }
                if (operation.cycles_set) {
                    event.cycles = operation.cycles;
                } else {
                    event.cycles = cpuIssueCycles(
                        operation.instructions, config_.cpu_ipc);
                }
                event.cycles = checkedAdd(event.cycles, operation.stall_cycles);
                result.summary.cpu_instructions = checkedAdd(
                    result.summary.cpu_instructions, event.instructions);
                break;
            case OperationKind::Sync:
                event.cycles = 0;
                break;
            case OperationKind::H2P:
            case OperationKind::P2H:
                if (operation.bytes == 0) {
                    throw std::invalid_argument("DMA operation " + operation.id +
                                                " requires positive bytes");
                }
                event.bytes = operation.bytes;
                event.cycles = checkedAdd(
                    config_.dma_setup_cycles,
                    ceilingDivide(operation.bytes,
                                  static_cast<Word>(
                                      config_.dma_bytes_per_cycle)));
                if (operation.kind == OperationKind::H2P) {
                    result.summary.h2p_bytes =
                        checkedAdd(result.summary.h2p_bytes, event.bytes);
                } else {
                    result.summary.p2h_bytes =
                        checkedAdd(result.summary.p2h_bytes, event.bytes);
                }
                break;
            case OperationKind::PimLoad: {
                const cim::WeightLoadTiming timing = cim::estimateWeightLoadTiming(
                    operation.rows, operation.output_channels,
                    operation.weight_bits, config_.pim);
                event.cycles = timing.cycles;
                event.cells_written = timing.cells_written;
                break;
            }
            case OperationKind::PimVmm: {
                const cim::VmmTiming timing = cim::estimateVmmTiming(
                    operation.rows, operation.output_channels,
                    operation.weight_bits, operation.activation_bits, config_.pim);
                event.cycles = timing.cycles;
                event.adc_conversions = timing.adc_conversions;
                event.row_groups = timing.row_groups;
                event.input_bits = timing.input_bits;
                result.summary.pim_adc_conversions = checkedAdd(
                    result.summary.pim_adc_conversions,
                    event.adc_conversions);
                break;
            }
            case OperationKind::PimWrite:
                event.cycles = cim::estimateRowWriteCycles(
                    operation.columns, config_.pim);
                event.cells_written = static_cast<Word>(operation.columns);
                break;
            case OperationKind::PimLogic:
                if (operation.columns == 0) {
                    throw std::invalid_argument("PIM logic operation " +
                                                operation.id +
                                                " requires positive columns");
                }
                event.cycles = cim::estimateLogicCycles(
                    operation.logic_operation, config_.pim);
                event.cells_written = static_cast<Word>(operation.columns);
                break;
        }

        event.end_cycle = checkedAdd(event.start_cycle, event.cycles);
        resource_available[resource_index] = event.end_cycle;
        end_cycles[index] = event.end_cycle;
        result.summary.total_cycles =
            std::max(result.summary.total_cycles, event.end_cycle);
        if (event.resource == Resource::Cpu) {
            result.summary.cpu_busy_cycles = checkedAdd(
                result.summary.cpu_busy_cycles, event.cycles);
        } else if (event.resource == Resource::Dma) {
            result.summary.dma_busy_cycles = checkedAdd(
                result.summary.dma_busy_cycles, event.cycles);
        } else {
            result.summary.pim_busy_cycles = checkedAdd(
                result.summary.pim_busy_cycles, event.cycles);
        }
        result.events.push_back(std::move(event));

        for (const std::size_t dependent : dependents[index]) {
            if (--indegree[dependent] == 0) {
                ready.insert(dependent);
            }
        }
    }

    if (result.events.size() != operations.size()) {
        throw std::invalid_argument("trace dependency graph contains a cycle");
    }
    return result;
}

}  // namespace cpupim
