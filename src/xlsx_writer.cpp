#include "xlsx_format.hpp"

#include "quickxlsx/errors.hpp"

#include <pugixml.hpp>

#include <atomic>
#include <chrono>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>
#include <utility>

namespace quickxlsx::detail {
namespace {
using xlsx_internal::ZipWriter;

constexpr const char* spreadsheet_namespace =
    "http://schemas.openxmlformats.org/spreadsheetml/2006/main";
constexpr const char* relationships_namespace =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships";
constexpr const char* package_relationships_namespace =
    "http://schemas.openxmlformats.org/package/2006/relationships";
constexpr const char* content_types_namespace =
    "http://schemas.openxmlformats.org/package/2006/content-types";

std::string serialize(const pugi::xml_document& document) {
    std::ostringstream output;
    document.save(output, "", pugi::format_raw, pugi::encoding_utf8);
    return std::move(output).str();
}

void append_declaration(pugi::xml_document& document) {
    pugi::xml_node declaration = document.append_child(pugi::node_declaration);
    declaration.append_attribute("version") = "1.0";
    declaration.append_attribute("encoding") = "UTF-8";
    declaration.append_attribute("standalone") = "yes";
}

bool valid_xml_character(uint32_t codepoint) {
    return codepoint == 0x9 || codepoint == 0xA || codepoint == 0xD ||
           (codepoint >= 0x20 && codepoint <= 0xD7FF) ||
           (codepoint >= 0xE000 && codepoint <= 0xFFFD) ||
           (codepoint >= 0x10000 && codepoint <= 0x10FFFF);
}

void validate_xml_text(std::string_view value, std::string_view context) {
    for (std::size_t offset = 0; offset < value.size();) {
        const unsigned char first = static_cast<unsigned char>(value[offset]);
        uint32_t codepoint = 0;
        std::size_t count = 0;
        if (first < 0x80) { codepoint = first; count = 1; }
        else if ((first & 0xE0) == 0xC0) { codepoint = first & 0x1F; count = 2; }
        else if ((first & 0xF0) == 0xE0) { codepoint = first & 0x0F; count = 3; }
        else if ((first & 0xF8) == 0xF0) { codepoint = first & 0x07; count = 4; }
        else throw errors::InvalidUTF8("invalid UTF-8 in " + std::string(context));
        if (offset + count > value.size()) throw errors::InvalidUTF8("truncated UTF-8 in " + std::string(context));
        for (std::size_t index = 1; index < count; ++index) {
            const unsigned char continuation = static_cast<unsigned char>(value[offset + index]);
            if ((continuation & 0xC0) != 0x80) throw errors::InvalidUTF8("invalid UTF-8 in " + std::string(context));
            codepoint = (codepoint << 6) | (continuation & 0x3F);
        }
        const uint32_t minimum = count == 1 ? 0 : count == 2 ? 0x80 : count == 3 ? 0x800 : 0x10000;
        if (codepoint < minimum || (codepoint >= 0xD800 && codepoint <= 0xDFFF) || codepoint > 0x10FFFF)
            throw errors::InvalidUTF8("non-canonical UTF-8 in " + std::string(context));
        if (!valid_xml_character(codepoint))
            throw errors::WriteError("character forbidden by XML 1.0 in " + std::string(context));
        offset += count;
    }
}

std::string column_name(std::size_t column) {
    std::string result;
    do {
        const std::size_t remainder = column % 26;
        result.insert(result.begin(), static_cast<char>('A' + remainder));
        column = column / 26;
        if (column == 0) break;
        --column;
    } while (true);
    return result;
}

std::string cell_reference(std::size_t row, std::size_t column) {
    if (row >= xlsx_internal::max_xlsx_rows)
        throw errors::WriteError("row index " + std::to_string(row) + " exceeds Excel's 1,048,576-row limit");
    if (column >= xlsx_internal::max_xlsx_columns)
        throw errors::WriteError("column index " + std::to_string(column) + " exceeds Excel's XFD limit");
    return column_name(column) + std::to_string(row + 1);
}

void append_value(pugi::xml_node cell, const Value& value, std::string_view reference) {
    switch (value.type()) {
    case ValueType::Null:
        return;
    case ValueType::Boolean:
        cell.append_attribute("t") = "b";
        cell.append_child("v").text().set(value.as_bool_unchecked() ? "1" : "0");
        return;
    case ValueType::Integer:
        cell.append_child("v").text().set(std::to_string(value.as_int64_unchecked()).c_str());
        return;
    case ValueType::Double: {
        const double number = value.as_double_unchecked();
        if (!std::isfinite(number))
            throw errors::WriteError("non-finite number cannot be written to XLSX cell " + std::string(reference));
        char buffer[64];
        const auto formatted = std::to_chars(buffer, buffer + sizeof(buffer), number,
                                             std::chars_format::general);
        if (formatted.ec != std::errc{})
            throw errors::WriteError("failed to format XLSX numeric cell " + std::string(reference));
        cell.append_child("v").append_child(pugi::node_pcdata).set_value(buffer,
            static_cast<std::size_t>(formatted.ptr - buffer));
        return;
    }
    case ValueType::String: {
        const std::string_view text = value.as_string_unchecked();
        validate_xml_text(text, "XLSX cell " + std::string(reference));
        cell.append_attribute("t") = "inlineStr";
        pugi::xml_node text_node = cell.append_child("is").append_child("t");
        text_node.append_attribute("xml:space") = "preserve";
        text_node.append_child(pugi::node_pcdata).set_value(text.data(), text.size());
        return;
    }
    case ValueType::DateTime: {
        const double serial = value.as_double();
        if (!std::isfinite(serial))
            throw errors::WriteError("non-finite date serial cannot be written to XLSX cell " + std::string(reference));
        char buffer[64];
        const auto formatted = std::to_chars(buffer, buffer + sizeof(buffer), serial,
                                             std::chars_format::general);
        if (formatted.ec != std::errc{})
            throw errors::WriteError("failed to format XLSX date cell " + std::string(reference));
        cell.append_child("v").append_child(pugi::node_pcdata).set_value(buffer,
            static_cast<std::size_t>(formatted.ptr - buffer));
        return;
    }
    }
    throw errors::InternalError("unknown ValueType while writing XLSX");
}

std::string worksheet_xml(const Worksheet& worksheet) {
    pugi::xml_document document;
    append_declaration(document);
    pugi::xml_node root = document.append_child("worksheet");
    root.append_attribute("xmlns") = spreadsheet_namespace;
    pugi::xml_node sheet_data = root.append_child("sheetData");
    for (const Row& row : worksheet) {
        if (row.index() >= xlsx_internal::max_xlsx_rows)
            throw errors::WriteError("row index " + std::to_string(row.index()) + " exceeds Excel's 1,048,576-row limit in worksheet '" + worksheet.name() + "'");
        pugi::xml_node row_node = sheet_data.append_child("row");
        row_node.append_attribute("r").set_value(static_cast<unsigned long long>(row.index() + 1));
        for (const Cell& source_cell : row) {
            if (source_cell.column() >= xlsx_internal::max_xlsx_columns)
                throw errors::WriteError("column index " + std::to_string(source_cell.column()) + " exceeds Excel's XFD limit in worksheet '" + worksheet.name() + "'");
            const std::string reference = cell_reference(row.index(), source_cell.column());
            if (source_cell.value().is_null()) continue;
            pugi::xml_node cell = row_node.append_child("c");
            cell.append_attribute("r") = reference.c_str();
            append_value(cell, source_cell.value(), reference);
        }
    }
    return serialize(document);
}

std::string row_xml(const Row& row, std::string_view sheet_name) {
    if (row.index() >= xlsx_internal::max_xlsx_rows)
        throw errors::WriteError("row index " + std::to_string(row.index()) +
            " exceeds Excel's 1,048,576-row limit in worksheet '" + std::string(sheet_name) + "'");
    pugi::xml_document document;
    pugi::xml_node row_node = document.append_child("row");
    row_node.append_attribute("r").set_value(static_cast<unsigned long long>(row.index() + 1));
    for (const Cell& source_cell : row) {
        if (source_cell.column() >= xlsx_internal::max_xlsx_columns)
            throw errors::WriteError("column index " + std::to_string(source_cell.column()) +
                " exceeds Excel's XFD limit in worksheet '" + std::string(sheet_name) + "'");
        const std::string reference = cell_reference(row.index(), source_cell.column());
        if (source_cell.value().is_null()) continue;
        pugi::xml_node cell = row_node.append_child("c");
        cell.append_attribute("r") = reference.c_str();
        append_value(cell, source_cell.value(), reference);
    }
    return serialize(document);
}

std::string workbook_xml(const Workbook& workbook) {
    pugi::xml_document document;
    append_declaration(document);
    pugi::xml_node root = document.append_child("workbook");
    root.append_attribute("xmlns") = spreadsheet_namespace;
    root.append_attribute("xmlns:r") = relationships_namespace;
    pugi::xml_node sheets = root.append_child("sheets");
    std::size_t index = 0;
    for (const Worksheet& worksheet : workbook) {
        validate_xml_text(worksheet.name(), "worksheet name");
        pugi::xml_node sheet = sheets.append_child("sheet");
        sheet.append_attribute("name") = worksheet.name().c_str();
        sheet.append_attribute("sheetId").set_value(static_cast<unsigned long long>(index + 1));
        const std::string id = "rId" + std::to_string(index + 1);
        sheet.append_attribute("r:id") = id.c_str();
        ++index;
    }
    return serialize(document);
}

std::string workbook_relationships_xml(const Workbook& workbook) {
    pugi::xml_document document;
    append_declaration(document);
    pugi::xml_node root = document.append_child("Relationships");
    root.append_attribute("xmlns") = package_relationships_namespace;
    std::size_t index = 0;
    for (const Worksheet& worksheet : workbook) {
        static_cast<void>(worksheet);
        pugi::xml_node relationship = root.append_child("Relationship");
        const std::string id = "rId" + std::to_string(index + 1);
        const std::string target = "worksheets/sheet" + std::to_string(index + 1) + ".xml";
        relationship.append_attribute("Id") = id.c_str();
        relationship.append_attribute("Type") =
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet";
        relationship.append_attribute("Target") = target.c_str();
        ++index;
    }
    return serialize(document);
}

std::string root_relationships_xml() {
    pugi::xml_document document;
    append_declaration(document);
    pugi::xml_node root = document.append_child("Relationships");
    root.append_attribute("xmlns") = package_relationships_namespace;
    pugi::xml_node relationship = root.append_child("Relationship");
    relationship.append_attribute("Id") = "rId1";
    relationship.append_attribute("Type") =
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument";
    relationship.append_attribute("Target") = "xl/workbook.xml";
    return serialize(document);
}

std::string content_types_xml(const Workbook& workbook) {
    pugi::xml_document document;
    append_declaration(document);
    pugi::xml_node root = document.append_child("Types");
    root.append_attribute("xmlns") = content_types_namespace;
    pugi::xml_node rels = root.append_child("Default");
    rels.append_attribute("Extension") = "rels";
    rels.append_attribute("ContentType") = "application/vnd.openxmlformats-package.relationships+xml";
    pugi::xml_node xml = root.append_child("Default");
    xml.append_attribute("Extension") = "xml";
    xml.append_attribute("ContentType") = "application/xml";
    pugi::xml_node workbook_override = root.append_child("Override");
    workbook_override.append_attribute("PartName") = "/xl/workbook.xml";
    workbook_override.append_attribute("ContentType") =
        "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml";
    std::size_t index = 0;
    for (const Worksheet& worksheet : workbook) {
        static_cast<void>(worksheet);
        pugi::xml_node sheet = root.append_child("Override");
        const std::string part = "/xl/worksheets/sheet" + std::to_string(index + 1) + ".xml";
        sheet.append_attribute("PartName") = part.c_str();
        sheet.append_attribute("ContentType") =
            "application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml";
        ++index;
    }
    return serialize(document);
}

std::filesystem::path temporary_path_for(const std::filesystem::path& destination) {
    static std::atomic<std::uint64_t> sequence{0};
    const auto stamp = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path directory = destination.parent_path().empty() ?
        std::filesystem::path(".") : destination.parent_path();
    const std::string base = destination.filename().string();
    for (std::uint64_t attempt = 0; attempt < 64; ++attempt) {
        const auto nonce = stamp ^ sequence.fetch_add(1, std::memory_order_relaxed) ^ attempt;
        const auto candidate = directory / ("." + base + ".quickxlsx-" + std::to_string(nonce) + ".tmp");
        std::error_code error;
        if (!std::filesystem::exists(candidate, error) && !error) return candidate;
    }
    throw errors::WriteError("could not allocate a temporary XLSX path beside '" + destination.string() + "'");
}

// The package is built beside the destination and published only after every ZIP entry closes.
// TemporaryFile removes an incomplete archive on all failure paths, preserving an existing file.
class TemporaryFile {
public:
    explicit TemporaryFile(std::filesystem::path path) : path_(std::move(path)) {}
    ~TemporaryFile() {
        if (!committed_) {
            std::error_code ignored;
            std::filesystem::remove(path_, ignored);
        }
    }
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
    void commit(const std::filesystem::path& destination) {
        std::error_code error;
        std::filesystem::rename(path_, destination, error);
        if (error)
            throw errors::WriteError("failed to replace XLSX destination '" + destination.string() +
                                     "' with completed temporary archive: " + error.message());
        committed_ = true;
    }
private:
    std::filesystem::path path_;
    bool committed_ = false;
};

} // namespace

// Metadata is small and retained; worksheet rows are serialized directly into the open ZIP entry.
// The only row-sized allocation is row_xml(), which is released before the next callback.
struct XlsxStreamWriter::Impl {
    std::filesystem::path destination;
    std::string sheet_name;
    TemporaryFile temporary;
    ZipWriter archive;
    std::size_t next_row = 0;
    bool closed = false;

