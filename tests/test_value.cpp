#include "test_helper.hpp"

#include <chrono>
#include <cstdint>
#include <limits>

using namespace quickxlsx;
using namespace test;

TEST(value_types_factories_and_comparison) {
    CHECK(Value().type() == ValueType::Null);
    CHECK(Value(nullptr).is_null());
    CHECK(Value::boolean(true) == Value(true));
    CHECK(Value::integer(-7) == Value(std::int32_t{-7}));
    CHECK(Value::floating(2.5) == Value(2.5f));
    CHECK(Value::string("text") == Value(std::string_view("text")));
    const auto instant = Value::DateTime(std::chrono::seconds(1234));
    CHECK(Value::datetime(instant).is_datetime());
    CHECK(Value::datetime(instant) != Value::integer(1234));
    CHECK(Value(static_cast<const char*>(nullptr)).is_null());
    CHECK(Value(std::numeric_limits<std::uint64_t>::max()).as_int64() == std::numeric_limits<std::int64_t>::max());
}

TEST(value_safe_and_unchecked_conversions) {
    CHECK(Value().as_bool(true));
    CHECK(Value(true).as_int64() == 1);
    CHECK(Value(false).as_double() == 0.0);
    CHECK(Value(-3).as_string() == "-3");
    CHECK(Value(1.25).as_string() == "1.25");
    CHECK(Value("TRUE").as_bool());
    CHECK(!Value("no").as_bool(true));
    CHECK(Value("42").as_int64() == 42);
    CHECK(Value("2.75").as_double() == 2.75);
    CHECK(Value("bad").as_int64(9) == 9);
    CHECK(Value("bad").as_double(4.5) == 4.5);
    CHECK(Value("bad").as_bool(true));
    CHECK(Value(true).as_bool_unchecked());
    CHECK(Value(std::int64_t{8}).as_int64_unchecked() == 8);
    CHECK(Value(3.5).as_double_unchecked() == 3.5);
    CHECK(Value("owned").as_string_unchecked() == "owned");
}

TEST(value_datetime_conversions) {
    const auto epoch = std::chrono::sys_days(std::chrono::year{1899}/12/30);
    CHECK(Value(epoch).as_double() == 0.0);
    CHECK(Value(2.0).as_datetime() == epoch + std::chrono::days(2));
    const auto unix_second = Value::DateTime(std::chrono::seconds(123));
    CHECK(Value(unix_second).as_int64() == 123);
    DateTimeOptions options{"%Y-%m-%d", false};
    CHECK(Value(std::chrono::sys_days(std::chrono::year{2024}/1/2)).as_string(options) == "2024-01-02");
    CHECK(Value("not a date").as_datetime() == Value::DateTime{});
}

TEST(value_blank_policies_and_replacement) {
    CHECK(Value().is_blank(BlankPolicy::NullOnly));
    CHECK(!Value("").is_blank(BlankPolicy::NullOnly));
    CHECK(Value("").is_blank());
    CHECK(!Value(" \t").is_blank(BlankPolicy::NullAndEmpty));
    CHECK(Value(" \t\r\n").is_blank(BlankPolicy::NullEmptyAndWhitespace));
    CHECK(!Value(" x ").is_blank(BlankPolicy::NullEmptyAndWhitespace));
    CHECK(Value().blank_to(7) == Value(7));
    CHECK(Value("x").blank_to(7) == Value("x"));
    CHECK(Value("").blank_to_string("missing") == "missing");
}
