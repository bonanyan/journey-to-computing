#ifndef CPU_PIM_TRACE_SIMULATOR_HPP
#define CPU_PIM_TRACE_SIMULATOR_HPP

#include "cim_simulator.hpp"

#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

namespace cpupim {

using Word = cim::Word;

enum class OperationKind {
    Cpu,
    Sync,
    H2P,
    P2H,
    PimLoad,
    PimVmm,
    PimWrite,
    PimLogic
};

enum class Resource {
    Cpu,
    Dma,
    Pim
};

struct TraceOperation {
    std::string id;
    OperationKind kind = OperationKind::Cpu;
    std::size_t source_line = 0;
    std::vector<std::string> dependencies;

    bool cycles_set = false;
    Word cycles = 0;
    bool instructions_set = false;
    Word instructions = 0;
    Word stall_cycles = 0;
    Word bytes = 0;

    std::size_t rows = 0;
    std::size_t output_channels = 0;
    std::size_t weight_bits = 0;
    std::size_t activation_bits = 0;
    std::size_t columns = 0;
    cim::LogicOperation logic_operation = cim::LogicOperation::And;
};

struct SystemConfig {
    double cpu_ipc = 4.0;
    std::size_t dma_bytes_per_cycle = 16;
    Word dma_setup_cycles = 2;
    cim::TimingConfig pim;
};

struct TraceEvent {
    std::size_t sequence = 0;
    std::string id;
    OperationKind kind = OperationKind::Cpu;
    Resource resource = Resource::Cpu;
    std::size_t source_line = 0;
    std::vector<std::string> dependencies;

    Word start_cycle = 0;
    Word end_cycle = 0;
    Word cycles = 0;
    Word dependency_ready_cycle = 0;
    Word resource_ready_cycle = 0;

    Word instructions = 0;
    Word bytes = 0;
    Word cells_written = 0;
    Word adc_conversions = 0;
    Word row_groups = 0;
    Word input_bits = 0;
    cim::LogicOperation logic_operation = cim::LogicOperation::And;
};

struct TraceSummary {
    Word total_cycles = 0;
    Word cpu_busy_cycles = 0;
    Word dma_busy_cycles = 0;
    Word pim_busy_cycles = 0;
    Word cpu_instructions = 0;
    Word h2p_bytes = 0;
    Word p2h_bytes = 0;
    Word pim_adc_conversions = 0;
    std::size_t operation_count = 0;
};

struct TraceResult {
    std::vector<TraceEvent> events;
    TraceSummary summary;
};

std::vector<TraceOperation> parseTrace(std::istream& input,
                                       const std::string& source_name);
std::vector<TraceOperation> readTrace(const std::string& path);

class TraceSimulator {
public:
    explicit TraceSimulator(SystemConfig config = {});

    TraceResult run(const std::vector<TraceOperation>& operations) const;
    const SystemConfig& config() const;

private:
    SystemConfig config_;
};

const char* operationName(OperationKind kind);
const char* resourceName(Resource resource);
const char* logicOperationName(cim::LogicOperation operation);

}  // namespace cpupim

#endif
