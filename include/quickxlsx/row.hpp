#pragma once

#include "quickxlsx/cell.hpp"
#include "quickxlsx/errors.hpp"

#include <cstddef>
#include <initializer_list>
#include <vector>

namespace quickxlsx {

/** Owning sparse row whose cells are kept in ascending column order. */
class Row {
public:
    /** Cell storage type. */ using container_type = std::vector<Cell>;
    /** Mutable cell iterator. */ using iterator = container_type::iterator;
    /** Read-only cell iterator. */ using const_iterator = container_type::const_iterator;
    /** Constructs an empty row at the zero-based index. */
    explicit Row(std::size_t index = 0) noexcept : index_(index) {}
    /** Constructs consecutive cells beginning at column zero. */
    Row(std::size_t index, std::initializer_list<Value> values);
    /** Returns the zero-based source row index. */
    [[nodiscard]] std::size_t index() const noexcept { return index_; }
    /** Returns the number of stored cells, excluding sparse holes. */
    [[nodiscard]] std::size_t size() const noexcept { return cells_.size(); }
    /** Returns whether no cells are stored. */
    [[nodiscard]] bool empty() const noexcept { return cells_.empty(); }
    /** Returns one past the greatest stored column, or zero when empty. */
    [[nodiscard]] std::size_t column_count() const noexcept;
    /** Returns an owning value copy; a sparse missing column returns Null. */
    [[nodiscard]] Value operator[](std::size_t column) const;
    /** Returns the cell, inserting an owned Null cell for a sparse missing column. */
    [[nodiscard]] Cell& operator[](std::size_t column);
    /** Returns an owning value copy; throws errors::InvalidColumnIndex when absent. */
    [[nodiscard]] Value at(std::size_t column) const;
    /** Finds a stored cell; the pointer is invalidated by row mutation. */
    [[nodiscard]] const Cell* find(std::size_t column) const noexcept;
    /** Finds a stored cell; the pointer is invalidated by row mutation. */
    [[nodiscard]] Cell* find(std::size_t column) noexcept;
    /** Returns whether a cell is stored at column. */
    [[nodiscard]] bool contains(std::size_t column) const noexcept { return find(column) != nullptr; }
    /** Inserts or replaces a cell and returns a reference invalidated by later row mutation. */
    Cell& set(std::size_t column, Value value);
    /** Erases a stored cell and reports whether one existed. */
    bool erase(std::size_t column) noexcept;
    /** Returns the first mutable stored-cell iterator. */ [[nodiscard]] iterator begin() noexcept { return cells_.begin(); }
    /** Returns the mutable stored-cell sentinel. */ [[nodiscard]] iterator end() noexcept { return cells_.end(); }
    /** Returns the first read-only stored-cell iterator. */ [[nodiscard]] const_iterator begin() const noexcept { return cells_.begin(); }
    /** Returns the read-only stored-cell sentinel. */ [[nodiscard]] const_iterator end() const noexcept { return cells_.end(); }
    /** Returns the first read-only stored-cell iterator. */ [[nodiscard]] const_iterator cbegin() const noexcept { return cells_.cbegin(); }
    /** Returns the read-only stored-cell sentinel. */ [[nodiscard]] const_iterator cend() const noexcept { return cells_.cend(); }
    /** Returns sparse cell storage; the reference is valid until row mutation or destruction. */
    [[nodiscard]] const container_type& cells() const noexcept { return cells_; }
    /** Returns dense owning values through column_count(), filling sparse holes with Null. */
    [[nodiscard]] std::vector<Value> values() const;
private:
    using position = container_type::iterator;
    using const_position = container_type::const_iterator;
    [[nodiscard]] position lower_bound(std::size_t column) noexcept;
    [[nodiscard]] const_position lower_bound(std::size_t column) const noexcept;
    std::size_t index_;
    container_type cells_;
};

} // namespace quickxlsx
