#include "quickxlsx/workbook.hpp"

#include "quickxlsx/errors.hpp"
#include "utf8_path.hpp"

#include <algorithm>
#include <string>

namespace quickxlsx {

Workbook::Workbook(const std::string& path) : Workbook(open(path)) {}

Workbook Workbook::open(const std::string& path) {
    return detail::load_workbook(detail::path_from_utf8(path), std::nullopt, std::nullopt);
}
Workbook Workbook::open(const std::string& path, const CSVConfig& config) {
    return detail::load_workbook(detail::path_from_utf8(path), config, std::nullopt);
}
Workbook Workbook::open(const std::string& path, const XLSXConfig& config) {
    return detail::load_workbook(detail::path_from_utf8(path), std::nullopt, config);
}

namespace {

constexpr char ascii_lower(char value) noexcept {
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
}

bool sheet_name_equal(std::string_view left, std::string_view right) noexcept {
    if (left.size() != right.size()) return false;
    return std::equal(left.begin(), left.end(), right.begin(),
                      [](char a, char b) { return ascii_lower(a) == ascii_lower(b); });
}

} // namespace

std::vector<std::string> Workbook::sheet_names() const {
    std::vector<std::string> result;
    result.reserve(sheets_.size());
    for (const auto& worksheet : sheets_) result.push_back(worksheet.name());
    return result;
}

bool Workbook::contains_sheet(std::string_view name) const noexcept {
    return std::any_of(sheets_.begin(), sheets_.end(), [name](const Worksheet& worksheet) {
        return sheet_name_equal(worksheet.name(), name);
    });
}

Worksheet& Workbook::sheet(std::string_view name) {
    const auto position = std::find_if(sheets_.begin(), sheets_.end(), [name](const Worksheet& worksheet) {
        return sheet_name_equal(worksheet.name(), name);
    });
    if (position == sheets_.end()) throw errors::WorksheetNotFound("worksheet not found: " + std::string(name));
    return *position;
}
const Worksheet& Workbook::sheet(std::string_view name) const {
    const auto position = std::find_if(sheets_.begin(), sheets_.end(), [name](const Worksheet& worksheet) {
        return sheet_name_equal(worksheet.name(), name);
    });
    if (position == sheets_.end()) throw errors::WorksheetNotFound("worksheet not found: " + std::string(name));
    return *position;
}

Worksheet& Workbook::sheet(std::size_t index) {
    if (index >= sheets_.size()) throw errors::WorksheetNotFound("worksheet index out of range: " + std::to_string(index));
    return sheets_[index];
}
const Worksheet& Workbook::sheet(std::size_t index) const {
    if (index >= sheets_.size()) throw errors::WorksheetNotFound("worksheet index out of range: " + std::to_string(index));
    return sheets_[index];
}

Worksheet& Workbook::add_sheet(std::string name) {
    if (contains_sheet(name)) throw errors::InvalidWorksheetName("duplicate worksheet name: " + name);
    sheets_.emplace_back(std::move(name));
    return sheets_.back();
}

bool Workbook::remove_sheet(std::string_view name) {
    const auto position = std::find_if(sheets_.begin(), sheets_.end(), [name](const Worksheet& worksheet) {
        return sheet_name_equal(worksheet.name(), name);
    });
    if (position == sheets_.end()) return false;
    sheets_.erase(position);
    return true;
}

void Workbook::save(const std::string& path) const {
    detail::save_workbook(*this, detail::path_from_utf8(path));
}
void Workbook::export_csv(std::string_view name, const std::string& path) const {
    detail::export_worksheet_csv(sheet(name), detail::path_from_utf8(path));
}
void Workbook::export_csv(std::size_t index, const std::string& path) const {
    detail::export_worksheet_csv(sheet(index), detail::path_from_utf8(path));
}

} // namespace quickxlsx
