#include "xlsx_format.hpp"

#include "quickxlsx/errors.hpp"

#include <pugixml.hpp>

#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <algorithm>

namespace quickxlsx::detail {
namespace {
using xlsx_internal::PackageIndex;
using xlsx_internal::SheetPart;
using xlsx_internal::max_xlsx_columns;
using xlsx_internal::max_xlsx_rows;
using xlsx_internal::ZipReader;

std::string_view local_name(const char* name) {
    const std::string_view value(name == nullptr ? "" : name);
    const std::size_t colon = value.find(':');
    return colon == std::string_view::npos ? value : value.substr(colon + 1);
}

pugi::xml_node child_named(pugi::xml_node parent, std::string_view wanted) {
    for (const auto child : parent.children())
        if (local_name(child.name()) == wanted) return child;
    return {};
}

std::string attribute(pugi::xml_node node, std::string_view wanted) {
    for (const auto item : node.attributes())
        if (local_name(item.name()) == wanted) return item.value();
    return {};
}

void append_text_nodes(pugi::xml_node parent, std::string& text) {
    for (const auto child : parent.children()) {
        if (local_name(child.name()) == "t") text += child.child_value();
        else append_text_nodes(child, text);
    }
}

pugi::xml_document parse_xml(std::string_view xml, std::string_view part) {
    pugi::xml_document document;
    const pugi::xml_parse_result result = document.load_buffer(xml.data(), xml.size(),
        pugi::parse_default | pugi::parse_ws_pcdata, pugi::encoding_utf8);
    if (!result)
        throw errors::XLSXParseError("malformed worksheet XML in '" + std::string(part) +
            "' at byte " + std::to_string(result.offset) + ": " + result.description(),
            std::string(part), 0, result.offset < 0 ? 0 : static_cast<std::size_t>(result.offset));
    return document;
}

std::size_t parse_positive_index(std::string_view text, std::string_view what,
                                 std::string_view part, std::size_t maximum) {
    uint64_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        value == 0 || value > maximum)
        throw errors::XLSXParseError("invalid " + std::string(what) + " '" + std::string(text) +
                                     "' in XLSX part '" + std::string(part) + "'", std::string(part));
    return static_cast<std::size_t>(value - 1);
}

std::pair<std::size_t, std::size_t> parse_cell_reference(std::string_view reference,
                                                         std::string_view part) {
    std::size_t position = 0;
    uint64_t column = 0;
    while (position < reference.size() && reference[position] >= 'A' && reference[position] <= 'Z') {
        const uint64_t digit = static_cast<unsigned>(reference[position] - 'A' + 1);
        if (column > (static_cast<uint64_t>(max_xlsx_columns) - digit) / 26)
            throw errors::XLSXParseError("cell column exceeds Excel's XFD limit in reference '" + std::string(reference) + "'", std::string(part));
        column = column * 26 + digit;
        ++position;
    }
    if (position == 0 || position == reference.size())
        throw errors::XLSXParseError("invalid A1 cell reference '" + std::string(reference) + "'", std::string(part));
    const std::size_t row = parse_positive_index(reference.substr(position), "cell row", part, max_xlsx_rows);
    return {row, static_cast<std::size_t>(column - 1)};
}

Value parse_number(std::string_view text, std::string_view reference, std::string_view part) {
    std::int64_t integer = 0;
    const auto int_result = std::from_chars(text.data(), text.data() + text.size(), integer);
    if (!text.empty() && int_result.ec == std::errc{} && int_result.ptr == text.data() + text.size())
        return Value::integer(integer);
    double number = 0.0;
    const auto double_result = std::from_chars(text.data(), text.data() + text.size(), number,
                                               std::chars_format::general);
    if (text.empty() || double_result.ec != std::errc{} ||
        double_result.ptr != text.data() + text.size() || !std::isfinite(number))
        throw errors::XLSXParseError("invalid numeric value '" + std::string(text) +
            "' in cell " + std::string(reference), std::string(part));
    return Value::floating(number);
}

Value parse_cell_value(pugi::xml_node cell, const std::vector<std::string>& shared_strings,
                       std::string_view reference, std::string_view part) {
    const std::string type = attribute(cell, "t");
    const pugi::xml_node value_node = child_named(cell, "v");
    const bool has_formula = static_cast<bool>(child_named(cell, "f"));
    const std::string value = value_node ? value_node.child_value() : std::string{};

    if (type == "inlineStr") {
        const pugi::xml_node inline_string = child_named(cell, "is");
        if (!inline_string) return Value::string("");
        std::string text;
        append_text_nodes(inline_string, text);
        return Value::string(text);
    }
    if (type == "s") {
        if (!value_node) throw errors::XLSXParseError("shared-string cell has no value: " + std::string(reference), std::string(part));
        uint64_t index = 0;
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), index);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || index >= shared_strings.size())
            throw errors::XLSXParseError("shared-string index out of range in cell " + std::string(reference), std::string(part));
        return Value::string(shared_strings[static_cast<std::size_t>(index)]);
    }
    if (type == "b") {
        if (value == "1" || value == "true") return Value::boolean(true);
        if (value == "0" || value == "false") return Value::boolean(false);
        throw errors::XLSXParseError("invalid boolean value in cell " + std::string(reference), std::string(part));
    }
    if (type == "str" || type == "e") return Value::string(value);
    if (type == "d")
        throw errors::UnsupportedFeature("ISO 8601 date cells (t=\"d\") are not supported: " + std::string(reference));
    if (!type.empty() && type != "n")
        throw errors::UnsupportedFeature("unsupported XLSX cell type '" + type + "' at " + std::string(reference));
    if (!value_node) {
        if (has_formula)
            throw errors::UnsupportedFeature("formula cell without a cached result is not supported: " + std::string(reference));
        return Value::null();
    }
    return parse_number(value, reference, part);
}

