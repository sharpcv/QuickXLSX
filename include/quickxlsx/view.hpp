#pragma once

#include "quickxlsx/row.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace quickxlsx {

class Worksheet;
class Range;
class CellView;
class ValueView;
class StringView;
class IntView;
class DoubleView;
class BoolView;

/** Predicate selecting rows. */
using RowFilter = std::function<bool(const Row&)>;
/** Predicate selecting cells. */
using CellFilter = std::function<bool(const Cell&)>;
/** Predicate selecting values. */
using ValueFilter = std::function<bool(const Value&)>;
/** Owning transformation applied to values. */
using ValueMapper = std::function<Value(const Value&)>;

/**
 * A deferred row pipeline. The view never owns its source Worksheet; the
 * worksheet must outlive the view and every derived view. Evaluation creates a
 * stable, shared snapshot of Row values, so iterators remain valid while any
 * copy of that view remains alive. Worksheet mutations before first evaluation
 * are observed; mutations afterwards are not. Filtering is deferred, while
 * sort_by_column necessarily materializes the rows when evaluated.
 */
class RowView {
public:
    using container_type = std::vector<Row>;
    using const_iterator = container_type::const_iterator;

    /** Constructs an empty view. */
    RowView() = default;

    /** Lazily retains rows accepted by predicate; exceptions propagate during evaluation. */
    [[nodiscard]] RowView filter(RowFilter predicate) const;
    /** Lazily retains at most the first n rows. */
    [[nodiscard]] RowView take(std::size_t n) const;
    /** Lazily drops at most the first n rows. */
    [[nodiscard]] RowView skip(std::size_t n) const;
    /** Lazily selects count rows after start. */
    [[nodiscard]] RowView slice(std::size_t start, std::size_t count) const;
    /** Lazily stable-sorts by a sparse-safe column; Nulls sort by ValueType order. */
    [[nodiscard]] RowView sort_by_column(std::size_t column, bool ascending = true) const;
    /** Flattens stored cells in row order, excluding sparse holes. */
    [[nodiscard]] CellView cells() const;
    /** Projects a column, synthesizing owning Null values for sparse holes. */
    [[nodiscard]] ValueView column(std::size_t column) const;
    /** Projects requested source columns into consecutive output columns. */
    [[nodiscard]] RowView columns(const std::vector<std::size_t>& columns) const;
    /** Materializes if needed and returns row count. */
    [[nodiscard]] std::size_t count() const;
    /** Returns whether the materialized result has no rows. */
    [[nodiscard]] bool empty() const;
    /** Returns snapshot iterator; valid while this view or a copy owns the snapshot. */
    [[nodiscard]] const_iterator begin() const;
    /** Returns snapshot sentinel. */
    [[nodiscard]] const_iterator end() const;
    /** Returns snapshot iterator. */
    [[nodiscard]] const_iterator cbegin() const;
    /** Returns snapshot sentinel. */
    [[nodiscard]] const_iterator cend() const;

private:
    using Loader = std::function<container_type()>;
    explicit RowView(Loader loader);
    struct State;
    explicit RowView(std::shared_ptr<State> state) noexcept;
    [[nodiscard]] const container_type& data() const;

    std::shared_ptr<State> state_;
    friend class WorksheetView;
    friend class Range;
    friend class CellView;
    friend class ValueView;
};

/** Non-owning worksheet entry view; see RowView for lifetime and invalidation. */
class WorksheetView {
public:
    using const_iterator = RowView::const_iterator;

    /** Binds worksheet without ownership; worksheet must outlive first evaluation. */
    explicit WorksheetView(const Worksheet& worksheet) noexcept;
    /** Lazily retains rows accepted by predicate. */
    [[nodiscard]] RowView filter(RowFilter predicate) const;
    /** Lazily retains at most the first n rows. */
    [[nodiscard]] RowView take(std::size_t n) const;
    /** Lazily drops at most the first n rows. */
    [[nodiscard]] RowView skip(std::size_t n) const;
    /** Lazily selects count rows after start. */
    [[nodiscard]] RowView slice(std::size_t start, std::size_t count) const;
    /** Lazily stable-sorts rows by column. */
    [[nodiscard]] RowView sort_by_column(std::size_t column, bool ascending = true) const;
    /** Projects a sparse-safe value column. */
    [[nodiscard]] ValueView column(std::size_t column) const;
    /** Projects requested columns in their requested order. */
    [[nodiscard]] RowView columns(const std::vector<std::size_t>& columns) const;
    /** Materializes if needed and returns row count. */
    [[nodiscard]] std::size_t count() const;
    /** Counts unique full rows, or unique values in selected columns. */
    [[nodiscard]] std::size_t distinct_count(const std::vector<std::size_t>& columns = {}) const;
    /** Returns whether no rows are present. */
    [[nodiscard]] bool empty() const;
    /** Returns snapshot iterator. */
    [[nodiscard]] const_iterator begin() const;
    /** Returns snapshot sentinel. */
    [[nodiscard]] const_iterator end() const;
    /** Returns snapshot iterator. */
    [[nodiscard]] const_iterator cbegin() const;
    /** Returns snapshot sentinel. */
    [[nodiscard]] const_iterator cend() const;

private:
    RowView rows_;
};

