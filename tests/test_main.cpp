#include "test_helper.hpp"

#include <exception>
#include <iostream>
#include <utility>

namespace test {
struct TestCase { std::string name; TestFunction function; };
static std::vector<TestCase>& tests() { static std::vector<TestCase> value; return value; }
void register_test(std::string name, TestFunction function) { tests().push_back({std::move(name), function}); }
} // namespace test

int main() {
    std::size_t failures = 0;
    for (const auto& entry : test::tests()) {
        try { entry.function(); }
        catch (const std::exception& error) { ++failures; std::cerr << "FAIL " << entry.name << ": " << error.what() << '\n'; }
        catch (...) { ++failures; std::cerr << "FAIL " << entry.name << ": unknown exception\n"; }
    }
    if (failures != 0) return 1;
    std::cout << test::tests().size() << " tests passed\n";
    return 0;
}