    Impl(const std::filesystem::path& path, std::string name, int compression_level)
        : destination(path), sheet_name(std::move(name)), temporary(temporary_path_for(path)),
          archive(temporary.path(), compression_level) {
        if (!Worksheet::valid_name(sheet_name))
            throw errors::InvalidWorksheetName("invalid worksheet name: " + sheet_name);
        validate_xml_text(sheet_name, "worksheet name");
        Workbook metadata;
        metadata.add_sheet(sheet_name);
        archive.add("[Content_Types].xml", content_types_xml(metadata));
        archive.add("_rels/.rels", root_relationships_xml());
        archive.add("xl/workbook.xml", workbook_xml(metadata));
        archive.add("xl/_rels/workbook.xml.rels", workbook_relationships_xml(metadata));
        archive.begin_entry("xl/worksheets/sheet1.xml");
        archive.write_chunk("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                            "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"><sheetData>");
    }

    // Reject regressions before serialization so sparse row coordinates remain monotonic and no
    // partially invalid row is emitted into an otherwise valid package.
    void write(const Row& row) {
        if (closed) throw errors::WriteError("cannot write a row to a closed XLSX stream");
        if (row.index() < next_row)
            throw errors::WriteError("row index " + std::to_string(row.index()) +
                " precedes next output row " + std::to_string(next_row));
        const std::string xml = row_xml(row, sheet_name);
        archive.write_chunk(xml);
        next_row = row.index() + 1;
    }

