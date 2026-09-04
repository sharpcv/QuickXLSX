#include "test_helper.hpp"

#ifdef QUICKXLSX_ENABLE_XLSX

using namespace quickxlsx;
using namespace quickxlsx::errors;
using namespace test;

TEST(xlsx_dom_values_multiple_sheets_and_config) {
    TempFile file(".xlsx");
    Workbook book;
    auto& first = book.add_sheet("One");
    first[0].set(0, "shared"); first[0].set(1, 42); first[0].set(2, true); first[0].set(3, Value{});
    first[2].set(0, "shared");
    auto& second = book.add_sheet("Two"); second[3].set(2, 3.25); second[3].set(4, "inline");
    book.save(file.path().string());

    XLSXConfig config; config.skip_empty_rows = false;
    auto loaded = Workbook::open(file.path().string(), config);
    CHECK(loaded.sheet_names() == std::vector<std::string>({"One", "Two"}));
    CHECK(loaded.sheet("One")[0][0].as_string() == "shared" && loaded.sheet("One")[0][1].as_int64() == 42);
    CHECK(loaded.sheet("One")[0][2].as_bool() && loaded.sheet("Two")[3][2].as_double() == 3.25);
}

TEST(xlsx_stream_writer_reader_and_generator) {
    TempFile file(".xlsx");
    Writer writer(file.path().string());
    writer.set_sheet_name("Stream"); writer.set_compression_level(1);
    writer.write_row("text", 7, false); writer.write_cell("tail").end_row();
    writer.close(); CHECK(!writer.is_open());

    Reader reader(file.path().string());
    CHECK(reader.sheet_names() == std::vector<std::string>({"Stream"}));
    auto rows = read_all(reader);
    CHECK(rows.size() == 2 && rows[0][1].as_int64() == 7 && rows[1][0].as_string() == "tail");
    Reader generated(file.path().string());
    std::size_t count = 0; for (const auto& row : generated.rows("Stream")) { (void)row; ++count; }
    CHECK(count == 2);
    check_throws<WorksheetNotFound>([&] { reader.read_rows("Missing", [](const Row&) {}); });
}

TEST(xlsx_export_to_csv_and_corruption_errors) {
    TempFile xlsx(".xlsx"); TempFile csv(".csv");
    Workbook book; book.add_sheet("Data")[0].set(0, "a,b"); book.save(xlsx.path().string());
    auto loaded = Workbook::open(xlsx.path().string()); loaded.export_csv("Data", csv.path().string());
    Reader reader(csv.path().string()); CHECK(read_all(reader)[0][0].as_string() == "a,b");
    TempFile bad(".xlsx"); write_file(bad.path(), "not a zip archive");
    check_throws<Error>([&] { (void)Workbook::open(bad.path().string()); });
}

TEST(xlsx_utf8_paths_and_vector_row_overload) {
    const auto directory = fs::temp_directory_path();
    auto xlsx = TempFile::from_path(directory / (L"quickxlsx_\u5ba2\u6237\u4fe1\u606f\u8868_" + std::to_wstring(1) + L".xlsx"));
    auto csv = TempFile::from_path(directory / (L"quickxlsx_\u5ba2\u6237\u4fe1\u606f\u8868_" + std::to_wstring(1) + L".csv"));
    std::error_code ignored;
    fs::remove(xlsx.path(), ignored);
    fs::remove(csv.path(), ignored);

    const auto utf8 = [](const fs::path& path) {
        const auto bytes = path.u8string();
        return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    };
    const auto xlsx_path = utf8(xlsx.path());
    const auto csv_path = utf8(csv.path());
    const std::vector<Value> values = {Value("utf8"), Value(42)};

    Writer writer(xlsx_path);
    writer.write_row(values);
    writer.close();

    Reader reader(xlsx_path);
    const auto rows = read_all(reader);
    CHECK(rows.size() == 1 && rows[0][0].as_string() == "utf8" && rows[0][1].as_int64() == 42);

    const auto book = Workbook::open(xlsx_path);
    book.export_csv(0, csv_path);
    Reader csv_reader(csv_path);
    CHECK(read_all(csv_reader)[0][0].as_string() == "utf8");
}

#endif
