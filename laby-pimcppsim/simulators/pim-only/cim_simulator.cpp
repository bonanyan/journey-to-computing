#include "cim_simulator.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace cim {
namespace {

Word checkedAdd(Word left, Word right) {
    if (right > std::numeric_limits<Word>::max() - left) {
        throw std::overflow_error("cycle or result counter overflow");
    }
    return left + right;
}

Word checkedMultiply(Word left, Word right) {
    if (left != 0 && right > std::numeric_limits<Word>::max() / left) {
        throw std::overflow_error("cycle or result counter overflow");
    }
    return left * right;
}

std::size_t checkedSizeProduct(std::size_t left, std::size_t right) {
    if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left) {
        throw std::overflow_error("array dimensions are too large");
    }
    return left * right;
}

Word ceilingDivide(Word value, Word divisor) {
    return value / divisor + static_cast<Word>(value % divisor != 0);
}

bool fitsInBits(Word value, std::size_t bits) {
    return bits == 64 || value < (Word{1} << bits);
}

void validateBitWidth(std::size_t bits, const std::string& name) {
    if (bits == 0 || bits > 64) {
        throw std::invalid_argument(name + " must be in the range 1..64");
    }
}

void addShiftedContribution(Word& output, Word count, std::size_t shift) {
    if (count == 0) {
        return;
    }
    if (shift >= 64 || count > (std::numeric_limits<Word>::max() >> shift)) {
        throw std::overflow_error("VMM result exceeds 64 bits");
    }
    const Word contribution = count << shift;
    if (contribution > std::numeric_limits<Word>::max() - output) {
        throw std::overflow_error("VMM result exceeds 64 bits");
    }
    output += contribution;
}

const char* logicName(LogicOperation operation) {
    switch (operation) {
        case LogicOperation::And:
            return "and";
        case LogicOperation::Or:
            return "or";
        case LogicOperation::Xor:
            return "xor";
    }
    throw std::invalid_argument("unknown logic operation");
}

}  // namespace

void Statistics::record(const std::string& operation, Word cycles) {
    OperationStats stats;
    stats.operation = operation;
    stats.cycles = cycles;
    record(stats);
}

void Statistics::record(OperationStats stats) {
    stats.start_cycle = total_cycles_;
    total_cycles_ = checkedAdd(total_cycles_, stats.cycles);
    stats.end_cycle = total_cycles_;
    operations_.push_back(std::move(stats));
}

void Statistics::clear() {
    operations_.clear();
    total_cycles_ = 0;
}

const std::vector<OperationStats>& Statistics::operations() const {
    return operations_;
}

Word Statistics::totalCycles() const {
    return total_cycles_;
}

Simulator::Simulator(TimingConfig timing) : timing_(timing) {
    if (timing_.write_ports == 0 || timing_.max_active_rows == 0 ||
        timing_.adc_count == 0) {
        throw std::invalid_argument(
            "write_ports, max_active_rows, and adc_count must be positive");
    }
}

const TimingConfig& Simulator::timing() const {
    return timing_;
}

Statistics& Simulator::statistics() {
    return statistics_;
}

const Statistics& Simulator::statistics() const {
    return statistics_;
}

void Simulator::record(OperationStats stats) {
    statistics_.record(std::move(stats));
}

VmmArray::VmmArray(Simulator& simulator, ArrayConfig config)
    : simulator_(simulator), config_(config) {
    validateBitWidth(config_.weight_bits, "weight_bits");
    if (config_.rows == 0 || config_.columns == 0) {
        throw std::invalid_argument("VMM array rows and columns must be positive");
    }
    cells_.assign(checkedSizeProduct(config_.rows, config_.columns), 0);
}