bool row_is_blank(const Row& row, BlankPolicy policy) {
    for (const Cell& cell : row)
        if (!cell.value().is_blank(policy)) return false;
    return true;
}

// Scan the worksheet envelope incrementally and retain at most one complete <row> subtree.
// Each subtree is then delegated to pugixml, preserving its XML decoding without building a
// document for the full worksheet. Package metadata and shared strings remain separately owned.
class SheetStreamParser {
public:
    SheetStreamParser(const SheetPart& sheet, const std::vector<std::string>& shared_strings,
                      const XLSXConfig& config, const std::function<void(Row&&)>& callback)
        : sheet_(sheet), shared_strings_(shared_strings), config_(config), callback_(callback) {}

    bool feed(std::string_view chunk) {
        buffer_.append(chunk);
        return process(false);
    }

    void finish() {
        process(true);
        if (capturing_) fail("unterminated row element", absolute_offset_ + row_start_);
        if (!saw_worksheet_) throw errors::MissingRequiredElement(
            "XLSX worksheet part has no worksheet root: " + sheet_.path);
        if (!saw_sheet_data_) throw errors::MissingRequiredElement(
            "XLSX worksheet has no sheetData element: " + sheet_.path);
    }

private:
    struct Tag {
        std::size_t end = 0;
        std::string_view name;
        bool closing = false;
        bool self_closing = false;
        bool special = false;
    };

    [[noreturn]] void fail(std::string message, std::size_t offset) const {
        throw errors::XLSXParseError(std::move(message) + " in XLSX part '" + sheet_.path +
            "' at byte " + std::to_string(offset), sheet_.path, 0, offset);
    }

    // Locate one complete tag while respecting quoted attributes and XML constructs that may
    // contain '>'. An incomplete construct remains buffered for the next ZIP chunk.
    std::optional<Tag> tag_at(std::size_t start, bool final) {
        if (buffer_.compare(start, 4, "<!--") == 0) {
            const auto end = buffer_.find("-->", start + 4);
            if (end == std::string::npos) { if (final) fail("unterminated XML comment", absolute_offset_ + start); return std::nullopt; }
            return Tag{end + 3, {}, false, false, true};
        }
        if (buffer_.compare(start, 9, "<![CDATA[") == 0) {
            const auto end = buffer_.find("]]>", start + 9);
            if (end == std::string::npos) { if (final) fail("unterminated CDATA section", absolute_offset_ + start); return std::nullopt; }
            return Tag{end + 3, {}, false, false, true};
        }
        if (buffer_.compare(start, 2, "<?") == 0) {
            const auto end = buffer_.find("?>", start + 2);
            if (end == std::string::npos) { if (final) fail("unterminated processing instruction", absolute_offset_ + start); return std::nullopt; }
            return Tag{end + 2, {}, false, false, true};
        }
        bool quoted = false;
        char quote = 0;
        std::size_t end = start + 1;
        for (; end < buffer_.size(); ++end) {
            const char ch = buffer_[end];
            if (quoted) { if (ch == quote) quoted = false; }
            else if (ch == '\'' || ch == '"') { quoted = true; quote = ch; }
            else if (ch == '>') break;
        }
        if (end == buffer_.size()) { if (final) fail("unterminated XML tag", absolute_offset_ + start); return std::nullopt; }
        if (buffer_.compare(start, 2, "<!") == 0) return Tag{end + 1, {}, false, false, true};
        std::size_t position = start + 1;
        bool closing = position < end && buffer_[position] == '/';
        if (closing) ++position;
        while (position < end && (buffer_[position] == ' ' || buffer_[position] == '\t' ||
               buffer_[position] == '\r' || buffer_[position] == '\n')) ++position;
        const std::size_t name_start = position;
        while (position < end && buffer_[position] != ' ' && buffer_[position] != '\t' &&
               buffer_[position] != '\r' && buffer_[position] != '\n' &&
               buffer_[position] != '/' && buffer_[position] != '>') ++position;
        if (name_start == position) fail("empty XML tag name", absolute_offset_ + start);
        std::size_t slash = end;
        while (slash > start && (buffer_[slash - 1] == ' ' || buffer_[slash - 1] == '\t' ||
               buffer_[slash - 1] == '\r' || buffer_[slash - 1] == '\n')) --slash;
        return Tag{end + 1, std::string_view(buffer_).substr(name_start, position - name_start),
                   closing, !closing && slash > start && buffer_[slash - 1] == '/', false};
    }

