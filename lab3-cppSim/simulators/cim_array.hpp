#ifndef CIM_ARRAY_HPP
#define CIM_ARRAY_HPP

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

// The standalone CIM array computes one signed sum per column.
class CimArray {
public:
    CimArray(std::size_t rows, std::size_t cols, std::vector<std::int8_t> weights)
        : rows_(rows), cols_(cols), weights_(std::move(weights)) {
        if (!rows || !cols || rows > weights_.max_size() / cols || weights_.size() != rows * cols)
            throw std::invalid_argument("weight array dimensions do not match the data");
    }

    std::size_t rows() const { return rows_; }
    std::size_t cols() const { return cols_; }

    std::vector<std::int64_t> multiply(const std::vector<std::int8_t>& x) const {
        if (x.size() != rows_) throw std::invalid_argument("activation length mismatch");
        std::vector<std::int64_t> y(cols_, 0);
        for (std::size_t r = 0; r < rows_; ++r) {
            for (std::size_t c = 0; c < cols_; ++c) {
                y[c] += std::int64_t(x[r]) * weights_[r * cols_ + c];
            }
        }
        return y;
    }

private:
    std::size_t rows_, cols_;
    std::vector<std::int8_t> weights_;
};

#endif