void VmmArray::loadWeights(const Matrix& weights) {
    if (weights.size() != config_.rows) {
        throw std::invalid_argument("weight row count must equal the array row count");
    }
    if (weights.empty() || weights.front().empty()) {
        throw std::invalid_argument("weight matrix must not be empty");
    }

    const std::size_t outputs = weights.front().size();
    const std::size_t used_columns =
        checkedSizeProduct(outputs, config_.weight_bits);
    if (used_columns > config_.columns) {
        throw std::invalid_argument(
            "weight bit planes require more columns than the array provides");
    }

    std::vector<Bit> new_cells(cells_.size(), 0);
    for (std::size_t row = 0; row < weights.size(); ++row) {
        if (weights[row].size() != outputs) {
            throw std::invalid_argument("weight matrix must be rectangular");
        }
        for (std::size_t output = 0; output < outputs; ++output) {
            const Word weight = weights[row][output];
            if (!fitsInBits(weight, config_.weight_bits)) {
                throw std::invalid_argument("weight exceeds weight_bits");
            }
            for (std::size_t bit = 0; bit < config_.weight_bits; ++bit) {
                const std::size_t column = output * config_.weight_bits + bit;
                new_cells[row * config_.columns + column] =
                    static_cast<Bit>((weight >> bit) & Word{1});
            }
        }
    }

    cells_.swap(new_cells);
    output_channels_ = outputs;
    weights_loaded_ = true;

    OperationStats stats;
    stats.operation = "weight_load";
    stats.cells_written = static_cast<Word>(checkedSizeProduct(config_.rows, used_columns));
    stats.cycles = checkedAdd(
        simulator_.timing().write_setup_cycles,
        ceilingDivide(stats.cells_written,
                      static_cast<Word>(simulator_.timing().write_ports)));
    simulator_.record(std::move(stats));
}

std::vector<Word> VmmArray::multiply(const std::vector<Word>& activations,
                                     std::size_t activation_bits) {
    if (!weights_loaded_) {
        throw std::logic_error("weights must be loaded before multiplication");
    }
    validateBitWidth(activation_bits, "activation_bits");
    if (activations.size() != config_.rows) {
        throw std::invalid_argument("activation count must equal the array row count");
    }
    for (const Word activation : activations) {
        if (!fitsInBits(activation, activation_bits)) {
            throw std::invalid_argument("activation exceeds activation_bits");
        }
    }

    std::vector<Word> outputs(output_channels_, 0);
    const TimingConfig& timing = simulator_.timing();
    const std::size_t row_group_count =
        config_.rows / timing.max_active_rows +
        static_cast<std::size_t>(config_.rows % timing.max_active_rows != 0);

    for (std::size_t input_bit = 0; input_bit < activation_bits; ++input_bit) {
        for (std::size_t group = 0; group < row_group_count; ++group) {
            const std::size_t row_begin = group * timing.max_active_rows;
            const std::size_t row_end =
                row_begin + std::min(timing.max_active_rows,
                                     config_.rows - row_begin);
            for (std::size_t output = 0; output < output_channels_; ++output) {
                for (std::size_t weight_bit = 0;
                     weight_bit < config_.weight_bits; ++weight_bit) {
                    const std::size_t column =
                        output * config_.weight_bits + weight_bit;
                    Word active_cells = 0;
                    for (std::size_t row = row_begin; row < row_end; ++row) {
                        const Word input_enabled =
                            (activations[row] >> input_bit) & Word{1};
                        active_cells += input_enabled *
                            cells_[row * config_.columns + column];
                    }
                    addShiftedContribution(outputs[output], active_cells,
                                           input_bit + weight_bit);
                }
            }
        }
    }

    const Word conversions_per_group = static_cast<Word>(
        checkedSizeProduct(output_channels_, config_.weight_bits));
    const Word batches_per_group = ceilingDivide(
        conversions_per_group, static_cast<Word>(timing.adc_count));
    const Word cycles_per_group = checkedAdd(
        checkedMultiply(batches_per_group, timing.adc_cycles),
        timing.accumulate_cycles);
    const Word iteration_count = checkedMultiply(
        static_cast<Word>(activation_bits), static_cast<Word>(row_group_count));

    OperationStats stats;
    stats.operation = "vmm";
    stats.cycles = checkedAdd(
        timing.vmm_setup_cycles,
        checkedMultiply(iteration_count, cycles_per_group));
    stats.adc_conversions =
        checkedMultiply(iteration_count, conversions_per_group);
    stats.row_groups = static_cast<Word>(row_group_count);
    stats.input_bits = static_cast<Word>(activation_bits);
    simulator_.record(std::move(stats));
    return outputs;
}

