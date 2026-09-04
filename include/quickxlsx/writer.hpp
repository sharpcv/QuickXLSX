#pragma once

#include "quickxlsx/config.hpp"
#include "quickxlsx/row.hpp"
#include "quickxlsx/worksheet.hpp"

#include <concepts>
#include <cstddef>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace quickxlsx {

/**
 * Move-only streaming CSV/XLSX writer.
 *
 * Rows are emitted as they are completed. CSV writes directly to the target;
 * XLSX writes an incremental worksheet into a temporary package and atomically
 * replaces the target on close. Configuration that affects package metadata
 * must be set before the first row.
 */
class Writer {

public:
    /** Opaque implementation state; exposed only to support out-of-line helpers. */
    struct State;
    /** Selects CSV or XLSX from path extension and opens a streaming writer. */
    explicit Writer(const std::string& path);
    /** Opens CSV output using config. */
    Writer(const std::string& path, const CSVConfig& config);
    /** Opens XLSX output using config. */
    Writer(const std::string& path, const XLSXConfig& config);
    /** Closes output; exceptions during implicit close are suppressed. */
    ~Writer() noexcept;

    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;
    /** Transfers output ownership; the source becomes closed. */
    Writer(Writer&&) noexcept;
    /** Closes current output and transfers ownership; close failures are suppressed. */
    Writer& operator=(Writer&&) noexcept;

    /** Appends one field to the current row. A row boundary is established by end_row(), write_row(), flush(), or close(). */
    Writer& write_cell(const Value& value);
    /** Constructs and appends one field value. */
    template<typename T> requires (!std::same_as<std::remove_cvref_t<T>, Value>)
    Writer& write_cell(T&& value) { return write_cell(Value(std::forward<T>(value))); }
    /** Appends a Null field. */
    Writer& write_null();
    /** Appends an explicit empty String field. */
    Writer& write_empty_string();
    /** Emits the current row, including an empty row when no fields were appended. */
    Writer& end_row();

    /** Emits a sparse row at its absolute zero-based row index; indices must be nondecreasing. */
    Writer& write_row(const Row& row);
    /** Emits a dense row at the next output index. */
    Writer& write_row(const std::vector<Value>& values);
    /** Converts and emits a dense row at the next output index. */
    template<typename... Args>
        requires (sizeof...(Args) > 0 &&
                  !(sizeof...(Args) == 1 &&
                    ((std::convertible_to<Args, const Row&> ||
                      std::convertible_to<Args, const std::vector<Value>&>) || ...)))
    Writer& write_row(Args&&... values) {
        std::vector<Value> row;
        row.reserve(sizeof...(Args));
        (row.emplace_back(std::forward<Args>(values)), ...);
        return write_row(static_cast<const std::vector<Value>&>(row));
    }
    /** Emits rows in order, preserving their absolute indices. */
    Writer& write_rows(const std::vector<Row>& rows);
    /** Sets the output sheet name and emits all stored rows. */
    Writer& write_sheet(const Worksheet& sheet);

    /** Changes the CSV delimiter; valid only for CSV output. */
    void set_delimiter(char delimiter);
    /** Changes the XLSX sheet name before writing any row. */
    void set_sheet_name(const std::string& name);
    /** Sets XLSX DEFLATE compression from 0 through 9 before writing any row. */
    void set_compression_level(int level);
    /** Sets DateTime text conversion used by subsequent output. */
    void set_datetime_options(const DateTimeOptions& options);

    /** Completes a pending row and flushes available output without closing it. */
    void flush();
    /** Completes pending output and closes; XLSX atomically publishes the package. */
    void close();
    /** Reports whether this object still owns open output. */
    [[nodiscard]] bool is_open() const noexcept;

private:
    std::unique_ptr<State> state_;
};

} // namespace quickxlsx
