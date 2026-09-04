#include "test_helper.hpp"

#include <utility>

using namespace quickxlsx;
using namespace quickxlsx::errors;
using namespace test;

TEST(csv_quotes_newlines_bom_utf8_header_and_empty_fields) {
    TempFile file(".csv");
    write_file(file.path(), "\xEF\xBB\xBFh,c,t\r\nDoe,\"hello, \"\"world\"\"\",你好\r\n\"multi\nline\",,\"\"\r\n,,\r\n");
    CSVConfig config; config.has_header = true; config.skip_empty_rows = false;
    Reader reader(file.path().string(), config);
    auto rows = read_all(reader);
    CHECK(rows.size() == 3 && rows[0].index() == 1);
    CHECK(rows[0][1].as_string() == "hello, \"world\"" && rows[0][2].as_string() == "你好");
    CHECK(rows[1][0].as_string() == "multi\nline" && rows[1][1].value().is_null());
    CHECK(rows[1][2].value().is_string() && rows[1][2].as_string().empty());
}

TEST(csv_delimiter_blank_policy_limits_empty_and_long_fields) {
    TempFile file(".csv");
    std::string long_text(200000, 'x');
    write_file(file.path(), "a;1\n ; \n" + long_text + ";2\n");
    CSVConfig config; config.delimiter = ';'; config.blank_policy = BlankPolicy::NullEmptyAndWhitespace;
    Reader reader(file.path().string(), config);
    auto rows = read_all(reader);
    CHECK(rows.size() == 2 && rows[1][0].as_string() == long_text);
    Reader limited(file.path().string(), config); limited.set_max_rows(1);
    CHECK(read_all(limited).size() == 1);
    TempFile empty(".csv"); write_file(empty.path(), "");
    Reader empty_reader(empty.path().string()); CHECK(read_all(empty_reader).empty());
}

TEST(csv_writer_roundtrip_lifecycle_and_generator) {
    TempFile file(".csv");
    Writer writer(file.path().string());
    writer.set_delimiter(';');
    writer.write_cell("a;b").write_cell("q\"z").write_null().write_empty_string().flush();
    writer.write_row("line\nfeed", 42, true);
    CHECK(writer.is_open()); writer.close(); CHECK(!writer.is_open());
    check_throws<WriteError>([&] { writer.write_cell(1); });

    CSVConfig config; config.delimiter = ';'; config.skip_empty_rows = false;
    Reader reader(file.path().string(), config);
    CHECK(reader.sheet_count() == 1 && !reader.sheet_names().front().empty());
    auto rows = read_all(reader);
    CHECK(rows.size() == 2 && rows[0][0].as_string() == "a;b" && rows[0][1].as_string() == "q\"z");
    CHECK(rows[0][2].value().is_null() && rows[0][3].value().is_string() && rows[1][0].as_string() == "line\nfeed");

    Reader generated(file.path().string(), config);
    std::size_t count = 0; for (const auto& row : generated.rows()) { CHECK(row.index() == count); ++count; }
    CHECK(count == 2);
    Reader moved(std::move(generated)); CHECK(moved.is_open() && !generated.is_open());
    moved.close(); CHECK(!moved.is_open());
    check_throws<ReadError>([&] { (void)moved.sheet_count(); });
}

TEST(csv_dom_import_export_and_errors) {
    TempFile input(".csv"); write_file(input.path(), "a,b\n1,2\n");
    auto book = Workbook::open(input.path().string());
    CHECK(book.sheet_count() == 1 && book.sheet(0)[1][1].as_string() == "2");
    TempFile output(".csv"); book.export_csv(0, output.path().string());
    CHECK(fs::file_size(output.path()) != 0);
    check_throws<WorksheetNotFound>([&] { book.export_csv("missing", output.path().string()); });

    TempFile malformed(".csv"); write_file(malformed.path(), "a,\"unterminated\n");
    check_throws<CSVParseError>([&] { Reader reader(malformed.path().string()); (void)read_all(reader); });
    check_throws<CSVParseError>([&] { CSVConfig bad; bad.delimiter = '"'; Reader reader(input.path().string(), bad); (void)read_all(reader); });
    check_throws<FileNotFound>([] { Reader reader("/definitely/not/quickxlsx.csv"); (void)read_all(reader); });
    check_throws<UnsupportedFeature>([] { Reader reader("input.unknown"); });
    Reader reader(input.path().string());
    check_throws<ReadError>([&] { reader.read_rows(Reader::RowCallback{}); });
    check_throws<WorksheetNotFound>([&] { reader.read_rows("missing", [](const Row&) {}); });
}
