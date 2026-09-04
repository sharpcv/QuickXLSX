#include "quickxlsx/errors.hpp"
#include "quickxlsx/row.hpp"
#include "quickxlsx/value.hpp"

#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace quickxlsx::detail {
namespace {

void write_field(std::ostream& output, std::string_view field, char delimiter) {
    const bool quote = field.find('"') != std::string_view::npos ||
                       field.find('\r') != std::string_view::npos ||
                       field.find('\n') != std::string_view::npos ||
                       field.find(delimiter) != std::string_view::npos;
    if (!quote) { output.write(field.data(), static_cast<std::streamsize>(field.size())); return; }
    output.put('"');
    std::size_t start = 0;
    for (std::size_t index = 0; index < field.size(); ++index) {
        if (field[index] != '"') continue;
        output.write(field.data() + start, static_cast<std::streamsize>(index - start));
        output.write("\"\"", 2);
        start = index + 1;
    }
    output.write(field.data() + start, static_cast<std::streamsize>(field.size() - start));
    output.put('"');
}

} // namespace

void write_csv_values(std::ostream& output, const std::vector<Value>& values, char delimiter,
                      const DateTimeOptions& datetime_options) {
    for (std::size_t column = 0; column < values.size(); ++column) {
        if (column != 0) output.put(delimiter);
        if (values[column].is_null()) continue;
        if (values[column].is_string() && values[column].as_string_unchecked().empty()) output.write("\"\"", 2);
        else write_field(output, values[column].as_string(datetime_options), delimiter);
    }
    output.write("\r\n", 2);
    if (!output) throw errors::WriteError("failed to write CSV row");
}

void write_csv_row(std::ostream& output, const Row& row, char delimiter,
                   const DateTimeOptions& datetime_options) {
    write_csv_values(output, row.values(), delimiter, datetime_options);
}

} // namespace quickxlsx::detail
