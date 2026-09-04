#include "quickxlsx/row.hpp"

#include <algorithm>
#include <string>

namespace quickxlsx {

Row::Row(std::size_t index, std::initializer_list<Value> values) : index_(index) {
    cells_.reserve(values.size());
    std::size_t column = 0;
    for (const auto& value : values) cells_.emplace_back(column++, value);
}

std::size_t Row::column_count() const noexcept {
    return cells_.empty() ? 0 : cells_.back().column() + 1;
}

Row::position Row::lower_bound(std::size_t column) noexcept {
    return std::lower_bound(cells_.begin(), cells_.end(), column,
                            [](const Cell& cell, std::size_t key) { return cell.column() < key; });
}

Row::const_position Row::lower_bound(std::size_t column) const noexcept {
    return std::lower_bound(cells_.begin(), cells_.end(), column,
                            [](const Cell& cell, std::size_t key) { return cell.column() < key; });
}

const Cell* Row::find(std::size_t column) const noexcept {
    const auto position = lower_bound(column);
    return position != cells_.end() && position->column() == column ? &*position : nullptr;
}

Cell* Row::find(std::size_t column) noexcept {
    const auto position = lower_bound(column);
    return position != cells_.end() && position->column() == column ? &*position : nullptr;
}

Value Row::operator[](std::size_t column) const {
    const Cell* cell = find(column);
    return cell == nullptr ? Value::null() : cell->value();
}

Cell& Row::operator[](std::size_t column) {
    auto position = lower_bound(column);
    if (position == cells_.end() || position->column() != column) {
        position = cells_.emplace(position, column, Value::null());
    }
    return *position;
}

Value Row::at(std::size_t column) const {
    const Cell* cell = find(column);
    if (cell == nullptr) {
        throw errors::InvalidColumnIndex("column " + std::to_string(column) +
                                         " is not present in sparse row " + std::to_string(index_));
    }
    return cell->value();
}

Cell& Row::set(std::size_t column, Value value) {
    auto position = lower_bound(column);
    if (position != cells_.end() && position->column() == column) {
        position->set_value(std::move(value));
        return *position;
    }
    return *cells_.emplace(position, column, std::move(value));
}

bool Row::erase(std::size_t column) noexcept {
    const auto position = lower_bound(column);
    if (position == cells_.end() || position->column() != column) return false;
    cells_.erase(position);
    return true;
}

std::vector<Value> Row::values() const {
    std::vector<Value> result(column_count());
    for (const auto& cell : cells_) result[cell.column()] = cell.value();
    return result;
}

} // namespace quickxlsx