    static std::string_view tag_local_name(std::string_view name) {
        const auto colon = name.find(':');
        return colon == std::string_view::npos ? name : name.substr(colon + 1);
    }

    // OOXML permits omitted row/cell references. Track their implicit successors while keeping
    // Row indices absolute and Cell columns sparse.
    void emit_row(std::size_t end) {
        const std::string_view xml(buffer_.data() + row_start_, end - row_start_);
        const pugi::xml_document document = parse_xml(xml, sheet_.path);
        const pugi::xml_node row_node = document.document_element();
        if (!row_node || local_name(row_node.name()) != "row") fail("invalid row element", absolute_offset_ + row_start_);
        const std::string declared_row = attribute(row_node, "r");
        if (declared_row.empty() && next_implicit_row_ >= max_xlsx_rows)
            fail("implicit row exceeds Excel's 1,048,576-row limit", absolute_offset_ + row_start_);
        const std::size_t row_index = declared_row.empty() ? next_implicit_row_ :
            parse_positive_index(declared_row, "row index", sheet_.path, max_xlsx_rows);
        next_implicit_row_ = row_index + 1;
        Row row(row_index);
        std::size_t next_implicit_column = 0;
        for (const auto cell : row_node.children()) {
            if (local_name(cell.name()) != "c") continue;
            const std::string reference = attribute(cell, "r");
            if (reference.empty() && next_implicit_column >= max_xlsx_columns)
                fail("implicit cell exceeds Excel's XFD column limit", absolute_offset_ + row_start_);
            std::size_t column = next_implicit_column;
            if (!reference.empty()) {
                const auto [cell_row, cell_column] = parse_cell_reference(reference, sheet_.path);
                if (cell_row != row_index)
                    fail("cell reference '" + reference + "' disagrees with containing row", absolute_offset_ + row_start_);
                column = cell_column;
            }
            next_implicit_column = column + 1;
            Value parsed = parse_cell_value(cell, shared_strings_,
                reference.empty() ? std::to_string(row_index + 1) + ":" + std::to_string(column + 1) : reference,
                sheet_.path);
            if (!parsed.is_null()) row.set(column, std::move(parsed));
        }
        if (!config_.skip_empty_rows || !row_is_blank(row, config_.blank_policy)) {
            callback_(std::move(row));
            ++emitted_;
            if (config_.max_rows != 0 && emitted_ >= config_.max_rows) stopped_ = true;
        }
    }

