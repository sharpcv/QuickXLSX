#include <OpenXLSX.hpp>

#include <cstddef>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
struct Counts {
    std::size_t rows = 0;
    std::size_t cells = 0;
};

// OpenXLSX is an in-memory (DOM) engine only: open() parses the full workbook XML
// tree before iteration. Absent cells within a row's rectangular span read back as
// XLValueType::Empty, so those are excluded to count stored cells only.
Counts read(const std::filesystem::path& input) {
    OpenXLSX::XLDocument document;
    document.open(input.string());
    auto sheet = document.workbook().worksheet(1);

    Counts counts;
    for (auto row : sheet.rows()) {
        ++counts.rows;
        for (auto cell : row.cells()) {
            if (cell.value().type() != OpenXLSX::XLValueType::Empty) ++counts.cells;
        }
    }
    document.close();
    return counts;
}

void copy_value(const OpenXLSX::XLCell& source, OpenXLSX::XLCellAssignable destination) {
    const auto& value = source.value();
    switch (value.type()) {
    case OpenXLSX::XLValueType::Boolean: destination.value() = value.get<bool>(); break;
    case OpenXLSX::XLValueType::Integer: destination.value() = value.get<std::int64_t>(); break;
    case OpenXLSX::XLValueType::Float: destination.value() = value.get<double>(); break;
    case OpenXLSX::XLValueType::String: destination.value() = value.get<std::string>(); break;
    case OpenXLSX::XLValueType::Error: destination.value() = value.get<std::string>(); break;
    case OpenXLSX::XLValueType::Empty: break;
    }
}

std::size_t filter_write(const std::filesystem::path& input, const std::filesystem::path& output) {
    OpenXLSX::XLDocument source;
    source.open(input.string());
    auto source_sheet = source.workbook().worksheet(1);

    OpenXLSX::XLDocument destination;
    destination.create(output.string(), OpenXLSX::XLForceOverwrite);
    auto destination_sheet = destination.workbook().worksheet(1);
    destination_sheet.setName(source_sheet.name());

    std::uint16_t match_col = 0;
    bool header = true;
    std::uint32_t out_row = 1;
    std::size_t matches = 0;
    for (auto row : source_sheet.rows()) {
        if (header) {
            for (auto cell : row.cells()) {
                if (cell.value().type() == OpenXLSX::XLValueType::String &&
                    cell.value().get<std::string>() == "结算客户名称") {
                    match_col = cell.cellReference().column();
                    break;
                }
            }
            for (auto cell : row.cells())
                copy_value(cell, destination_sheet.cell(out_row, cell.cellReference().column()));
            ++out_row;
            header = false;
            continue;
        }
        auto match_cell = source_sheet.cell(row.rowNumber(), match_col);
        if (match_cell.value().type() == OpenXLSX::XLValueType::String &&
            match_cell.value().get<std::string>().find("西子电商") != std::string::npos) {
            for (auto cell : row.cells())
                copy_value(cell, destination_sheet.cell(out_row, cell.cellReference().column()));
            ++out_row;
            ++matches;
        }
    }
    destination.save();
    destination.close();
    source.close();
    return matches;
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: benchmark_openxlsx read INPUT | filterwrite INPUT OUTPUT\n";
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
