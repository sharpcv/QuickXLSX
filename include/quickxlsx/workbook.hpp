#pragma once
#include "quickxlsx/config.hpp"
#include "quickxlsx/worksheet.hpp"
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace quickxlsx {
/** Owning DOM workbook containing ordered worksheets. */
class Workbook {
public:
    /** Worksheet storage type. */ using container_type = std::vector<Worksheet>;
    /** Mutable worksheet iterator. */ using iterator = container_type::iterator;
    /** Read-only worksheet iterator. */ using const_iterator = container_type::const_iterator;
    /** Constructs an empty workbook. */ Workbook() = default;
    /** Opens path by extension; may throw file, parse, or UnsupportedFeature errors. */ explicit Workbook(const std::string& path);
    /** Opens CSV or XLSX by extension into memory. */ [[nodiscard]] static Workbook open(const std::string& path);
    /** Opens path as CSV using config. */ [[nodiscard]] static Workbook open(const std::string& path, const CSVConfig& config);
    /** Opens path as XLSX using config; throws UnsupportedFeature in CSV-only builds. */ [[nodiscard]] static Workbook open(const std::string& path, const XLSXConfig& config);
    /** Returns worksheet names in workbook order. */ [[nodiscard]] std::vector<std::string> sheet_names() const;
    /** Returns the worksheet count. */ [[nodiscard]] std::size_t sheet_count() const noexcept { return sheets_.size(); }
    /** Returns whether the workbook has no worksheets. */ [[nodiscard]] bool empty() const noexcept { return sheets_.empty(); }
    /** Finds a case-insensitive name; throws errors::WorksheetNotFound when absent. */ [[nodiscard]] Worksheet& sheet(std::string_view name);
    /** Finds a case-insensitive name; throws errors::WorksheetNotFound when absent. */ [[nodiscard]] const Worksheet& sheet(std::string_view name) const;
    /** Returns an indexed sheet; throws errors::WorksheetNotFound when out of range. */ [[nodiscard]] Worksheet& sheet(std::size_t index);
    /** Returns an indexed sheet; throws errors::WorksheetNotFound when out of range. */ [[nodiscard]] const Worksheet& sheet(std::size_t index) const;
    /** Tests for a case-insensitive worksheet name. */ [[nodiscard]] bool contains_sheet(std::string_view name) const noexcept;
    /** Appends a sheet; throws errors::InvalidWorksheetName for invalid or duplicate names. */ Worksheet& add_sheet(std::string name);
    /** Removes a case-insensitive name and reports whether it existed. */ bool remove_sheet(std::string_view name);
    /** Returns first mutable worksheet iterator; mutation can invalidate it. */ [[nodiscard]] iterator begin() noexcept { return sheets_.begin(); }
    /** Returns mutable worksheet sentinel. */ [[nodiscard]] iterator end() noexcept { return sheets_.end(); }
    /** Returns first read-only worksheet iterator. */ [[nodiscard]] const_iterator begin() const noexcept { return sheets_.begin(); }
    /** Returns read-only worksheet sentinel. */ [[nodiscard]] const_iterator end() const noexcept { return sheets_.end(); }
    /** Returns first read-only worksheet iterator. */ [[nodiscard]] const_iterator cbegin() const noexcept { return sheets_.cbegin(); }
    /** Returns read-only worksheet sentinel. */ [[nodiscard]] const_iterator cend() const noexcept { return sheets_.cend(); }
    /** Saves XLSX; may throw UnsupportedFeature, file, ZIP, XML, or write errors. */ void save(const std::string& path) const;
    /** Exports a named sheet as CSV; throws WorksheetNotFound or file/write errors. */ void export_csv(std::string_view sheet_name, const std::string& csv_path) const;
    /** Exports an indexed sheet as CSV; throws WorksheetNotFound or file/write errors. */ void export_csv(std::size_t sheet_index, const std::string& csv_path) const;
private: container_type sheets_;
};
namespace detail {
[[nodiscard]] Workbook load_workbook(const std::filesystem::path&, const std::optional<CSVConfig>&, const std::optional<XLSXConfig>&);
void save_workbook(const Workbook&, const std::filesystem::path&);
void export_worksheet_csv(const Worksheet&, const std::filesystem::path&);
} // namespace detail
} // namespace quickxlsx
