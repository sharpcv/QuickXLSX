#include <quickxlsx/quickxlsx.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <string>

namespace {
using Clock = std::chrono::steady_clock;
template<typename Function>
double seconds(Function&& function) {
    const auto start = Clock::now();
    function();
    return std::chrono::duration<double>(Clock::now() - start).count();
}
} // namespace

int main(int argc, char** argv) {
    const std::size_t generated_rows = argc > 1 ? std::stoull(argv[1]) : 1'000'000;
    const auto path = std::filesystem::temp_directory_path() / "quickxlsx-benchmark.csv";
    const double write_time = seconds([&] {
        quickxlsx::Writer writer(path.string());
        for (std::size_t row = 0; row < generated_rows; ++row)
            writer.write_row(row, "benchmark text", row * 0.25, (row & 1U) != 0);
        writer.close();
    });
    std::size_t read_rows = 0;
    std::size_t read_cells = 0;
    const double read_time = seconds([&] {
        quickxlsx::Reader reader(path.string());
        reader.read_rows([&](const quickxlsx::Row& row) {
            ++read_rows;
            read_cells += row.size();
        });
    });
    const auto bytes = std::filesystem::file_size(path);
    std::filesystem::remove(path);
    std::cout << "rows=" << read_rows << " cells=" << read_cells << " bytes=" << bytes << '\n'
              << "write_seconds=" << write_time << " write_rows_per_second="
              << generated_rows / write_time << '\n'
              << "read_seconds=" << read_time << " read_rows_per_second="
              << read_rows / read_time << '\n';
    return read_rows == generated_rows ? 0 : 1;
}
