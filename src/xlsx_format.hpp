#pragma once

#include "quickxlsx/config.hpp"
#include "quickxlsx/row.hpp"
#include "quickxlsx/workbook.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace quickxlsx::detail {

[[nodiscard]] Workbook read_xlsx(const std::filesystem::path& path, const XLSXConfig& config);
void stream_xlsx(const std::filesystem::path& path, std::string_view sheet_name,
                 const XLSXConfig& config, const std::function<void(const Row&)>& callback);
[[nodiscard]] std::vector<std::string> xlsx_sheet_names(const std::filesystem::path& path);
void write_xlsx(const Workbook& workbook, const std::filesystem::path& path, int compression_level);

class XlsxStreamWriter {
public:
    XlsxStreamWriter(const std::filesystem::path& path, std::string sheet_name,
                     int compression_level);
    ~XlsxStreamWriter();
    XlsxStreamWriter(const XlsxStreamWriter&) = delete;
    XlsxStreamWriter& operator=(const XlsxStreamWriter&) = delete;
    void write_row(const Row& row);
    void flush();
    void close();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace quickxlsx::detail

namespace quickxlsx::detail::xlsx_internal {

inline constexpr std::uint64_t max_xlsx_part_uncompressed_bytes = 8ULL * 1024 * 1024 * 1024;
inline constexpr std::uint64_t max_xlsx_package_uncompressed_bytes = 16ULL * 1024 * 1024 * 1024;
inline constexpr std::size_t max_xlsx_rows = 1'048'576;
inline constexpr std::size_t max_xlsx_columns = 16'384;

class ZipReader {
public:
    explicit ZipReader(const std::filesystem::path& path);
    ~ZipReader();
    ZipReader(const ZipReader&) = delete;
    ZipReader& operator=(const ZipReader&) = delete;

    [[nodiscard]] bool contains(std::string_view name);
    void read_chunks(std::string_view name,
                     const std::function<bool(std::string_view)>& callback);
    [[nodiscard]] std::string read(std::string_view name);
private:
    void* handle_ = nullptr;
    std::filesystem::path path_;
    std::uint64_t total_uncompressed_bytes_ = 0;
};

class ZipWriter {
public:
    ZipWriter(const std::filesystem::path& path, int compression_level);
    ~ZipWriter();
    ZipWriter(const ZipWriter&) = delete;
    ZipWriter& operator=(const ZipWriter&) = delete;

    void begin_entry(std::string_view name);
    void write_chunk(std::string_view contents);
    void end_entry();
    void add(std::string_view name, std::string_view contents);
    void close();

private:
    void* handle_ = nullptr;
    std::filesystem::path path_;
    int compression_level_ = 0;
    std::string current_entry_;
    std::uint64_t current_entry_bytes_ = 0;
    std::uint64_t total_uncompressed_bytes_ = 0;
    bool entry_open_ = false;
};

struct SheetPart {
    std::string name;
    std::string path;
};

struct PackageIndex {
    std::vector<SheetPart> sheets;
    std::string shared_strings_path;
};

[[nodiscard]] PackageIndex read_package_index(ZipReader& archive);
[[nodiscard]] std::vector<std::string> read_shared_strings(ZipReader& archive,
                                                           std::string_view part_path);

} // namespace quickxlsx::detail::xlsx_internal
