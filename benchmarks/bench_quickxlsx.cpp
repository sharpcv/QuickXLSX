#include <quickxlsx/quickxlsx.hpp>

#include <cstddef>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
struct Counts {
    std::size_t rows = 0;
    std::size_t cells = 0;
};

// DOM path: Workbook::open materializes every sheet and cell before returning.
// Iteration below is pure traversal over the fully populated object graph.
Counts object_read(const std::filesystem::path& input) {
    const quickxlsx::Workbook workbook = quickxlsx::Workbook::open(input.string());
    Counts counts;
    for (std::size_t index = 0; index < workbook.sheet_count(); ++index) {
        const quickxlsx::Worksheet& sheet = workbook.sheet(index);
        for (const quickxlsx::Row& row : sheet) {
            ++counts.rows;
            counts.cells += row.size();
        }
    }
    return counts;
}

// Stream path: rows are delivered one at a time and released after the callback.
// Only shared strings plus a single <row> subtree are resident at any moment.
Counts stream_read(const std::filesystem::path& input) {
    Counts counts;
    std::vector<std::string> names;
    {
        const quickxlsx::Reader probe(input.string());
        names = probe.sheet_names();
    }
    for (const std::string& name : names) {
        quickxlsx::Reader reader(input.string());
        reader.read_rows(name, [&](const quickxlsx::Row& row) {
            ++counts.rows;
            counts.cells += row.size();
        });
    }
    return counts;
}

// Read -> filter -> write pipeline. The "结算客户名称" column is located from the
// header, then rows whose value at that column contains "西子电商" are written
// densely (header at row 0) to the output. Streaming both sides keeps memory flat.
std::size_t filter_write(const std::filesystem::path& input, const std::filesystem::path& output) {
    std::string sheet_name;
    {
        quickxlsx::Reader probe(input.string());
        sheet_name = probe.sheet_names().front();
    }

    quickxlsx::Reader reader(input.string());
    quickxlsx::Writer writer(output.string());
    writer.set_sheet_name(sheet_name);

    std::size_t match_col = static_cast<std::size_t>(-1);
    std::size_t matches = 0;
    reader.read_rows([&](const quickxlsx::Row& row) {
        if (match_col == static_cast<std::size_t>(-1)) {
            for (const quickxlsx::Cell& cell : row) {
                if (cell.value().is_string() && cell.value().as_string_unchecked() == "结算客户名称") {
                    match_col = cell.column();
                    break;
                }
            }
            const std::vector<quickxlsx::Value> header = row.values();
            writer.write_row(header);
            return;
        }
        const quickxlsx::Value value = row[match_col];
        if (value.is_string() && value.as_string_unchecked().find("西子电商") != std::string_view::npos) {
            const std::vector<quickxlsx::Value> values = row.values();
            writer.write_row(values);
            ++matches;
        }
    });
    writer.close();
    return matches;
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: benchmark_quickxlsx object INPUT | stream INPUT | filterwrite INPUT OUTPUT\n";
        return 2;
    }
    try {
        const std::string mode = argv[1];
        const std::filesystem::path input = argv[2];
        if (mode == "object" || mode == "stream") {
            const Counts counts = mode == "object" ? object_read(input) : stream_read(input);
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