    // Prefix bytes are discarded only outside a captured row; this bounds memory by the largest
    // row plus a small envelope-scanning window rather than worksheet size.
    bool process(bool final) {
        while (!stopped_) {
            const auto start = buffer_.find('<', scan_);
            if (start == std::string::npos) {
                if (!capturing_) {
                    absolute_offset_ += buffer_.size();
                    buffer_.clear();
                    scan_ = 0;
                }
                return true;
            }
            const auto parsed = tag_at(start, final);
            if (!parsed) {
                if (!capturing_ && start != 0) {
                    absolute_offset_ += start;
                    buffer_.erase(0, start);
                    scan_ = 0;
                }
                return true;
            }
            const Tag tag = *parsed;
            scan_ = tag.end;
            if (tag.special) continue;
            const std::string_view name = tag_local_name(tag.name);
            if (!saw_worksheet_ && !tag.closing && name == "worksheet") saw_worksheet_ = true;
            if (!tag.closing && name == "sheetData") saw_sheet_data_ = true;
            if (!capturing_ && saw_sheet_data_ && !tag.closing && name == "row") {
                capturing_ = true;
                row_start_ = start;
                row_depth_ = tag.self_closing ? 0 : 1;
                if (tag.self_closing) {
                    emit_row(tag.end);
                    discard_through(tag.end);
                }
                continue;
            }
            if (capturing_) {
                if (!tag.closing && !tag.self_closing) ++row_depth_;
                else if (tag.closing) {
                    if (row_depth_ == 0) fail("unexpected closing tag in row", absolute_offset_ + start);
                    --row_depth_;
                    if (row_depth_ == 0) {
                        if (name != "row") fail("mismatched closing tag for row", absolute_offset_ + start);
                        emit_row(tag.end);
                        discard_through(tag.end);
                    }
                }
            } else if (scan_ > 128 * 1024) discard_through(scan_);
        }
        return false;
    }

    void discard_through(std::size_t end) {
        absolute_offset_ += end;
        buffer_.erase(0, end);
        scan_ = 0;
        row_start_ = 0;
        capturing_ = false;
    }

    const SheetPart& sheet_;
    const std::vector<std::string>& shared_strings_;
    const XLSXConfig& config_;
    const std::function<void(Row&&)>& callback_;
    std::string buffer_;
    std::size_t scan_ = 0;
    std::size_t row_start_ = 0;
    std::size_t row_depth_ = 0;
    std::size_t absolute_offset_ = 0;
    std::size_t next_implicit_row_ = 0;
    std::size_t emitted_ = 0;
    bool saw_worksheet_ = false;
    bool saw_sheet_data_ = false;
    bool capturing_ = false;
    bool stopped_ = false;
};

bool parse_sheet(ZipReader& archive, const SheetPart& sheet,
                 const std::vector<std::string>& shared_strings, const XLSXConfig& config,
                 const std::function<void(Row&&)>& callback) {
    SheetStreamParser parser(sheet, shared_strings, config, callback);
    bool completed = true;
    archive.read_chunks(sheet.path, [&parser, &completed](std::string_view chunk) {
        completed = parser.feed(chunk);
        return completed;
    });
    if (completed) parser.finish();
    return completed;
}

const SheetPart& select_sheet(const PackageIndex& index, std::string_view name) {
    if (name.empty()) return index.sheets.front();
    for (const auto& sheet : index.sheets) if (sheet.name == name) return sheet;
    throw errors::WorksheetNotFound("worksheet not found in XLSX package: " + std::string(name));
}

} // namespace

Workbook read_xlsx(const std::filesystem::path& path, const XLSXConfig& config) {
    ZipReader archive(path);
    const PackageIndex index = xlsx_internal::read_package_index(archive);
    const auto shared_strings = xlsx_internal::read_shared_strings(archive, index.shared_strings_path);
    Workbook workbook;
    for (const auto& sheet : index.sheets) {
        Worksheet& worksheet = workbook.add_sheet(sheet.name);
        parse_sheet(archive, sheet, shared_strings, config,
                    [&worksheet](Row&& row) { worksheet.insert(std::move(row)); });
    }
    return workbook;
}

void stream_xlsx(const std::filesystem::path& path, std::string_view sheet_name,
                 const XLSXConfig& config, const std::function<void(const Row&)>& callback) {
    if (!callback) throw errors::ReadError("XLSX row callback is empty");
    ZipReader archive(path);
    const PackageIndex index = xlsx_internal::read_package_index(archive);
    const auto shared_strings = xlsx_internal::read_shared_strings(archive, index.shared_strings_path);
    const SheetPart& sheet = select_sheet(index, sheet_name);
    parse_sheet(archive, sheet, shared_strings, config,
                [&callback](Row&& row) { callback(row); });
}

std::vector<std::string> xlsx_sheet_names(const std::filesystem::path& path) {
    ZipReader archive(path);
    const PackageIndex index = xlsx_internal::read_package_index(archive);
    std::vector<std::string> names;
    names.reserve(index.sheets.size());
    for (const auto& sheet : index.sheets) names.push_back(sheet.name);
    return names;
}

} // namespace quickxlsx::detail
