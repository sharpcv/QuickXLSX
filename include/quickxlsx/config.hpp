#pragma once

#include <cstddef>
#include <string>

namespace quickxlsx {

/** Controls which textual values are treated as blank. */
enum class BlankPolicy {
    /** Only Null is blank. */ NullOnly,
    /** Null and an empty string are blank. */ NullAndEmpty,
    /** Null, empty strings, and ASCII-whitespace-only strings are blank. */ NullEmptyAndWhitespace,
};

/** Options used while reading or writing CSV data. */
struct CSVConfig {
    /** Field separator; must not be NUL, quote, CR, or LF. */
    char delimiter = ',';
    /** Whether to consume the first record without returning it. */
    bool has_header = false;
    /** Whether records blank under blank_policy are omitted. */
    bool skip_empty_rows = true;
    /** Maximum delivered rows, or zero for no limit. */
    std::size_t max_rows = 0;
    /** Policy used to determine whether an input row is empty. */
    BlankPolicy blank_policy = BlankPolicy::NullAndEmpty;
};

/** Options used while reading or writing XLSX data. */
struct XLSXConfig {
    /** Whether rows blank under blank_policy are omitted while reading. */
    bool skip_empty_rows = true;
    /** Maximum delivered rows, or zero for no limit. */
    std::size_t max_rows = 0;
    /** Policy used to determine whether an input row is empty. */
    BlankPolicy blank_policy = BlankPolicy::NullAndEmpty;
};

/** Controls conversion of DateTime values to text. */
struct DateTimeOptions {
    /** strftime-compatible format, optionally wrapped as a fmt-style "{:...}" field. */
    std::string format = "{:%Y-%m-%d %H:%M:%S}";
    /** Use local time when true and UTC when false. */
    bool local_time = true;
};

} // namespace quickxlsx
