#include "quickxlsx/range.hpp"

#include "quickxlsx/errors.hpp"
#include "quickxlsx/view.hpp"
#include "quickxlsx/worksheet.hpp"

#include <string>

namespace quickxlsx {
namespace {

[[noreturn]] void invalid_range(std::string_view definition, std::string_view reason) {
    throw errors::InvalidRange("invalid range '" + std::string(definition) + "': " + std::string(reason));
}

void validate_cell(const CellRef& cell, std::string_view definition) {
    if (cell.row() >= Range::max_rows) invalid_range(definition, "row exceeds Excel limit 1048576");
    if (cell.column() >= Range::max_columns) invalid_range(definition, "column exceeds Excel limit XFD");
}

} // namespace

Range::Range(const Worksheet& worksheet, std::string_view a1)
    : worksheet_(&worksheet), top_(0), left_(0), bottom_(0), right_(0) {
    if (a1.empty()) invalid_range(a1, "empty reference");
    const std::size_t colon = a1.find(':');
    if (colon != std::string_view::npos && a1.find(':', colon + 1) != std::string_view::npos) {
        invalid_range(a1, "expected one ':' separator at most");
    }
    const std::string_view first_text = colon == std::string_view::npos ? a1 : a1.substr(0, colon);
    const std::string_view last_text = colon == std::string_view::npos ? a1 : a1.substr(colon + 1);
    if (first_text.empty() || last_text.empty()) invalid_range(a1, "missing range endpoint");

    CellRef first(first_text);
    CellRef last(last_text);
    validate_cell(first, a1);
    validate_cell(last, a1);
    if (first.row() > last.row()) invalid_range(a1, "top row follows bottom row");
    if (first.column() > last.column()) invalid_range(a1, "left column follows right column");

    // Public A1 endpoints are inclusive; internal bounds are half-open for size and iteration.
    top_ = first.row();
    left_ = first.column();
    bottom_ = last.row() + 1;
    right_ = last.column() + 1;
}

Range::Range(const Worksheet& worksheet, std::size_t top, std::size_t left,
             std::size_t bottom, std::size_t right)
    : worksheet_(&worksheet), top_(top), left_(left), bottom_(bottom), right_(right) {
    if (top > bottom) throw errors::InvalidRange("invalid range: top exceeds bottom");
    if (left > right) throw errors::InvalidRange("invalid range: left exceeds right");
    if (top == bottom || left == right) throw errors::InvalidRange("invalid range: empty rectangles are not supported");
    if (bottom > max_rows) throw errors::InvalidRange("invalid range: bottom exceeds Excel row limit 1048576");
    if (right > max_columns) throw errors::InvalidRange("invalid range: right exceeds Excel column limit XFD");
}

Value Range::value(std::size_t row, std::size_t column) const {
    if (row >= row_count() || column >= col_count()) {
        throw errors::InvalidRange("relative cell coordinate is outside range " + to_string());
    }
    const Row* source_row = worksheet_->find(top_ + row);
    return source_row == nullptr ? Value{} : (*source_row)[left_ + column];
}

Cell Range::cell(std::size_t row, std::size_t column) const {
    if (column >= col_count()) {
        throw errors::InvalidRange("relative cell coordinate is outside range " + to_string());
    }
    return Cell(column, value(row, column));
}

std::vector<Value> Range::values() const {
    std::vector<Value> output;
    output.reserve(row_count() * col_count());
    for (std::size_t row = 0; row < row_count(); ++row) {
        for (std::size_t column = 0; column < col_count(); ++column) output.push_back(value(row, column));
    }
    return output;
}

std::vector<Cell> Range::cells() const {
    std::vector<Cell> output;
    output.reserve(row_count() * col_count());
    for (std::size_t row = 0; row < row_count(); ++row) {
        for (std::size_t column = 0; column < col_count(); ++column) output.emplace_back(column, value(row, column));
    }
    return output;
}

// Materialized range rows use coordinates relative to the range while source lookup remains
// absolute. Missing sparse cells become owning Null values, never dangling references.
std::vector<Row> Range::rows() const {
    std::vector<Row> output;
    output.reserve(row_count());
    for (std::size_t row = 0; row < row_count(); ++row) {
        Row dense(row);
        for (std::size_t column = 0; column < col_count(); ++column) dense.set(column, value(row, column));
        output.push_back(std::move(dense));
    }
    return output;
}

RowView Range::view() const {
    const Range source = *this;
    return RowView([source] { return source.rows(); });
}

std::string Range::to_string() const {
    const std::string first = CellRef(top_, left_).to_string();
    if (is_single_cell()) return first;
    return first + ':' + CellRef(bottom_ - 1, right_ - 1).to_string();
}

} // namespace quickxlsx
