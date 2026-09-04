#include "quickxlsx/workbook.hpp"

#include "quickxlsx/errors.hpp"
#include "quickxlsx/writer.hpp"
#ifdef QUICKXLSX_ENABLE_XLSX
#include "xlsx_format.hpp"
#endif
#include "utf8_path.hpp"

#include <functional>
#include <cctype>
#include <algorithm>
#include <filesystem>

namespace quickxlsx::detail {
namespace {

enum class Format { CSV, XLSX };

Format format_from_path(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    if (extension == ".csv") return Format::CSV;
    if (extension == ".xlsx") {
#ifdef QUICKXLSX_ENABLE_XLSX
        return Format::XLSX;
#else
        throw errors::UnsupportedFeature("XLSX support is disabled: " + path.string());
#endif
    }
    throw errors::UnsupportedFeature("unsupported file format for " + path.string() + "; expected .csv or .xlsx");
}

} // namespace

void stream_csv(const std::filesystem::path&, const CSVConfig&, const std::function<void(Row&&)>&);

Workbook load_workbook(const std::filesystem::path& path,
                       const std::optional<CSVConfig>& csv_config,
                       const std::optional<XLSXConfig>& xlsx_config) {
    if (csv_config && xlsx_config) throw errors::InternalError("conflicting CSV and XLSX configurations");
    const Format format = csv_config ? Format::CSV : xlsx_config ? Format::XLSX : format_from_path(path);
    if (format == Format::XLSX) {
#ifdef QUICKXLSX_ENABLE_XLSX
        return read_xlsx(path, xlsx_config.value_or(XLSXConfig{}));
#else
        throw errors::UnsupportedFeature("XLSX support is disabled: " + path.string());
#endif
    }

    Workbook workbook;
    auto name = utf8_bytes(path.stem());
    if (!Worksheet::valid_name(name)) name = "Sheet1";
    auto& worksheet = workbook.add_sheet(std::move(name));
    stream_csv(path, csv_config.value_or(CSVConfig{}), [&](Row&& row) { worksheet.insert(std::move(row)); });
    return workbook;
}

void save_workbook(const Workbook& workbook, const std::filesystem::path& path) {
    if (format_from_path(path) != Format::XLSX)
        throw errors::UnsupportedFeature("Workbook::save writes XLSX; use export_csv for CSV output");
#ifdef QUICKXLSX_ENABLE_XLSX
    write_xlsx(workbook, path, 6);
#else
    (void)workbook;
    throw errors::UnsupportedFeature("XLSX support is disabled: " + path.string());
#endif
}

void export_worksheet_csv(const Worksheet& worksheet, const std::filesystem::path& path) {
    if (format_from_path(path) != Format::CSV)
        throw errors::UnsupportedFeature("CSV export path must have a .csv extension");
    Writer writer(detail::utf8_bytes(path), CSVConfig{});
    writer.write_sheet(worksheet);
    writer.close();
}

} // namespace quickxlsx::detail
