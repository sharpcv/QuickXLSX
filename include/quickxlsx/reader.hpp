#pragma once
#include "quickxlsx/config.hpp"
#include "quickxlsx/generator.hpp"
#include "quickxlsx/row.hpp"
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>
namespace quickxlsx {
/** Move-only streaming reader. Row callback references are valid only during the callback. */
class Reader {
public:
    /** Callback invoked synchronously for each delivered row. */ using RowCallback = std::function<void(const Row&)>;
    /** Selects CSV/XLSX by extension; format and file errors can be deferred until reading. */ explicit Reader(const std::string& path);
    /** Forces CSV input with config. */ Reader(const std::string& path, const CSVConfig& config);
    /** Forces XLSX input; throws UnsupportedFeature when XLSX is disabled. */ Reader(const std::string& path, const XLSXConfig& config);
    /** Releases reader state; outstanding generators own independent parsing state. */ ~Reader();
    /** Readers cannot be copied. */ Reader(const Reader&) = delete;
    /** Readers cannot be copied. */ Reader& operator=(const Reader&) = delete;
    /** Transfers state; the source becomes closed. */ Reader(Reader&&) noexcept;
    /** Closes current state and transfers state; the source becomes closed. */ Reader& operator=(Reader&&) noexcept;
    /** Returns input sheet names; closed readers throw errors::ReadError. */ [[nodiscard]] std::vector<std::string> sheet_names() const;
    /** Returns input sheet count; CSV always reports one. */ [[nodiscard]] std::size_t sheet_count() const;
    /** Streams the first sheet; exceptions from parsing or callback propagate. */ void read_rows(const RowCallback& callback);
    /** Streams a named sheet; throws errors::WorksheetNotFound when absent. */ void read_rows(const std::string& sheet_name, const RowCallback& callback);
    /** Returns a single-pass lazy row generator independent of later Reader lifetime. */ [[nodiscard]] Generator<Row> rows();
    /** Returns a single-pass lazy generator for a named sheet. */ [[nodiscard]] Generator<Row> rows(const std::string& sheet_name);
    /** Sets CSV delimiter before reading; throws UnsupportedFeature for XLSX or ReadError when closed. */ void set_delimiter(char delimiter);
    /** Controls omission of blank rows for subsequent operations. */ void set_skip_empty_rows(bool skip);
    /** Sets maximum delivered rows; zero means unlimited. */ void set_max_rows(std::size_t max_rows);
    /** Sets blank classification for subsequent operations. */ void set_blank_policy(BlankPolicy policy);
    /** Sets DateTime conversion options for subsequent operations. */ void set_datetime_options(const DateTimeOptions& options);
    /** Returns whether usable state remains. */ [[nodiscard]] bool is_open() const noexcept;
    /** Idempotently prevents further operations; it does not invalidate already-created generators. */ void close() noexcept;
private: struct State; std::unique_ptr<State> state_;
};
} // namespace quickxlsx
