#pragma once

#include "quickxlsx/errors.hpp"
#include "quickxlsx/row.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace quickxlsx {
class Range; class WorksheetView; class RowView; class ValueView;

/** Owning sparse worksheet with rows kept in ascending source-index order. */
class Worksheet {
public:
    /** Row storage type. */ using container_type = std::vector<Row>;
    /** Mutable row iterator. */ using iterator = container_type::iterator;
    /** Read-only row iterator. */ using const_iterator = container_type::const_iterator;
    /** Constructs an empty sheet; throws errors::InvalidWorksheetName for invalid Excel names. */
    explicit Worksheet(std::string name);
    /** Returns the name; the reference is valid for this worksheet's lifetime. */ [[nodiscard]] const std::string& name() const noexcept { return name_; }
    /** Returns the number of stored rows, excluding sparse holes. */ [[nodiscard]] std::size_t row_count() const noexcept { return rows_.size(); }
    /** Returns one past the greatest stored column across all rows. */ [[nodiscard]] std::size_t column_count() const noexcept;
    /** Returns whether no rows are stored. */ [[nodiscard]] bool empty() const noexcept { return rows_.empty(); }
    /** Returns a row, inserting an empty sparse row when absent. */ [[nodiscard]] Row& operator[](std::size_t row);
    /** Returns an existing row; throws errors::InvalidRowIndex when absent. */ [[nodiscard]] const Row& operator[](std::size_t row) const;
    /** Returns an existing row; throws errors::InvalidRowIndex when absent. */ [[nodiscard]] Row& at(std::size_t row);
    /** Returns an existing row; throws errors::InvalidRowIndex when absent. */ [[nodiscard]] const Row& at(std::size_t row) const;
    /** Finds a row; the pointer is invalidated by worksheet mutation. */ [[nodiscard]] Row* find(std::size_t row) noexcept;
    /** Finds a row; the pointer is invalidated by worksheet mutation. */ [[nodiscard]] const Row* find(std::size_t row) const noexcept;
    /** Returns whether a row is stored. */ [[nodiscard]] bool contains(std::size_t row) const noexcept { return find(row) != nullptr; }
    /** Inserts or replaces by source index and returns a reference invalidated by later mutation. */ Row& insert(Row row);
    /** Returns an existing row or inserts an empty row. */ Row& ensure_row(std::size_t row);
    /** Erases a stored row and reports whether one existed. */ bool erase(std::size_t row) noexcept;
    /** Returns the first mutable stored-row iterator. */ [[nodiscard]] iterator begin() noexcept { return rows_.begin(); }
    /** Returns the mutable stored-row sentinel. */ [[nodiscard]] iterator end() noexcept { return rows_.end(); }
    /** Returns the first read-only stored-row iterator. */ [[nodiscard]] const_iterator begin() const noexcept { return rows_.begin(); }
    /** Returns the read-only stored-row sentinel. */ [[nodiscard]] const_iterator end() const noexcept { return rows_.end(); }
    /** Returns the first read-only stored-row iterator. */ [[nodiscard]] const_iterator cbegin() const noexcept { return rows_.cbegin(); }
    /** Returns the read-only stored-row sentinel. */ [[nodiscard]] const_iterator cend() const noexcept { return rows_.cend(); }
    /** Returns sparse row storage; the reference is invalidated by mutation. */ [[nodiscard]] const container_type& rows_data() const noexcept { return rows_; }
    /** Creates a non-owning A1 Range; throws errors::InvalidRange for invalid syntax. */ [[nodiscard]] Range range(std::string_view a1) const;
    /** Creates a non-owning half-open Range; throws errors::InvalidRange for invalid bounds. */
    [[nodiscard]] Range range(std::size_t top, std::size_t left, std::size_t bottom, std::size_t right) const;
    /** Creates a lazy non-owning view; this worksheet must outlive it until materialization. */ [[nodiscard]] WorksheetView rows() const;
    /** Creates a lazy value view, returning Null snapshots for sparse cells. */ [[nodiscard]] ValueView column(std::size_t column) const;
    /** Creates a lazy projection whose output columns follow the requested order. */ [[nodiscard]] RowView columns(const std::vector<std::size_t>& columns) const;
    /** Tests Excel worksheet-name rules. */ [[nodiscard]] static bool valid_name(std::string_view name) noexcept;
private:
    using position = container_type::iterator; using const_position = container_type::const_iterator;
    [[nodiscard]] position lower_bound(std::size_t row) noexcept;
    [[nodiscard]] const_position lower_bound(std::size_t row) const noexcept;
    std::string name_; container_type rows_;
};
} // namespace quickxlsx