const ArrayConfig& VmmArray::config() const {
    return config_;
}

std::size_t VmmArray::outputChannels() const {
    return output_channels_;
}

bool VmmArray::weightsLoaded() const {
    return weights_loaded_;
}

Bit VmmArray::readCell(std::size_t row, std::size_t column) const {
    if (row >= config_.rows || column >= config_.columns) {
        throw std::out_of_range("cell index is outside the VMM array");
    }
    return cells_[row * config_.columns + column];
}

LogicArray::LogicArray(Simulator& simulator, std::size_t rows,
                       std::size_t columns)
    : simulator_(simulator), rows_(rows), columns_(columns) {
    if (rows_ == 0 || columns_ == 0) {
        throw std::invalid_argument("logic array rows and columns must be positive");
    }
    cells_.assign(checkedSizeProduct(rows_, columns_), 0);
}

void LogicArray::writeRow(std::size_t row, const BitRow& bits) {
    validateRow(row);
    if (bits.size() != columns_) {
        throw std::invalid_argument("logic row width does not match the array");
    }
    for (std::size_t column = 0; column < columns_; ++column) {
        if (bits[column] > 1) {
            throw std::invalid_argument("logic row values must be zero or one");
        }
        cells_[index(row, column)] = bits[column];
    }

    OperationStats stats;
    stats.operation = "row_write";
    stats.cells_written = static_cast<Word>(columns_);
    stats.cycles = checkedAdd(
        simulator_.timing().write_setup_cycles,
        ceilingDivide(stats.cells_written,
                      static_cast<Word>(simulator_.timing().write_ports)));
    simulator_.record(std::move(stats));
}

BitRow LogicArray::readRow(std::size_t row) const {
    validateRow(row);
    BitRow result(columns_);
    for (std::size_t column = 0; column < columns_; ++column) {
        result[column] = cells_[index(row, column)];
    }
    return result;
}

BitRow LogicArray::apply(LogicOperation operation, std::size_t destination,
                         std::size_t left, std::size_t right) {
    validateRow(destination);
    validateRow(left);
    validateRow(right);

    BitRow result(columns_);
    for (std::size_t column = 0; column < columns_; ++column) {
        const Bit left_bit = cells_[index(left, column)];
        const Bit right_bit = cells_[index(right, column)];
        switch (operation) {
            case LogicOperation::And:
                result[column] = static_cast<Bit>(left_bit & right_bit);
                break;
            case LogicOperation::Or:
                result[column] = static_cast<Bit>(left_bit | right_bit);
                break;
            case LogicOperation::Xor:
                result[column] = static_cast<Bit>(left_bit ^ right_bit);
                break;
        }
    }
    for (std::size_t column = 0; column < columns_; ++column) {
        cells_[index(destination, column)] = result[column];
    }

    const TimingConfig& timing = simulator_.timing();
    const Word sense_cycles = operation == LogicOperation::Xor
                                  ? timing.xor_sense_cycles
                                  : timing.boolean_sense_cycles;
    OperationStats stats;
    stats.operation = logicName(operation);
    stats.cycles = checkedAdd(
        checkedAdd(timing.logic_setup_cycles, sense_cycles),
        timing.logic_write_cycles);
    simulator_.record(std::move(stats));
    return result;
}

std::size_t LogicArray::rows() const {
    return rows_;
}

std::size_t LogicArray::columns() const {
    return columns_;
}

std::size_t LogicArray::index(std::size_t row, std::size_t column) const {
    return row * columns_ + column;
}

void LogicArray::validateRow(std::size_t row) const {
    if (row >= rows_) {
        throw std::out_of_range("logic row index is outside the array");
    }
}

}  // namespace cim
