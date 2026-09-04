#include "test_helper.hpp"

#include <algorithm>

using namespace quickxlsx;
using namespace test;

static Worksheet sample_sheet() {
    Worksheet sheet("View");
    sheet[0].set(0, 3); sheet[0].set(1, "c");
    sheet[2].set(0, 1); sheet[2].set(1, "a");
    sheet[4].set(0, 2); sheet[4].set(1, "b");
    sheet[6].set(0, 2); sheet[6].set(1, "b");
    return sheet;
}

TEST(worksheet_and_row_view_operations) {
    auto sheet = sample_sheet();
    auto rows = sheet.rows();
    CHECK(rows.count() == 4 && !rows.empty());
    CHECK(rows.filter([](const Row& row) { return row[0].as_int64() >= 2; }).count() == 3);
    CHECK(rows.take(2).count() == 2 && rows.take(99).count() == 4);
    CHECK(rows.skip(2).count() == 2 && rows.skip(99).empty());
    CHECK(rows.slice(1, 2).begin()->index() == 2);
    auto ascending = rows.sort_by_column(0);
    CHECK(ascending.begin()->index() == 2);
    auto descending = rows.sort_by_column(0, false);
    CHECK(descending.begin()->index() == 0);
    CHECK(rows.distinct_count() == 3 && rows.distinct_count({0}) == 3);
    auto projected = rows.columns({1, 0});
    CHECK(projected.begin()->index() == 0 && (*projected.begin())[0].as_string() == "c" && (*projected.begin())[1].as_int64() == 3);
}

TEST(cell_and_value_view_operations) {
    auto sheet = sample_sheet();
    auto cells = sheet.rows().take(2).cells();
    CHECK(cells.count() == 4 && cells.take(1).count() == 1 && cells.skip(3).count() == 1);
    CHECK(cells.filter([](const Cell& cell) { return cell.column() == 1; }).count() == 2);
    CHECK(cells.collect().size() == 4);
    auto values = sheet.column(0);
    CHECK(values.count() == 4);
    CHECK(values.filter([](const Value& value) { return value.as_int64() == 2; }).count() == 2);
    CHECK((values.map([](const Value& value) { return value.as_int64() * 10; }).as_ints().collect() == std::vector<std::int64_t>{30, 10, 20, 20}));
    CHECK((values.as_strings().collect() == std::vector<std::string>{"3", "1", "2", "2"}));
    CHECK((values.as_doubles().collect() == std::vector<double>{3, 1, 2, 2}));
    CHECK((values.as_bools().collect() == std::vector<bool>{true, true, true, true}));
    CHECK(values.take(2).skip(1).collect().front().as_int64() == 1);
}

TEST(view_laziness_and_snapshot_iteration) {
    Worksheet sheet("Lazy");
    sheet[0].set(0, 1);
    auto before_evaluation = sheet.rows();
    sheet[1].set(0, 2);
    CHECK(before_evaluation.count() == 2);
    auto snapshot = sheet.rows();
    auto first = snapshot.begin();
    sheet[2].set(0, 3);
    CHECK(snapshot.count() == 2 && first->index() == 0);
    auto copied = snapshot;
    CHECK(std::distance(copied.begin(), copied.end()) == 2);
    CHECK(sheet.rows().count() == 3);
    CHECK(sheet.range("A1:A3").view().column(0).collect().size() == 3);
}

TEST(default_views_are_empty) {
    CHECK(RowView{}.empty()); CHECK(CellView{}.empty()); CHECK(ValueView{}.empty());
    CHECK(StringView{}.empty()); CHECK(IntView{}.empty()); CHECK(DoubleView{}.empty()); CHECK(BoolView{}.empty());
}