    void close() {
        if (closed) return;
        archive.write_chunk("</sheetData></worksheet>");
        archive.end_entry();
        archive.close();
        temporary.commit(destination);
        closed = true;
    }
};

XlsxStreamWriter::XlsxStreamWriter(const std::filesystem::path& path, std::string sheet_name,
                                   int compression_level)
    : impl_(std::make_unique<Impl>(path, std::move(sheet_name), compression_level)) {}

XlsxStreamWriter::~XlsxStreamWriter() = default;

void XlsxStreamWriter::write_row(const Row& row) { impl_->write(row); }

void XlsxStreamWriter::flush() {
    if (!impl_ || impl_->closed) throw errors::WriteError("cannot flush a closed XLSX stream");
}

void XlsxStreamWriter::close() { if (impl_) impl_->close(); }

void write_xlsx(const Workbook& workbook, const std::filesystem::path& path, int compression_level) {
    if (workbook.empty()) throw errors::WriteError("cannot write an XLSX workbook with no worksheets");

    // Materialize and validate every XML part before creating any output file.
    const std::string content_types = content_types_xml(workbook);
    const std::string root_relationships = root_relationships_xml();
    const std::string workbook_part = workbook_xml(workbook);
    const std::string workbook_relationships = workbook_relationships_xml(workbook);
    std::vector<std::string> worksheet_parts;
    worksheet_parts.reserve(workbook.sheet_count());
    for (const Worksheet& worksheet : workbook) worksheet_parts.push_back(worksheet_xml(worksheet));

    TemporaryFile temporary(temporary_path_for(path));
    ZipWriter archive(temporary.path(), compression_level);
    archive.add("[Content_Types].xml", content_types);
    archive.add("_rels/.rels", root_relationships);
    archive.add("xl/workbook.xml", workbook_part);
    archive.add("xl/_rels/workbook.xml.rels", workbook_relationships);
    for (std::size_t index = 0; index < worksheet_parts.size(); ++index)
        archive.add("xl/worksheets/sheet" + std::to_string(index + 1) + ".xml", worksheet_parts[index]);
    archive.close();
    temporary.commit(path);
}

} // namespace quickxlsx::detail
