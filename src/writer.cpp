#include "quickxlsx/writer.hpp"

#include "quickxlsx/errors.hpp"
#ifdef QUICKXLSX_ENABLE_XLSX
#include "xlsx_format.hpp"
#endif
#include "utf8_path.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace quickxlsx::detail {
void write_csv_values(std::ostream&, const std::vector<Value>&, char, const DateTimeOptions&);
void write_csv_row(std::ostream&, const Row&, char, const DateTimeOptions&);
}

namespace quickxlsx {
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
    throw errors::UnsupportedFeature("unsupported output format for " + path.string() + "; expected .csv or .xlsx");
}
void validate_delimiter(char value) {
    if (value == '\0' || value == '\r' || value == '\n' || value == '"')
        throw errors::WriteError("invalid CSV delimiter");
}

} // namespace

struct Writer::State {
    std::filesystem::path path;
    Format format;
    CSVConfig csv;
    DateTimeOptions datetime;
    int compression_level = 6;
    std::string sheet_name = "Sheet1";
    std::ofstream output;
#ifdef QUICKXLSX_ENABLE_XLSX
    std::unique_ptr<detail::XlsxStreamWriter> xlsx;
#endif
    std::vector<Value> current_row;
    std::size_t next_row = 0;
    bool open = true;
};

namespace {
std::unique_ptr<Writer::State> make_state(const std::filesystem::path& path, Format format, const CSVConfig& csv) {
    auto state = std::make_unique<Writer::State>();
    state->path = path;
    state->format = format;
    state->csv = csv;
    if (format == Format::CSV) {
        validate_delimiter(csv.delimiter);
        state->output.open(state->path, std::ios::binary | std::ios::trunc);
        if (!state->output) throw errors::FilePermissionDenied("cannot open output file: " + state->path.string());
    } else {
#ifndef QUICKXLSX_ENABLE_XLSX
        throw errors::UnsupportedFeature("XLSX support is disabled: " + state->path.string());
#endif
    }
    return state;
}
void require_open(const Writer::State* state) {
    if (state == nullptr || !state->open) throw errors::WriteError("writer is closed");
}
// XLSX startup is delayed until the first row or close so callers can configure package metadata.
// CSV opens eagerly because its configuration does not require a package prologue.
void ensure_xlsx_started(Writer::State& state) {
#ifdef QUICKXLSX_ENABLE_XLSX
    if (state.format == Format::XLSX && !state.xlsx)
        state.xlsx = std::make_unique<detail::XlsxStreamWriter>(
            state.path, state.sheet_name, state.compression_level);
#else
    static_cast<void>(state);
#endif
}
// Explicit Row indices may be sparse. CSV needs physical blank records for skipped indices;
// XLSX emits empty row elements so both formats preserve the same absolute row numbering.
void append_blank_rows(Writer::State& state, std::size_t target) {
    while (state.next_row < target) {
        if (state.format == Format::CSV) detail::write_csv_values(state.output, {}, state.csv.delimiter, state.datetime);
        else {
            ensure_xlsx_started(state);
#ifdef QUICKXLSX_ENABLE_XLSX
            state.xlsx->write_row(Row(state.next_row));
#endif
        }
        ++state.next_row;
    }
}
void append_row(Writer::State& state, Row row) {
    if (row.index() < state.next_row)
        throw errors::WriteError("row index " + std::to_string(row.index()) + " precedes next output row " + std::to_string(state.next_row));
    append_blank_rows(state, row.index());
    if (state.format == Format::CSV) detail::write_csv_row(state.output, row, state.csv.delimiter, state.datetime);
    else {
        ensure_xlsx_started(state);
#ifdef QUICKXLSX_ENABLE_XLSX
        state.xlsx->write_row(row);
#endif
    }
    ++state.next_row;
}
}

