#include "quickxlsx/cell.hpp"

#include "quickxlsx/errors.hpp"

#include <limits>

namespace quickxlsx {

CellRef::CellRef(std::size_t row, std::size_t column) : row_(row), column_(column) {
    if (row >= max_rows || column >= max_columns) {
        throw errors::InvalidRange("cell coordinates exceed Excel worksheet limits");
    }
}

CellRef::CellRef(std::string_view a1) {
    if (a1.empty()) throw errors::InvalidRange("empty cell reference");
    std::size_t split = 0;
    while (split < a1.size() && a1[split] >= 'A' && a1[split] <= 'Z') ++split;
    if (split == 0 || split == a1.size() || a1[split] == '0') {
        throw errors::InvalidRange("invalid cell reference: " + std::string(a1));
    }
    std::size_t column = 0;
    for (std::size_t i = 0; i < split; ++i) {
        const std::size_t digit = static_cast<std::size_t>(a1[i] - 'A' + 1);
        if (column > (std::numeric_limits<std::size_t>::max() - digit) / 26) {
            throw errors::InvalidRange("cell column overflows: " + std::string(a1));
        }
        column = column * 26 + digit;
    }
    std::size_t row = 0;
    for (std::size_t i = split; i < a1.size(); ++i) {
        if (a1[i] < '0' || a1[i] > '9') throw errors::InvalidRange("invalid cell reference: " + std::string(a1));
        const std::size_t digit = static_cast<std::size_t>(a1[i] - '0');
        if (row > (std::numeric_limits<std::size_t>::max() - digit) / 10) {
            throw errors::InvalidRange("cell row overflows: " + std::string(a1));
        }
        row = row * 10 + digit;
    }
    if (row == 0) throw errors::InvalidRange("cell rows are one-based: " + std::string(a1));
    if (row > max_rows || column > max_columns) {
        throw errors::InvalidRange("cell reference exceeds Excel worksheet limits: " + std::string(a1));
    }
    row_ = row - 1;
    column_ = column - 1;
}

std::string CellRef::to_string() const {
    std::string column;
    std::size_t value = column_;
    do {
        column.insert(column.begin(), static_cast<char>('A' + value % 26));
        if (value < 26) break;
        value = value / 26 - 1;
    } while (true);
    return column + std::to_string(row_ + 1);
}

std::string Cell::as_string() const { return value_.as_string(); }
std::int64_t Cell::as_int64(std::int64_t fallback) const noexcept { return value_.as_int64(fallback); }
double Cell::as_double(double fallback) const noexcept { return value_.as_double(fallback); }
bool Cell::as_bool(bool fallback) const noexcept { return value_.as_bool(fallback); }

} // namespace quickxlsx