/** Deferred cell pipeline backed by a stable materialized snapshot on iteration. */
class CellView {
public:
    using container_type = std::vector<Cell>;
    using const_iterator = container_type::const_iterator;

    /** Constructs an empty view. */
    CellView() = default;
    /** Lazily retains cells accepted by predicate. */
    [[nodiscard]] CellView filter(CellFilter predicate) const;
    /** Lazily retains at most the first n cells. */
    [[nodiscard]] CellView take(std::size_t n) const;
    /** Lazily drops at most the first n cells. */
    [[nodiscard]] CellView skip(std::size_t n) const;
    /** Projects owning Value snapshots from cells. */
    [[nodiscard]] ValueView values() const;
    /** Materializes and returns owning Value copies. */
    [[nodiscard]] std::vector<Value> collect() const;
    /** Returns cell count. */
    [[nodiscard]] std::size_t count() const;
    /** Returns whether no cells are present. */
    [[nodiscard]] bool empty() const;
    /** Returns snapshot iterator. */
    [[nodiscard]] const_iterator begin() const;
    /** Returns snapshot sentinel. */
    [[nodiscard]] const_iterator end() const;
    /** Returns snapshot iterator. */
    [[nodiscard]] const_iterator cbegin() const;
    /** Returns snapshot sentinel. */
    [[nodiscard]] const_iterator cend() const;

private:
    struct State;
    explicit CellView(std::shared_ptr<State> state) noexcept;
    [[nodiscard]] const container_type& data() const;
    std::shared_ptr<State> state_;
    using Loader = std::function<container_type()>;
    explicit CellView(Loader loader);
    friend class RowView;
};

/** Deferred value pipeline. Values are snapshots, so synthesized Nulls never dangle. */
class ValueView {
public:
    using container_type = std::vector<Value>;
    using const_iterator = container_type::const_iterator;

    /** Constructs an empty view. */
    ValueView() = default;
    /** Lazily retains values accepted by predicate. */
    [[nodiscard]] ValueView filter(ValueFilter predicate) const;
    /** Lazily transforms values; exceptions propagate during evaluation. */
    [[nodiscard]] ValueView map(ValueMapper mapper) const;
    /** Lazily retains at most the first n values. */
    [[nodiscard]] ValueView take(std::size_t n) const;
    /** Lazily drops at most the first n values. */
    [[nodiscard]] ValueView skip(std::size_t n) const;
    /** Materializes converted owned strings. */
    [[nodiscard]] StringView as_strings() const;
    /** Materializes safely converted integers. */
    [[nodiscard]] IntView as_ints() const;
    /** Materializes safely converted doubles. */
    [[nodiscard]] DoubleView as_doubles() const;
    /** Materializes safely converted booleans. */
    [[nodiscard]] BoolView as_bools() const;
    /** Returns owning Value copies. */
    [[nodiscard]] std::vector<Value> collect() const;
    /** Returns value count. */
    [[nodiscard]] std::size_t count() const;
    /** Returns whether no values are present. */
    [[nodiscard]] bool empty() const;
    /** Returns snapshot iterator. */
    [[nodiscard]] const_iterator begin() const;
    /** Returns snapshot sentinel. */
    [[nodiscard]] const_iterator end() const;
    /** Returns snapshot iterator. */
    [[nodiscard]] const_iterator cbegin() const;
    /** Returns snapshot sentinel. */
    [[nodiscard]] const_iterator cend() const;

private:
    struct State;
    explicit ValueView(std::shared_ptr<State> state) noexcept;
    [[nodiscard]] const container_type& data() const;
    std::shared_ptr<State> state_;
    using Loader = std::function<container_type()>;
    explicit ValueView(Loader loader);
    friend class RowView;
    friend class CellView;
};

