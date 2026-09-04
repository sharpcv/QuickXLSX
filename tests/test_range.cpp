#include "test_helper.hpp"

using namespace quickxlsx;
using namespace quickxlsx::errors;
using namespace test;

TEST(cell_ref_forms_limits_and_errors) {
    CHECK(CellRef("A1") == CellRef::from_index(0, 0));
    CHECK(CellRef("Z9").column() == 25);
    CHECK(CellRef("AA10").to_string() == "AA10");
    CHECK(CellRef("XFD1048576") == CellRef(1048575, 16383));
    for (const auto bad : {"", "a1", "A", "1", "A0", "A-1", "XFE1", "A1048577"})
        check_throws<InvalidRange>([&] { (void)CellRef(bad); });
    check_throws<InvalidRange>([] { (void)CellRef(CellRef::max_rows, 0); });
}

TEST(range_forms_bounds_and_sparse_ownership) {
    Worksheet sheet("Data");
    sheet[2].set(3, "x");
    auto single = sheet.range("A1");
    CHECK(single.is_single_cell() && single.is_single_column() && single.is_single_row());
    CHECK(single.to_string() == "A1" && single.value(0, 0).is_null());
    auto column = sheet.range("D1:D3");
    CHECK(column.row_count() == 3 && column.col_count() == 1 && column.is_single_column());
    auto block = sheet.range("B2:E4");
    CHECK(block.top() == 1 && block.left() == 1 && block.bottom() == 4 && block.right() == 5);
    auto values = block.values();
    CHECK(values.size() == 12 && values[6].as_string() == "x");
    auto cell = block.cell(1, 2);
    sheet[2].set(3, "changed");
    CHECK(cell.column() == 2 && cell.as_string() == "x");
    auto rows = block.rows();
    CHECK(rows.size() == 3 && rows[1].index() == 1 && rows[1].size() == 4);
    CHECK(block.cells().size() == 12);
}

TEST(range_coordinate_errors) {
    Worksheet sheet("Data");
    for (const auto bad : {"", ":", "A1:", ":A1", "B2:A1", "A2:A1", "A1:B2:C3", "XFE1"})
        check_throws<InvalidRange>([&] { (void)sheet.range(bad); });
    check_throws<InvalidRange>([&] { (void)sheet.range(2, 0, 1, 1); });
    check_throws<InvalidRange>([&] { (void)sheet.range(0, 0, 0, 1); });
    auto range = sheet.range("A1:B2");
    check_throws<InvalidRange>([&] { (void)range.value(2, 0); });
    check_throws<InvalidRange>([&] { (void)range.cell(0, 2); });
}

TEST(row_worksheet_and_workbook_sparse_errors) {
    Row row(4, {1, 2});
    CHECK(row.index() == 4 && row.column_count() == 2);
    row.set(4, "tail");
    CHECK(static_cast<const Row&>(row)[3].is_null() && row.values().size() == 5);
    const Row& read_only = row;
    CHECK(read_only[99].is_null());
    check_throws<InvalidColumnIndex>([&] { (void)read_only.at(3); });
    CHECK(row.erase(1) && !row.erase(1));

    Worksheet sheet("Valid");
    sheet.insert(Row(5, {"five"}));
    sheet.insert(Row(1, {"one"}));
    CHECK(sheet.begin()->index() == 1 && sheet.column_count() == 1);
    check_throws<InvalidRowIndex>([&] { (void)static_cast<const Worksheet&>(sheet)[2]; });
    CHECK(sheet.erase(5) && !sheet.erase(5));
    check_throws<InvalidWorksheetName>([] { (void)Worksheet("bad/name"); });

    Workbook book;
    book.add_sheet("One"); book.add_sheet("Two");
    CHECK(book.contains_sheet("one") && book.sheet_names().size() == 2);
    check_throws<InvalidWorksheetName>([&] { book.add_sheet("ONE"); });
    check_throws<WorksheetNotFound>([&] { (void)book.sheet(2); });
    CHECK(book.remove_sheet("two") && !book.remove_sheet("missing"));
}
