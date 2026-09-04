#pragma once

#include <quickxlsx/quickxlsx.hpp>

#include <filesystem>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace test {
namespace fs = std::filesystem;

inline void check(bool condition, const char* expression, const char* file, int line) {
    if (!condition) throw std::runtime_error(std::string(file) + ":" + std::to_string(line) + ": CHECK(" + expression + ") failed");
}

#define CHECK(expression) ::test::check(static_cast<bool>(expression), #expression, __FILE__, __LINE__)

inline fs::path temp_path(std::string_view extension) {
    static std::size_t sequence = 0;
    return fs::temp_directory_path() / ("quickxlsx_test_" + std::to_string(++sequence) + std::string(extension));
}

class TempFile {
public:
    explicit TempFile(std::string_view extension) : path_(temp_path(extension)) {}
    static TempFile from_path(fs::path path) { return TempFile(std::move(path), 0); }
    ~TempFile() { std::error_code ignored; fs::remove(path_, ignored); }
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
    [[nodiscard]] const fs::path& path() const noexcept { return path_; }
private:
    TempFile(fs::path path, int) : path_(std::move(path)) {}
    fs::path path_;
};

inline void write_file(const fs::path& path, std::string_view bytes) {
    std::ofstream output(path, std::ios::binary);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!output) throw std::runtime_error("failed to prepare test file");
}

template<class Exception = quickxlsx::errors::Error, class Function>
void check_throws(Function&& function) {
    bool caught = false;
    try { std::forward<Function>(function)(); }
    catch (const Exception&) { caught = true; }
    CHECK(caught);
}

inline std::vector<quickxlsx::Row> read_all(quickxlsx::Reader& reader) {
    std::vector<quickxlsx::Row> rows;
    reader.read_rows([&](const quickxlsx::Row& row) { rows.push_back(row); });
    return rows;
}

using TestFunction = void (*)();
void register_test(std::string name, TestFunction function);

struct Registration {
    Registration(std::string name, TestFunction function) { register_test(std::move(name), function); }
};

#define TEST(name) \
    static void name(); \
    static const ::test::Registration name##_registration(#name, &name); \
    static void name()

} // namespace test