/** Owning materialized string sequence produced by ValueView conversion. */
class StringView {
public:
    using container_type = std::vector<std::string>;
    using const_iterator = container_type::const_iterator;
    /** Constructs an empty sequence. */
    StringView() = default;
    /** Returns a copy of all converted strings. */
    [[nodiscard]] std::vector<std::string> collect() const;
    /** Returns element count. */
    [[nodiscard]] std::size_t count() const;
    /** Returns whether no elements are present. */
    [[nodiscard]] bool empty() const;
    /** Returns stable iterator owned by this view and its copies. */
    [[nodiscard]] const_iterator begin() const;
    /** Returns stable sentinel. */
    [[nodiscard]] const_iterator end() const;
    /** Returns stable iterator. */
    [[nodiscard]] const_iterator cbegin() const;
    /** Returns stable sentinel. */
    [[nodiscard]] const_iterator cend() const;
private:
    explicit StringView(std::shared_ptr<const container_type> values) noexcept;
    std::shared_ptr<const container_type> values_;
    friend class ValueView;
};

/** Owning materialized int64 sequence produced by ValueView conversion. */
class IntView {
public:
    using container_type = std::vector<std::int64_t>;
    using const_iterator = container_type::const_iterator;
    /** Constructs an empty sequence. */
    IntView() = default;
    /** Returns a copy of all converted integers. */
    [[nodiscard]] std::vector<std::int64_t> collect() const;
    /** Returns element count. */
    [[nodiscard]] std::size_t count() const;
    /** Returns whether no elements are present. */
    [[nodiscard]] bool empty() const;
    /** Returns stable iterator owned by this view and its copies. */
    [[nodiscard]] const_iterator begin() const;
    /** Returns stable sentinel. */
    [[nodiscard]] const_iterator end() const;
    /** Returns stable iterator. */
    [[nodiscard]] const_iterator cbegin() const;
    /** Returns stable sentinel. */
    [[nodiscard]] const_iterator cend() const;
private:
    explicit IntView(std::shared_ptr<const container_type> values) noexcept;
    std::shared_ptr<const container_type> values_;
    friend class ValueView;
};

/** Owning materialized double sequence produced by ValueView conversion. */
class DoubleView {
public:
    using container_type = std::vector<double>;
    using const_iterator = container_type::const_iterator;
    /** Constructs an empty sequence. */
    DoubleView() = default;
    /** Returns a copy of all converted doubles. */
    [[nodiscard]] std::vector<double> collect() const;
    /** Returns element count. */
    [[nodiscard]] std::size_t count() const;
    /** Returns whether no elements are present. */
    [[nodiscard]] bool empty() const;
    /** Returns stable iterator owned by this view and its copies. */
    [[nodiscard]] const_iterator begin() const;
    /** Returns stable sentinel. */
    [[nodiscard]] const_iterator end() const;
    /** Returns stable iterator. */
    [[nodiscard]] const_iterator cbegin() const;
    /** Returns stable sentinel. */
    [[nodiscard]] const_iterator cend() const;
private:
    explicit DoubleView(std::shared_ptr<const container_type> values) noexcept;
    std::shared_ptr<const container_type> values_;
    friend class ValueView;
};

/** Owning materialized boolean sequence produced by ValueView conversion. */
class BoolView {
public:
    using container_type = std::vector<bool>;
    using const_iterator = container_type::const_iterator;
    /** Constructs an empty sequence. */
    BoolView() = default;
    /** Returns a copy of all converted booleans. */
    [[nodiscard]] std::vector<bool> collect() const;
    /** Returns element count. */
    [[nodiscard]] std::size_t count() const;
    /** Returns whether no elements are present. */
    [[nodiscard]] bool empty() const;
    /** Returns stable iterator owned by this view and its copies. */
    [[nodiscard]] const_iterator begin() const;
    /** Returns stable sentinel. */
    [[nodiscard]] const_iterator end() const;
    /** Returns stable iterator. */
    [[nodiscard]] const_iterator cbegin() const;
    /** Returns stable sentinel. */
    [[nodiscard]] const_iterator cend() const;
private:
    explicit BoolView(std::shared_ptr<const container_type> values) noexcept;
    std::shared_ptr<const container_type> values_;
    friend class ValueView;
};

} // namespace quickxlsx
