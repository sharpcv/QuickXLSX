#include "quickxlsx/view.hpp"

#include "quickxlsx/worksheet.hpp"

#include <algorithm>
#include <utility>

namespace quickxlsx {
namespace {

template<class T>
const std::vector<T>& empty_vector() {
    static const std::vector<T> empty;
    return empty;
}

template<class T>
std::vector<T> take_items(const std::vector<T>& source, std::size_t n) {
    n = std::min(n, source.size());
    return {source.begin(), source.begin() + static_cast<std::ptrdiff_t>(n)};
}

template<class T>
std::vector<T> skip_items(const std::vector<T>& source, std::size_t n) {
    n = std::min(n, source.size());
    return {source.begin() + static_cast<std::ptrdiff_t>(n), source.end()};
}

// Cross-type ordering is deterministic by ValueType; values of the same type use natural order.

int compare_values(const Value& left, const Value& right) {
    if (left.type() == right.type()) {
        switch (left.type()) {
        case ValueType::Null: return 0;
        case ValueType::Boolean: return (left.as_bool() > right.as_bool()) - (left.as_bool() < right.as_bool());
        case ValueType::Integer: return (left.as_int64() > right.as_int64()) - (left.as_int64() < right.as_int64());
        case ValueType::Double: return (left.as_double() > right.as_double()) - (left.as_double() < right.as_double());
        case ValueType::String: return left.as_string().compare(right.as_string());
        case ValueType::DateTime: return (left.as_datetime() > right.as_datetime()) - (left.as_datetime() < right.as_datetime());
        }
    }
    return (left.type() > right.type()) - (left.type() < right.type());
}

} // namespace

// A pipeline stage is evaluated once on first observation. Sharing State across copied views
// keeps iterators stable and prevents repeated execution of expensive upstream transformations.
struct RowView::State {
    explicit State(Loader input) : loader(std::move(input)) {}
    Loader loader;
    mutable std::shared_ptr<const container_type> cache;
};

RowView::RowView(Loader loader) : state_(std::make_shared<State>(std::move(loader))) {}
RowView::RowView(std::shared_ptr<State> state) noexcept : state_(std::move(state)) {}

const RowView::container_type& RowView::data() const {
    if (!state_) return empty_vector<Row>();
    if (!state_->cache) state_->cache = std::make_shared<const container_type>(state_->loader());
    return *state_->cache;
}

RowView RowView::filter(RowFilter predicate) const {
    const RowView source = *this;
    return RowView([source, predicate = std::move(predicate)] {
        container_type output;
        for (const auto& row : source.data()) if (predicate(row)) output.push_back(row);
        return output;
    });
}
RowView RowView::take(std::size_t n) const {
    const RowView source = *this;
    return RowView([source, n] { return take_items(source.data(), n); });
}
RowView RowView::skip(std::size_t n) const {
    const RowView source = *this;
    return RowView([source, n] { return skip_items(source.data(), n); });
}
RowView RowView::slice(std::size_t start, std::size_t count) const { return skip(start).take(count); }
RowView RowView::sort_by_column(std::size_t column, bool ascending) const {
    const RowView source = *this;
    return RowView([source, column, ascending] {
        container_type output = source.data();
        std::stable_sort(output.begin(), output.end(), [column, ascending](const Row& a, const Row& b) {
            const int result = compare_values(a[column], b[column]);
            return ascending ? result < 0 : result > 0;
        });
        return output;
    });
}
CellView RowView::cells() const {
    const RowView source = *this;
    return CellView([source] {
        CellView::container_type output;
        for (const auto& row : source.data()) output.insert(output.end(), row.begin(), row.end());
        return output;
    });
}
ValueView RowView::column(std::size_t column) const {
    const RowView source = *this;
    return ValueView([source, column] {
        ValueView::container_type output;
        output.reserve(source.data().size());
        for (const auto& row : source.data()) output.push_back(row[column]);
        return output;
    });
}
RowView RowView::columns(const std::vector<std::size_t>& columns) const {
    const RowView source = *this;
    return RowView([source, columns] {
        container_type output;
        output.reserve(source.data().size());
        for (const auto& row : source.data()) {
            Row projected(row.index());
            for (std::size_t i = 0; i < columns.size(); ++i) projected.set(i, row[columns[i]]);
            output.push_back(std::move(projected));
        }
        return output;
    });
}
std::size_t RowView::count() const { return data().size(); }
bool RowView::empty() const { return data().empty(); }
RowView::const_iterator RowView::begin() const { return data().begin(); }
RowView::const_iterator RowView::end() const { return data().end(); }
RowView::const_iterator RowView::cbegin() const { return data().cbegin(); }
RowView::const_iterator RowView::cend() const { return data().cend(); }

WorksheetView::WorksheetView(const Worksheet& worksheet) noexcept
    : rows_(RowView([source = &worksheet] { return RowView::container_type(source->begin(), source->end()); })) {}
RowView WorksheetView::filter(RowFilter predicate) const { return rows_.filter(std::move(predicate)); }
RowView WorksheetView::take(std::size_t n) const { return rows_.take(n); }
RowView WorksheetView::skip(std::size_t n) const { return rows_.skip(n); }
RowView WorksheetView::slice(std::size_t start, std::size_t count) const { return rows_.slice(start, count); }
RowView WorksheetView::sort_by_column(std::size_t column, bool ascending) const { return rows_.sort_by_column(column, ascending); }
ValueView WorksheetView::column(std::size_t column) const { return rows_.column(column); }
RowView WorksheetView::columns(const std::vector<std::size_t>& columns) const { return rows_.columns(columns); }
std::size_t WorksheetView::count() const { return rows_.count(); }
bool WorksheetView::empty() const { return rows_.empty(); }
WorksheetView::const_iterator WorksheetView::begin() const { return rows_.begin(); }
WorksheetView::const_iterator WorksheetView::end() const { return rows_.end(); }
WorksheetView::const_iterator WorksheetView::cbegin() const { return rows_.cbegin(); }
WorksheetView::const_iterator WorksheetView::cend() const { return rows_.cend(); }
std::size_t WorksheetView::distinct_count(const std::vector<std::size_t>& columns) const {
    std::vector<std::vector<Value>> keys;
    for (const auto& row : rows_) {
        std::vector<Value> key;
        if (columns.empty()) {
            key = row.values();
        } else {
            key.reserve(columns.size());
            for (const auto column : columns) key.push_back(row[column]);
        }
        if (std::find(keys.begin(), keys.end(), key) == keys.end()) keys.push_back(std::move(key));
    }
    return keys.size();
}

struct CellView::State {
    explicit State(Loader input) : loader(std::move(input)) {}
    Loader loader;
    mutable std::shared_ptr<const container_type> cache;
};
CellView::CellView(Loader loader) : state_(std::make_shared<State>(std::move(loader))) {}
CellView::CellView(std::shared_ptr<State> state) noexcept : state_(std::move(state)) {}
const CellView::container_type& CellView::data() const {
    if (!state_) return empty_vector<Cell>();
    if (!state_->cache) state_->cache = std::make_shared<const container_type>(state_->loader());
    return *state_->cache;
}
CellView CellView::filter(CellFilter predicate) const {
    const CellView source = *this;
    return CellView([source, predicate = std::move(predicate)] {
        container_type output;
        for (const auto& cell : source.data()) if (predicate(cell)) output.push_back(cell);
        return output;
    });
}
CellView CellView::take(std::size_t n) const { const CellView source = *this; return CellView([source, n] { return take_items(source.data(), n); }); }
CellView CellView::skip(std::size_t n) const { const CellView source = *this; return CellView([source, n] { return skip_items(source.data(), n); }); }
ValueView CellView::values() const {
    const CellView source = *this;
    return ValueView([source] {
        ValueView::container_type output;
        output.reserve(source.data().size());
        for (const auto& cell : source.data()) output.push_back(cell.value());
        return output;
    });
}
std::vector<Value> CellView::collect() const {
    std::vector<Value> output;
    output.reserve(data().size());
    for (const auto& cell : data()) output.push_back(cell.value());
    return output;
}
std::size_t CellView::count() const { return data().size(); }
bool CellView::empty() const { return data().empty(); }
CellView::const_iterator CellView::begin() const { return data().begin(); }
CellView::const_iterator CellView::end() const { return data().end(); }
CellView::const_iterator CellView::cbegin() const { return data().cbegin(); }
CellView::const_iterator CellView::cend() const { return data().cend(); }

struct ValueView::State {
    explicit State(Loader input) : loader(std::move(input)) {}
    Loader loader;
    mutable std::shared_ptr<const container_type> cache;
};
ValueView::ValueView(Loader loader) : state_(std::make_shared<State>(std::move(loader))) {}
ValueView::ValueView(std::shared_ptr<State> state) noexcept : state_(std::move(state)) {}
const ValueView::container_type& ValueView::data() const {
    if (!state_) return empty_vector<Value>();
    if (!state_->cache) state_->cache = std::make_shared<const container_type>(state_->loader());
    return *state_->cache;
}
ValueView ValueView::filter(ValueFilter predicate) const {
    const ValueView source = *this;
    return ValueView([source, predicate = std::move(predicate)] {
        container_type output;
        for (const auto& value : source.data()) if (predicate(value)) output.push_back(value);
        return output;
    });
}
ValueView ValueView::map(ValueMapper mapper) const {
    const ValueView source = *this;
    return ValueView([source, mapper = std::move(mapper)] {
        container_type output;
        output.reserve(source.data().size());
        for (const auto& value : source.data()) output.push_back(mapper(value));
        return output;
    });
}
ValueView ValueView::take(std::size_t n) const { const ValueView source = *this; return ValueView([source, n] { return take_items(source.data(), n); }); }
ValueView ValueView::skip(std::size_t n) const { const ValueView source = *this; return ValueView([source, n] { return skip_items(source.data(), n); }); }
std::vector<Value> ValueView::collect() const { return data(); }
std::size_t ValueView::count() const { return data().size(); }
bool ValueView::empty() const { return data().empty(); }
ValueView::const_iterator ValueView::begin() const { return data().begin(); }
ValueView::const_iterator ValueView::end() const { return data().end(); }
ValueView::const_iterator ValueView::cbegin() const { return data().cbegin(); }
ValueView::const_iterator ValueView::cend() const { return data().cend(); }

StringView ValueView::as_strings() const {
    auto output = std::make_shared<StringView::container_type>();
    output->reserve(data().size());
    for (const auto& value : data()) output->push_back(value.as_string());
    return StringView(std::move(output));
}
IntView ValueView::as_ints() const {
    auto output = std::make_shared<IntView::container_type>();
    output->reserve(data().size());
    for (const auto& value : data()) output->push_back(value.as_int64());
    return IntView(std::move(output));
}
DoubleView ValueView::as_doubles() const {
    auto output = std::make_shared<DoubleView::container_type>();
    output->reserve(data().size());
    for (const auto& value : data()) output->push_back(value.as_double());
    return DoubleView(std::move(output));
}
BoolView ValueView::as_bools() const {
    auto output = std::make_shared<BoolView::container_type>();
    output->reserve(data().size());
    for (const auto& value : data()) output->push_back(value.as_bool());
    return BoolView(std::move(output));
}

#define QUICKXLSX_TYPED_VIEW(Type, Element) \
Type::Type(std::shared_ptr<const container_type> values) noexcept : values_(std::move(values)) {} \
std::vector<Element> Type::collect() const { return values_ ? *values_ : std::vector<Element>{}; } \
std::size_t Type::count() const { return values_ ? values_->size() : 0; } \
bool Type::empty() const { return !values_ || values_->empty(); } \
Type::const_iterator Type::begin() const { return values_ ? values_->begin() : empty_vector<Element>().begin(); } \
Type::const_iterator Type::end() const { return values_ ? values_->end() : empty_vector<Element>().end(); } \
Type::const_iterator Type::cbegin() const { return values_ ? values_->cbegin() : empty_vector<Element>().cbegin(); } \
Type::const_iterator Type::cend() const { return values_ ? values_->cend() : empty_vector<Element>().cend(); }

QUICKXLSX_TYPED_VIEW(StringView, std::string)
QUICKXLSX_TYPED_VIEW(IntView, std::int64_t)
QUICKXLSX_TYPED_VIEW(DoubleView, double)
QUICKXLSX_TYPED_VIEW(BoolView, bool)
#undef QUICKXLSX_TYPED_VIEW

} // namespace quickxlsx
