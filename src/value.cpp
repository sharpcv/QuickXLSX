#include "quickxlsx/value.hpp"

#include <charconv>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <locale>
#include <sstream>

namespace quickxlsx {
namespace {

bool parse_bool(std::string_view text, bool& result) noexcept {
    if (text == "1" || text == "true" || text == "TRUE" || text == "True" ||
        text == "yes" || text == "YES" || text == "Yes") {
        result = true;
        return true;
    }
    if (text == "0" || text == "false" || text == "FALSE" || text == "False" ||
        text == "no" || text == "NO" || text == "No") {
        result = false;
        return true;
    }
    return false;
}

std::string format_double(double value) {
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value,
                                      std::chars_format::general);
    return result.ec == std::errc{} ? std::string(buffer, result.ptr) : std::string{};
}

std::tm calendar_time(std::time_t time, bool local) noexcept {
    std::tm result{};
#if defined(_WIN32)
    if (local) localtime_s(&result, &time); else gmtime_s(&result, &time);
#else
    if (local) localtime_r(&time, &result); else gmtime_r(&time, &result);
#endif
    return result;
}

} // namespace

ValueType Value::type() const noexcept { return static_cast<ValueType>(data_.index()); }
bool Value::is_null() const noexcept { return std::holds_alternative<std::monostate>(data_); }
bool Value::is_boolean() const noexcept { return std::holds_alternative<bool>(data_); }
bool Value::is_integer() const noexcept { return std::holds_alternative<std::int64_t>(data_); }
bool Value::is_double() const noexcept { return std::holds_alternative<double>(data_); }
bool Value::is_string() const noexcept { return std::holds_alternative<std::string>(data_); }
bool Value::is_datetime() const noexcept { return std::holds_alternative<DateTime>(data_); }
bool Value::is_numeric() const noexcept { return is_integer() || is_double(); }

bool Value::as_bool(bool fallback) const noexcept {
    if (is_null()) return fallback;
    if (const auto* value = std::get_if<bool>(&data_)) return *value;
    if (const auto* value = std::get_if<std::int64_t>(&data_)) return *value != 0;
    if (const auto* value = std::get_if<double>(&data_)) return *value != 0.0;
    if (const auto* value = std::get_if<std::string>(&data_)) {
        bool parsed = false;
        return parse_bool(*value, parsed) ? parsed : fallback;
    }
    return true;
}

std::int64_t Value::as_int64(std::int64_t fallback) const noexcept {
    if (is_null()) return fallback;
    if (const auto* value = std::get_if<bool>(&data_)) return *value ? 1 : 0;
    if (const auto* value = std::get_if<std::int64_t>(&data_)) return *value;
    if (const auto* value = std::get_if<double>(&data_)) {
        if (!std::isfinite(*value) || *value > static_cast<double>(std::numeric_limits<std::int64_t>::max()) ||
            *value < static_cast<double>(std::numeric_limits<std::int64_t>::min())) return fallback;
        return static_cast<std::int64_t>(*value);
    }
    if (const auto* value = std::get_if<std::string>(&data_)) {
        std::int64_t parsed{};
        const auto result = std::from_chars(value->data(), value->data() + value->size(), parsed);
        return result.ec == std::errc{} && result.ptr == value->data() + value->size() ? parsed : fallback;
    }
    return std::chrono::duration_cast<std::chrono::seconds>(std::get<DateTime>(data_).time_since_epoch()).count();
}

double Value::as_double(double fallback) const noexcept {
    if (is_null()) return fallback;
    if (const auto* value = std::get_if<bool>(&data_)) return *value ? 1.0 : 0.0;
    if (const auto* value = std::get_if<std::int64_t>(&data_)) return static_cast<double>(*value);
    if (const auto* value = std::get_if<double>(&data_)) return *value;
    if (const auto* value = std::get_if<std::string>(&data_)) {
        double parsed{};
        const auto result = std::from_chars(value->data(), value->data() + value->size(), parsed,
                                            std::chars_format::general);
        return result.ec == std::errc{} && result.ptr == value->data() + value->size() ? parsed : fallback;
    }
    constexpr double seconds_per_day = 86400.0;
    const auto epoch = std::chrono::sys_days(std::chrono::year{1899}/12/30);
    return std::chrono::duration<double>(std::get<DateTime>(data_) - epoch).count() / seconds_per_day;
}

std::string Value::as_string() const { return as_string(DateTimeOptions{}); }

std::string Value::as_string(const DateTimeOptions& options) const {
    if (is_null()) return {};
    if (const auto* value = std::get_if<bool>(&data_)) return *value ? "true" : "false";
    if (const auto* value = std::get_if<std::int64_t>(&data_)) return std::to_string(*value);
    if (const auto* value = std::get_if<double>(&data_)) return format_double(*value);
    if (const auto* value = std::get_if<std::string>(&data_)) return *value;
    const std::time_t time = std::chrono::system_clock::to_time_t(std::get<DateTime>(data_));
    const std::tm parts = calendar_time(time, options.local_time);
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    std::string format = options.format;
    if (format.size() >= 3 && format.starts_with("{:") && format.ends_with('}')) {
        format = format.substr(2, format.size() - 3);
    }
    stream << std::put_time(&parts, format.c_str());
    return stream.str();
}

Value::DateTime Value::as_datetime() const noexcept {
    if (const auto* value = std::get_if<DateTime>(&data_)) return *value;
    if (is_integer()) return DateTime(std::chrono::seconds(std::get<std::int64_t>(data_)));
    if (is_double()) {
        const auto epoch = std::chrono::sys_days(std::chrono::year{1899}/12/30);
        return epoch + std::chrono::duration_cast<DateTime::duration>(std::chrono::duration<double, std::ratio<86400>>(std::get<double>(data_)));
    }
    return {};
}

bool Value::as_bool_unchecked() const noexcept { return std::get<bool>(data_); }
std::int64_t Value::as_int64_unchecked() const noexcept { return std::get<std::int64_t>(data_); }
double Value::as_double_unchecked() const noexcept { return std::get<double>(data_); }
std::string_view Value::as_string_unchecked() const noexcept { return std::get<std::string>(data_); }

bool Value::is_blank(BlankPolicy policy) const noexcept {
    if (is_null()) return true;
    const auto* text = std::get_if<std::string>(&data_);
    if (text == nullptr || policy == BlankPolicy::NullOnly) return false;
    if (text->empty()) return true;
    if (policy != BlankPolicy::NullEmptyAndWhitespace) return false;
    for (const unsigned char c : *text) {
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r' && c != '\f' && c != '\v') return false;
    }
    return true;
}

Value Value::blank_to(Value fallback) const { return is_blank() ? std::move(fallback) : *this; }
std::string Value::blank_to_string(const std::string& fallback) const { return is_blank() ? fallback : as_string(); }

} // namespace quickxlsx
