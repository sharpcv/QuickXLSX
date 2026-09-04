#include <xlnt/xlnt.hpp>

#include <cstddef>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct Counts {
    std::size_t rows = 0;
    std::size_t cells = 0;
};

bool string_like(xlnt::cell::type type) {
    return type == xlnt::cell::type::inline_string || type == xlnt::cell::type::shared_string ||
           type == xlnt::cell::type::formula_string;
}

// xlnt's streaming reader is a cell-level pull API: begin_worksheet() then
// read_cell() until has_cell() is false. Cells arrive in row-major order, so
// row boundaries are tracked by cell.row() changes. Empty cells (no <c> value)
// are excluded via data_type() == empty.
Counts read(const std::filesystem::path& input) {
    xlnt::streaming_workbook_reader reader;
    reader.open(input.string());
    Counts counts;
    for (const std::string& title : reader.sheet_titles()) {
        reader.begin_worksheet(title);
        xlnt::row_t last_row = 0;
        while (reader.has_cell()) {
            const xlnt::cell cell = reader.read_cell();
            if (cell.row() != last_row) {
                last_row = cell.row();
                ++counts.rows;
            }
            if (cell.data_type() != xlnt::cell::type::empty) ++counts.cells;
        }
        reader.end_worksheet();
    }
    reader.close();
    return counts;
}

// Buffered value: xlnt's streaming reader hands back cells that alias an
// internal slot freed on the next read, so values are extracted eagerly.
struct CellValue {
    std::uint32_t column = 0;
    xlnt::cell::type type = xlnt::cell::type::empty;
    double number = 0.0;
    std::string text;
};

CellValue extract(const xlnt::cell& cell) {
    CellValue value;
    value.column = cell.column().index;
    value.type = cell.data_type();
    switch (value.type) {
    case xlnt::cell::type::shared_string:
    case xlnt::cell::type::inline_string:
    case xlnt::cell::type::formula_string:
    case xlnt::cell::type::error:
        value.text = cell.value<std::string>();
        break;
    case xlnt::cell::type::number:
    case xlnt::cell::type::date:
        value.number = cell.value<double>();
        break;
    case xlnt::cell::type::boolean:
        value.number = cell.value<bool>() ? 1.0 : 0.0;
        break;
    case xlnt::cell::type::empty:
        break;
    }
    return value;
}

void write_value(xlnt::cell& out_cell, const CellValue& value) {
    switch (value.type) {
    case xlnt::cell::type::shared_string:
    case xlnt::cell::type::inline_string:
    case xlnt::cell::type::formula_string:
    case xlnt::cell::type::error:
        out_cell.value(value.text);
        break;
    case xlnt::cell::type::number:
    case xlnt::cell::type::date:
        out_cell.value(value.number);
        break;
    case xlnt::cell::type::boolean:
        out_cell.value(value.number != 0.0);
        break;
    case xlnt::cell::type::empty:
        break;
    }
}

// Filter + write. Reads are cell-level, so the current row is buffered until
// its last cell, then matched against the "结算客户名称" column. On a hit the
// row is replayed densely. xlnt's streaming_workbook_writer does not serialize
// cells in 2022.12.04 (add_cell writes an unsaved slot), so the write side
// falls back to the DOM workbook.
std::size_t filter_write(const std::filesystem::path& input, const std::filesystem::path& output) {
    xlnt::streaming_workbook_reader reader;
    reader.open(input.string());
    xlnt::workbook output_workbook;
    xlnt::worksheet output_sheet = output_workbook.active_sheet();

    std::size_t matches = 0;
    for (const std::string& title : reader.sheet_titles()) {
        reader.begin_worksheet(title);
        output_sheet.title(title);

        std::vector<CellValue> current_row;
        xlnt::row_t current_row_num = 0;
        bool header_done = false;
        std::uint32_t match_col = 0;
        std::uint32_t out_row = 1;

        auto flush = [&]() {
            if (current_row.empty()) return;
            bool write_row = false;
            if (!header_done) {
                for (const CellValue& value : current_row) {
                    if (string_like(value.type) && value.text == "结算客户名称") {
                        match_col = value.column;
                        break;
                    }
                }
                write_row = true;
            } else {
                for (const CellValue& value : current_row) {
                    if (value.column == match_col) {
                        if (string_like(value.type) && value.text.find("西子电商") != std::string::npos)
                            write_row = true;
                        break;
                    }
                }
            }
            if (write_row) {
                for (const CellValue& value : current_row) {
                    xlnt::cell out_cell = output_sheet.cell(
                        xlnt::cell_reference(xlnt::column_t(value.column), static_cast<xlnt::row_t>(out_row)));
                    write_value(out_cell, value);
                }
                ++out_row;
                if (header_done) ++matches;
            }
            if (!header_done) header_done = true;
            current_row.clear();
        };

        while (reader.has_cell()) {
            const xlnt::cell cell = reader.read_cell();
            if (cell.row() != current_row_num) {
                flush();
                current_row_num = cell.row();
            }
            current_row.push_back(extract(cell));
        }
        flush();
        reader.end_worksheet();
    }
    reader.close();
    output_workbook.save(output.string());
    return matches;
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: benchmark_xlnt read INPUT | filterwrite INPUT OUTPUT\n";
        return 2;
    }
    try {
        const std::string mode = argv[1];
        const std::filesystem::path input = argv[2];
        if (mode == "read") {
            const Counts counts = read(input);
            std::cout << "rows=" << counts.rows << " cells=" << counts.cells << '\n';
            return counts.rows == 0 || counts.cells == 0 ? 1 : 0;
        }
        if (mode == "filterwrite" && argc == 4) {
            const std::size_t matches = filter_write(input, std::filesystem::path(argv[3]));
            std::cout << "matches=" << matches << '\n';
            return matches == 0 ? 1 : 0;
        }
        throw std::runtime_error("invalid mode");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
