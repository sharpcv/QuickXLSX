#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {
struct Result {
    double seconds;
    long peak_rss_kib;
    std::string output;
};

Result run(const std::vector<std::string>& command) {
    int pipefd[2];
    if (pipe(pipefd) != 0) throw std::runtime_error("pipe failed: " + std::string(std::strerror(errno)));

    std::vector<char*> arguments;
    arguments.reserve(command.size() + 1);
    for (const auto& item : command) arguments.push_back(const_cast<char*>(item.c_str()));
    arguments.push_back(nullptr);

    const auto start = std::chrono::steady_clock::now();
    const pid_t child = fork();
    if (child < 0) throw std::runtime_error("fork failed: " + std::string(std::strerror(errno)));
    if (child == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        execv(arguments.front(), arguments.data());
        std::cerr << "exec failed: " << std::strerror(errno) << '\n';
        _exit(127);
    }

    close(pipefd[1]);
    std::string output;
    char buffer[4096];
    for (ssize_t bytes = read(pipefd[0], buffer, sizeof(buffer)); bytes > 0;
         bytes = read(pipefd[0], buffer, sizeof(buffer)))
        output.append(buffer, static_cast<std::size_t>(bytes));
    close(pipefd[0]);

    int status = 0;
    rusage usage{};
    if (wait4(child, &status, 0, &usage) < 0)
        throw std::runtime_error("wait4 failed: " + std::string(std::strerror(errno)));
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        throw std::runtime_error(command.front() + " failed (status " + std::to_string(status) +
                                 "): " + output);
    return {elapsed, usage.ru_maxrss, output};
}

std::filesystem::path executable_dir() {
    std::vector<char> buffer(4096);
    const auto size = readlink("/proc/self/exe", buffer.data(), buffer.size());
    if (size < 0) throw std::runtime_error("cannot resolve benchmark executable path");
    return std::filesystem::path(std::string(buffer.data(), static_cast<std::size_t>(size))).parent_path();
}

std::filesystem::path project_root(const std::filesystem::path& directory) {
    return directory.parent_path().parent_path().parent_path().parent_path();
}

struct Entry {
    std::string library;
    std::string mode;
    std::vector<std::string> command;
};
} // namespace

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        std::cerr << "usage: benchmark_compare SAMPLE.xlsx [REPETITIONS]\n";
        return 2;
    }
    try {
        const auto directory = executable_dir();
        const auto root = project_root(directory);
        const auto requested_input = std::filesystem::path(argv[1]);
        const auto input = requested_input.is_absolute() ? requested_input : root / requested_input;
        const int repetitions = argc == 3 ? std::stoi(argv[2]) : 1;
        if (repetitions < 1) throw std::runtime_error("repetitions must be positive");
        const auto input_string = input.string();

        const auto quickxlsx = (directory / "benchmark_quickxlsx").string();
        const auto openxlsx = (directory / "benchmark_openxlsx").string();
        const auto xlnt = (directory / "benchmark_xlnt").string();
        const auto python = (root / ".venv-benchmark/bin/python").string();
        const auto calamine = (root / "benchmarks/bench_calamine.py").string();

        const std::vector<Entry> read_entries = {
            {"QuickXLSX", "object", {quickxlsx, "object", input_string}},
            {"QuickXLSX", "stream", {quickxlsx, "stream", input_string}},
            {"OpenXLSX", "object", {openxlsx, "read", input_string}},
            {"xlnt", "stream", {xlnt, "read", input_string}},
            {"openpyxl", "calamine", {python, calamine, "read", input_string}},
        };

        std::cout << "library,mode,run,seconds,peak_rss_mib,rows,cells\n"
                  << std::fixed << std::setprecision(3);
        for (const auto& entry : read_entries) {
            for (int repetition = 1; repetition <= repetitions; ++repetition) {
                const Result result = run(entry.command);
                std::size_t rows = 0;
                std::size_t cells = 0;
                const auto rows_pos = result.output.find("rows=");
                const auto cells_pos = result.output.find("cells=");
                if (rows_pos != std::string::npos) rows = std::stoull(result.output.substr(rows_pos + 5));
                if (cells_pos != std::string::npos) cells = std::stoull(result.output.substr(cells_pos + 6));
                std::cout << entry.library << ',' << entry.mode << ',' << repetition << ',' << result.seconds
                          << ',' << result.peak_rss_kib / 1024.0 << ',' << rows << ',' << cells << '\n';
            }
        }

        // Filter + write scenario. OpenXLSX is excluded: its DOM write path does
        // not finish 31,800 rows in a practical time (see report).
        const std::vector<Entry> filter_entries = {
            {"QuickXLSX", "filterwrite", {quickxlsx, "filterwrite", input_string}},
            {"xlnt", "filterwrite", {xlnt, "filterwrite", input_string}},
            {"openpyxl", "filterwrite", {python, calamine, "filterwrite", input_string}},
        };

        std::cout << "\nlibrary,mode,run,seconds,peak_rss_mib,matches,output_bytes\n";
        for (const auto& entry : filter_entries) {
            for (int repetition = 1; repetition <= repetitions; ++repetition) {
                const auto output = std::filesystem::temp_directory_path() /
                    ("quickxlsx-filter-" + entry.library + "-" + std::to_string(repetition) + ".xlsx");
                std::filesystem::remove(output);
                auto command = entry.command;
                command.push_back(output.string());
                const Result result = run(command);
                std::size_t matches = 0;
                const auto matches_pos = result.output.find("matches=");
                if (matches_pos != std::string::npos) matches = std::stoull(result.output.substr(matches_pos + 8));
                const auto output_bytes = std::filesystem::file_size(output);
                std::cout << entry.library << ',' << entry.mode << ',' << repetition << ',' << result.seconds
                          << ',' << result.peak_rss_kib / 1024.0 << ',' << matches << ',' << output_bytes << '\n';
                std::filesystem::remove(output);
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
