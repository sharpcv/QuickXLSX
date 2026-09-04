#pragma once
#include "quickxlsx/cell.hpp"
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>
namespace quickxlsx {
class Worksheet; class Row; class RowView;
/** Inclusive A1 rectangle stored as half-open zero-based bounds. It does not own its Worksheet, which must outlive access and derived views. */
class Range {
public:
    /** Maximum Excel worksheet row count. */ static constexpr std::size_t max_rows = 1'048'576;
    /** Maximum Excel worksheet column count. */ static constexpr std::size_t max_columns = 16'384;
    /** Binds an A1 rectangle; throws errors::InvalidRange for malformed, reversed, or out-of-limit input. */
    Range(const Worksheet& worksheet, std::string_view a1);
    /** Binds a non-empty half-open rectangle; throws errors::InvalidRange for invalid bounds. */
    Range(const Worksheet& worksheet, std::size_t top, std::size_t left, std::size_t bottom, std::size_t right);
    /** Returns inclusive top row as a zero-based index. */ [[nodiscard]] std::size_t top() const noexcept { return top_; }
    /** Returns inclusive left column as a zero-based index. */ [[nodiscard]] std::size_t left() const noexcept { return left_; }
    /** Returns exclusive bottom row. */ [[nodiscard]] std::size_t bottom() const noexcept { return bottom_; }
    /** Returns exclusive right column. */ [[nodiscard]] std::size_t right() const noexcept { return right_; }
    /** Returns rectangle height. */ [[nodiscard]] std::size_t row_count() const noexcept { return bottom_ - top_; }
    /** Returns rectangle width. */ [[nodiscard]] std::size_t col_count() const noexcept { return right_ - left_; }
    /** Returns whether either dimension is empty; valid constructed ranges are never empty. */ [[nodiscard]] bool empty() const noexcept { return top_ == bottom_ || left_ == right_; }
    /** Returns whether the rectangle is one cell. */ [[nodiscard]] bool is_single_cell() const noexcept { return row_count() == 1 && col_count() == 1; }
    /** Returns whether width is one. */ [[nodiscard]] bool is_single_column() const noexcept { return col_count() == 1; }
    /** Returns whether height is one. */ [[nodiscard]] bool is_single_row() const noexcept { return row_count() == 1; }
    /** Returns an owning relative-column cell; sparse holes contain Null; throws InvalidRange outside this rectangle. */ [[nodiscard]] Cell cell(std::size_t row, std::size_t column) const;
    /** Returns an owning value; sparse holes produce Null; throws InvalidRange outside this rectangle. */ [[nodiscard]] Value value(std::size_t row, std::size_t column) const;
    /** Materializes row-major owning cells, including sparse holes as Null. */ [[nodiscard]] std::vector<Cell> cells() const;
    /** Materializes dense owning rows with relative row and column indices. */ [[nodiscard]] std::vector<Row> rows() const;
    /** Materializes row-major owning values; size is row_count()*col_count(). */ [[nodiscard]] std::vector<Value> values() const;
    /** Creates a lazy view; this range's Worksheet must outlive first materialization. */ [[nodiscard]] RowView view() const;
    /** Returns canonical single-cell or rectangular A1 notation. */ [[nodiscard]] std::string to_string() const;
private:
    const Worksheet* worksheet_; std::size_t top_; std::size_t left_; std::size_t bottom_; std::size_t right_;
};
} // namespace quickxlsx