Writer::Writer(const std::string& path) : state_(nullptr) {
    const auto native = detail::path_from_utf8(path);
    state_ = make_state(native, format_from_path(native), {});
}
Writer::Writer(const std::string& path, const CSVConfig& config) : state_(nullptr) {
    state_ = make_state(detail::path_from_utf8(path), Format::CSV, config);
}
Writer::Writer(const std::string& path, const XLSXConfig&) : state_(nullptr) {
    state_ = make_state(detail::path_from_utf8(path), Format::XLSX, {});
}
Writer::~Writer() noexcept { try { close(); } catch (...) {} }
Writer::Writer(Writer&&) noexcept = default;
Writer& Writer::operator=(Writer&& other) noexcept {
    if (this != &other) {
        try { close(); } catch (...) {}
        state_ = std::move(other.state_);
    }
    return *this;
}

Writer& Writer::write_cell(const Value& value) { require_open(state_.get()); state_->current_row.push_back(value); return *this; }
Writer& Writer::write_null() { return write_cell(Value::null()); }
Writer& Writer::write_empty_string() { return write_cell(Value(std::string{})); }
Writer& Writer::end_row() {
    require_open(state_.get());
    Row row(state_->next_row);
    for (std::size_t column = 0; column < state_->current_row.size(); ++column)
        row.set(column, std::move(state_->current_row[column]));
    state_->current_row.clear();
    append_row(*state_, std::move(row));
    return *this;
}
Writer& Writer::write_row(const Row& row) {
    require_open(state_.get());
    if (!state_->current_row.empty()) end_row();
    append_row(*state_, row);
    return *this;
}
Writer& Writer::write_row(const std::vector<Value>& values) {
    require_open(state_.get());
    if (!state_->current_row.empty()) end_row();
    Row row(state_->next_row);
    for (std::size_t column = 0; column < values.size(); ++column) row.set(column, values[column]);
    append_row(*state_, std::move(row));
    return *this;
}
Writer& Writer::write_rows(const std::vector<Row>& rows) { for (const auto& row : rows) write_row(row); return *this; }
Writer& Writer::write_sheet(const Worksheet& sheet) {
    require_open(state_.get());
    set_sheet_name(sheet.name());
    for (const auto& row : sheet) write_row(row);
    return *this;
}

void Writer::set_delimiter(char value) { require_open(state_.get()); if (state_->format != Format::CSV) throw errors::UnsupportedFeature("delimiter is only valid for CSV output"); validate_delimiter(value); state_->csv.delimiter = value; }
void Writer::set_sheet_name(const std::string& value) {
    require_open(state_.get());
    if (!Worksheet::valid_name(value)) throw errors::InvalidWorksheetName("invalid worksheet name: " + value);
    if (state_->next_row != 0 || !state_->current_row.empty()) throw errors::WriteError("sheet name must be set before writing rows");
    state_->sheet_name = value;
}
void Writer::set_compression_level(int value) { require_open(state_.get()); if (state_->format != Format::XLSX) throw errors::UnsupportedFeature("compression level is only valid for XLSX output"); if (value < 0 || value > 9) throw errors::WriteError("compression level must be between 0 and 9"); if (state_->next_row != 0 || !state_->current_row.empty()) throw errors::WriteError("compression level must be set before writing rows"); state_->compression_level = value; }
void Writer::set_datetime_options(const DateTimeOptions& value) { require_open(state_.get()); state_->datetime = value; }
void Writer::flush() {
    require_open(state_.get());
    if (!state_->current_row.empty()) end_row();
    if (state_->format == Format::CSV) {
        state_->output.flush();
        if (!state_->output) throw errors::WriteError("failed to flush output file: " + state_->path.string());
#ifdef QUICKXLSX_ENABLE_XLSX
    } else if (state_->xlsx) state_->xlsx->flush();
#else
    }
#endif
}
void Writer::close() {
    if (!is_open()) return;
    if (!state_->current_row.empty()) end_row();
    if (state_->format == Format::CSV) {
        state_->output.flush();
        if (!state_->output) throw errors::WriteError("failed to write output file: " + state_->path.string());
        state_->output.close();
        if (state_->output.fail()) throw errors::WriteError("failed to close output file: " + state_->path.string());
#ifdef QUICKXLSX_ENABLE_XLSX
    } else {
        ensure_xlsx_started(*state_);
        state_->xlsx->close();
#else
    } else {
        throw errors::UnsupportedFeature("XLSX support is disabled: " + state_->path.string());
#endif
    }
    state_->open = false;
}
bool Writer::is_open() const noexcept { return state_ && state_->open; }

} // namespace quickxlsx
