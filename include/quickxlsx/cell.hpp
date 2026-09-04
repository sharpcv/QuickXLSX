#pragma once

#include "quickxlsx/value.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace quickxlsx {

/** Zero-based cell coordinates with conversion to and from uppercase Excel A1 notation. */
class CellRef {
public:
    /** Parses A1; throws errors::InvalidRange for malformed or out-of-limit references. */
    explicit CellRef(std::string_view a1);
    /** Constructs coordinates; throws errors::InvalidRange outside Excel worksheet limits. */
    CellRef(std::size_t row, std::size_t column);
    /** Returns the zero-based row. */
    [[nodiscard]] std::size_t row() const noexcept { return row_; }
    /** Returns the zero-based column. */
    [[nodiscard]] std::size_t column() const noexcept { return column_; }
    /** Returns canonical uppercase A1 notation. */
    [[nodiscard]] std::string to_string() const;
    /** Constructs validated zero-based coordinates. */
    [[nodiscard]] static CellRef from_index(std::size_t row, std::size_t column) { return CellRef(row, column); }
    /** Compares coordinates. */
    [[nodiscard]] bool operator==(const CellRef&) const noexcept = default;
    /** Maximum Excel worksheet row count. */
    static constexpr std::size_t max_rows = 1'048'576;
    /** Maximum Excel worksheet column count. */
    static constexpr std::size_t max_columns = 16'384;
private:
    std::size_t row_;
    std::size_t column_;
};

/** Owning cell consisting of a zero-based column and a Value. */
class Cell {
public:
    /** Constructs a cell, moving value into owned storage. */
    Cell(std::size_t column, Value value = {}) : column_(column), value_(std::move(value)) {}
    /** Returns the zero-based column. */
    [[nodiscard]] std::size_t column() const noexcept { return column_; }
    /** Returns a reference valid until this cell is destroyed or its value is replaced. */
    [[nodiscard]] const Value& value() const noexcept { return value_; }
    /** Returns mutable owned value storage. */
    [[nodiscard]] Value& value() noexcept { return value_; }
    /** Replaces the owned value. */
    void set_value(Value value) { value_ = std::move(value); }
    /** Converts the value to owned text. */
    [[nodiscard]] std::string as_string() const;
    /** Converts to integer, returning default_value on failure. */
    [[nodiscard]] std::int64_t as_int64(std::int64_t default_value = 0) const noexcept;
    /** Converts to double, returning default_value on failure. */
    [[nodiscard]] double as_double(double default_value = 0.0) const noexcept;
    /** Converts to bool, returning default_value on failure. */
    [[nodiscard]] bool as_bool(bool default_value = false) const noexcept;
    /** Returns a reference to the owned Value; its lifetime is tied to this cell. */
    operator const Value&() const noexcept { return value_; }
private:
    std::size_t column_;
    Value value_;
};

} // namespace quickxlsx
