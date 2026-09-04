#pragma once

#include "quickxlsx/config.hpp"

#include <chrono>
#include <concepts>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

namespace quickxlsx {

/** Runtime type stored by Value. */
enum class ValueType : std::uint8_t {
    /** No value. */ Null,
    /** Boolean value. */ Boolean,
    /** Signed 64-bit integer. */ Integer,
    /** IEEE double-precision number. */ Double,
    /** Owned UTF-8 text. */ String,
    /** System-clock time point. */ DateTime
};

/** Owning, type-safe cell value with forgiving conversion helpers. */
class Value {
public:
    /** Date/time representation used by this library. */
    using DateTime = std::chrono::system_clock::time_point;

    /** Constructs Null. */
    Value() noexcept = default;
    /** Constructs Null. */
    Value(std::nullptr_t) noexcept {}
    /** Constructs a Boolean. */
    Value(bool value) noexcept : data_(value) {}

    /** Constructs an Integer, saturating integral types wider than int64_t. */
    template<std::integral T>
        requires (!std::same_as<std::remove_cv_t<T>, bool>)
    Value(T value) noexcept : data_(to_integer(value)) {}

    /** Constructs a Double. */
    template<std::floating_point T>
    Value(T value) noexcept : data_(static_cast<double>(value)) {}

    /** Copies owned String data. */
    Value(const std::string& value) : data_(value) {}
    /** Moves owned String data. */
    Value(std::string&& value) noexcept : data_(std::move(value)) {}
    /** Copies String data from a non-owning view. */
    Value(std::string_view value) : data_(std::string(value)) {}
    /** Copies a C string; a null pointer constructs Null. */
    Value(const char* value) : data_(value == nullptr ? Storage{} : Storage(std::string(value))) {}
    /** Constructs a DateTime. */
    Value(DateTime value) noexcept : data_(value) {}

    /** Returns the active stored type. */
    [[nodiscard]] ValueType type() const noexcept;
    /** Returns whether the active type is Null. */
    [[nodiscard]] bool is_null() const noexcept;
    /** Returns whether the active type is Boolean. */
    [[nodiscard]] bool is_boolean() const noexcept;
    /** Returns whether the active type is Integer. */
    [[nodiscard]] bool is_integer() const noexcept;
    /** Returns whether the active type is Double. */
    [[nodiscard]] bool is_double() const noexcept;
    /** Returns whether the active type is String. */
    [[nodiscard]] bool is_string() const noexcept;
    /** Returns whether the active type is DateTime. */
    [[nodiscard]] bool is_datetime() const noexcept;
    /** Returns whether the active type is Integer or Double. */
    [[nodiscard]] bool is_numeric() const noexcept;

    /** Converts to bool, returning default_value when conversion fails. */
    [[nodiscard]] bool as_bool(bool default_value = false) const noexcept;
    /** Converts to int64_t, returning default_value when conversion fails. */
    [[nodiscard]] std::int64_t as_int64(std::int64_t default_value = 0) const noexcept;
    /** Converts to double, returning default_value when conversion fails. */
    [[nodiscard]] double as_double(double default_value = 0.0) const noexcept;
    /** Converts any type to owned text; Null becomes an empty string. */
    [[nodiscard]] std::string as_string() const;
    /** Converts to text using options for DateTime formatting. */
    [[nodiscard]] std::string as_string(const DateTimeOptions& options) const;
    /** Converts DateTime directly, Integer as Unix seconds, or Double as an Excel serial; other types return epoch. */
    [[nodiscard]] DateTime as_datetime() const noexcept;

    /** Returns the Boolean without checking; calling for another type terminates because this function is noexcept. */
    [[nodiscard]] bool as_bool_unchecked() const noexcept;
    /** Returns the Integer without checking; calling for another type terminates because this function is noexcept. */
    [[nodiscard]] std::int64_t as_int64_unchecked() const noexcept;
    /** Returns the Double without checking; calling for another type terminates because this function is noexcept. */
    [[nodiscard]] double as_double_unchecked() const noexcept;
    /** Returns a view into String storage; valid only while this String Value is alive and unmodified. */
    [[nodiscard]] std::string_view as_string_unchecked() const noexcept;

    /** Tests Null and, according to policy, empty or ASCII-whitespace-only strings. */
    [[nodiscard]] bool is_blank(BlankPolicy policy = BlankPolicy::NullAndEmpty) const noexcept;
    /** Returns default_value for a blank value, otherwise an owning copy of this value. */
    [[nodiscard]] Value blank_to(Value default_value) const;
    /** Returns default_string for a blank value, otherwise converts to text. */
    [[nodiscard]] std::string blank_to_string(const std::string& default_string = {}) const;

    /** Compares both active type and stored value. */
    [[nodiscard]] bool operator==(const Value& other) const noexcept = default;

    /** Creates Null. */
    [[nodiscard]] static Value null() noexcept { return {}; }
    /** Creates an owning String by copying value. */
    [[nodiscard]] static Value string(std::string_view value) { return Value(value); }
    /** Creates an Integer. */
    [[nodiscard]] static Value integer(std::int64_t value) noexcept { return Value(value); }
    /** Creates a Double. */
    [[nodiscard]] static Value floating(double value) noexcept { return Value(value); }
    /** Creates a Boolean. */
    [[nodiscard]] static Value boolean(bool value) noexcept { return Value(value); }
    /** Creates a DateTime. */
    [[nodiscard]] static Value datetime(DateTime value) noexcept { return Value(value); }

private:
    using Storage = std::variant<std::monostate, bool, std::int64_t, double, std::string, DateTime>;

    template<std::integral T>
    static constexpr std::int64_t to_integer(T value) noexcept {
        if constexpr (std::is_signed_v<T>) {
            if constexpr (sizeof(T) > sizeof(std::int64_t)) {
                if (value > static_cast<T>(std::numeric_limits<std::int64_t>::max())) return std::numeric_limits<std::int64_t>::max();
                if (value < static_cast<T>(std::numeric_limits<std::int64_t>::min())) return std::numeric_limits<std::int64_t>::min();
            }
        } else if constexpr (sizeof(T) >= sizeof(std::int64_t)) {
            if (value > static_cast<T>(std::numeric_limits<std::int64_t>::max())) return std::numeric_limits<std::int64_t>::max();
        }
        return static_cast<std::int64_t>(value);
    }

    Storage data_;
};

} // namespace quickxlsx
