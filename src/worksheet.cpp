#include "quickxlsx/worksheet.hpp"

#include "quickxlsx/range.hpp"
#include "quickxlsx/view.hpp"

#include <algorithm>
#include <string>

namespace quickxlsx {

bool Worksheet::valid_name(std::string_view name) noexcept {
    if (name.empty() || name.size() > 31 || name.front() == '\'' || name.back() == '\'') return false;
    constexpr std::string_view forbidden = "[]:*?/\\";
    return name.find_first_of(forbidden) == std::string_view::npos;
}

Worksheet::Worksheet(std::string name) : name_(std::move(name)) {
    if (!valid_name(name_)) throw errors::InvalidWorksheetName("invalid worksheet name: " + name_);
}

Worksheet::position Worksheet::lower_bound(std::size_t row) noexcept {
    return std::lower_bound(rows_.begin(), rows_.end(), row,
                            [](const Row& item, std::size_t key) { return item.index() < key; });
}
Worksheet::const_position Worksheet::lower_bound(std::size_t row) const noexcept {
    return std::lower_bound(rows_.begin(), rows_.end(), row,
                            [](const Row& item, std::size_t key) { return item.index() < key; });
}

Row* Worksheet::find(std::size_t row) noexcept {
    const auto position = lower_bound(row);
    return position != rows_.end() && position->index() == row ? &*position : nullptr;
}
const Row* Worksheet::find(std::size_t row) const noexcept {
    const auto position = lower_bound(row);
    return position != rows_.end() && position->index() == row ? &*position : nullptr;
}

Row& Worksheet::operator[](std::size_t row) { return ensure_row(row); }
const Row& Worksheet::operator[](std::size_t row) const { return at(row); }

Row& Worksheet::at(std::size_t row) {
    Row* result = find(row);
    if (result == nullptr) throw errors::InvalidRowIndex("row " + std::to_string(row) + " is not present in worksheet " + name_);
    return *result;
}
const Row& Worksheet::at(std::size_t row) const {
    const Row* result = find(row);
    if (result == nullptr) throw errors::InvalidRowIndex("row " + std::to_string(row) + " is not present in worksheet " + name_);
    return *result;
}

Row& Worksheet::insert(Row row) {
    auto position = lower_bound(row.index());
    if (position != rows_.end() && position->index() == row.index()) {
        *position = std::move(row);
        return *position;
    }
    return *rows_.insert(position, std::move(row));
}

Row& Worksheet::ensure_row(std::size_t row) {
    auto position = lower_bound(row);
    if (position == rows_.end() || position->index() != row) position = rows_.emplace(position, row);
    return *position;
}

bool Worksheet::erase(std::size_t row) noexcept {
    const auto position = lower_bound(row);
    if (position == rows_.end() || position->index() != row) return false;
    rows_.erase(position);
    return true;
}

std::size_t Worksheet::column_count() const noexcept {
    std::size_t result = 0;
    for (const auto& row : rows_) result = std::max(result, row.column_count());
    return result;
}

Range Worksheet::range(std::string_view a1) const { return Range(*this, a1); }

Range Worksheet::range(std::size_t top, std::size_t left,
                       std::size_t bottom, std::size_t right) const {
    return Range(*this, top, left, bottom, right);
}

WorksheetView Worksheet::rows() const { return WorksheetView(*this); }

ValueView Worksheet::column(std::size_t column) const { return rows().column(column); }

RowView Worksheet::columns(const std::vector<std::size_t>& columns) const {
    return rows().columns(columns);
}

} // namespace quickxlsx
