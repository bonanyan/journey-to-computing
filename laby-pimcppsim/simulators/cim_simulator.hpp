#ifndef CIM_SIMULATOR_HPP
#define CIM_SIMULATOR_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cim {

using Bit = std::uint8_t;
using Word = std::uint64_t;
using Matrix = std::vector<std::vector<Word>>;
using BitRow = std::vector<Bit>;

struct TimingConfig {
    std::size_t write_ports = 32;
    std::size_t max_active_rows = 64;
    std::size_t adc_count = 8;
    Word adc_cycles = 2;
    Word accumulate_cycles = 1;
    Word write_setup_cycles = 1;
    Word vmm_setup_cycles = 2;
    Word logic_setup_cycles = 1;
    Word boolean_sense_cycles = 1;
    Word xor_sense_cycles = 2;
    Word logic_write_cycles = 1;
};

struct ArrayConfig {
    std::size_t rows = 0;
    std::size_t columns = 0;
    std::size_t weight_bits = 4;
};

struct OperationStats {
    std::string operation;
    Word start_cycle = 0;
    Word end_cycle = 0;
    Word cycles = 0;
    Word cells_written = 0;
    Word adc_conversions = 0;
    Word row_groups = 0;
    Word input_bits = 0;
};

struct WeightLoadTiming {
    Word cycles = 0;
    Word cells_written = 0;
};

struct VmmTiming {
    Word cycles = 0;
    Word adc_conversions = 0;
    Word row_groups = 0;
    Word input_bits = 0;
};

WeightLoadTiming estimateWeightLoadTiming(std::size_t rows,
                                          std::size_t output_channels,
                                          std::size_t weight_bits,
                                          const TimingConfig& timing);
VmmTiming estimateVmmTiming(std::size_t rows,
                            std::size_t output_channels,
                            std::size_t weight_bits,
                            std::size_t activation_bits,
                            const TimingConfig& timing);
Word estimateRowWriteCycles(std::size_t columns, const TimingConfig& timing);

enum class LogicOperation {
    And,
    Or,
    Xor
};

Word estimateLogicCycles(LogicOperation operation, const TimingConfig& timing);

class Statistics {
public:
    void record(const std::string& operation, Word cycles);
    void record(OperationStats stats);
    void clear();

    const std::vector<OperationStats>& operations() const;
    Word totalCycles() const;

private:
    std::vector<OperationStats> operations_;
    Word total_cycles_ = 0;
};

class Simulator {
public:
    explicit Simulator(TimingConfig timing = {});

    const TimingConfig& timing() const;
    Statistics& statistics();
    const Statistics& statistics() const;

private:
    void record(OperationStats stats);

    TimingConfig timing_;
    Statistics statistics_;

    friend class VmmArray;
    friend class LogicArray;
};

class VmmArray {
public:
    VmmArray(Simulator& simulator, ArrayConfig config);

    void loadWeights(const Matrix& weights);
    std::vector<Word> multiply(const std::vector<Word>& activations,
                               std::size_t activation_bits);

    const ArrayConfig& config() const;
    std::size_t outputChannels() const;
    bool weightsLoaded() const;
    Bit readCell(std::size_t row, std::size_t column) const;

private:
    Simulator& simulator_;
    ArrayConfig config_;
    std::vector<Bit> cells_;
    std::size_t output_channels_ = 0;
    bool weights_loaded_ = false;
};

class LogicArray {
public:
    LogicArray(Simulator& simulator, std::size_t rows, std::size_t columns);

    void writeRow(std::size_t row, const BitRow& bits);
    BitRow readRow(std::size_t row) const;
    BitRow apply(LogicOperation operation,
                 std::size_t destination,
                 std::size_t left,
                 std::size_t right);

    std::size_t rows() const;
    std::size_t columns() const;

private:
    std::size_t index(std::size_t row, std::size_t column) const;
    void validateRow(std::size_t row) const;

    Simulator& simulator_;
    std::size_t rows_;
    std::size_t columns_;
    std::vector<Bit> cells_;
};

}  // namespace cim

#endif
