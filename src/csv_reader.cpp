#include "quickxlsx/config.hpp"
#include "quickxlsx/errors.hpp"
#include "quickxlsx/row.hpp"

#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace quickxlsx::detail {
namespace {

bool blank_row(const std::vector<std::string>& fields, const std::vector<bool>& quoted,
               BlankPolicy policy) {
    for (std::size_t index = 0; index < fields.size(); ++index) {
        const Value value = fields[index].empty() && !quoted[index] ? Value::null() : Value(fields[index]);
        if (!value.is_blank(policy)) return false;
    }
    return true;
}

// A byte-at-a-time state machine keeps quoted records correct across the fixed-size I/O chunks.
// Only the current field and record are retained; completed rows are handed off immediately.
class CSVParser {
public:
    CSVParser(std::filesystem::path path, const CSVConfig& config,
              const std::function<void(Row&&)>& callback)
        : path_(std::move(path)), config_(config), callback_(callback) {
        if (config_.delimiter == '\r' || config_.delimiter == '\n' || config_.delimiter == '"' ||
            config_.delimiter == '\0') {
            throw errors::CSVParseError("invalid CSV delimiter");
        }
    }

    void parse() {
        std::ifstream input(path_, std::ios::binary);
        if (!input) {
            if (!std::filesystem::exists(path_)) throw errors::FileNotFound("CSV file not found: " + path_.string());
            throw errors::FilePermissionDenied("cannot open CSV file: " + path_.string());
        }

        char prefix[3]{};
        input.read(prefix, 3);
        const auto count = input.gcount();
        std::streamsize start = 0;
        if (count == 3 && static_cast<unsigned char>(prefix[0]) == 0xEF &&
            static_cast<unsigned char>(prefix[1]) == 0xBB && static_cast<unsigned char>(prefix[2]) == 0xBF) {
            start = 3;
        }
        for (std::streamsize i = start; i < count && !stopped_; ++i) consume(prefix[i]);
        char buffer[64 * 1024];
        while (!stopped_ && input.read(buffer, sizeof buffer)) {
            for (char value : buffer) { consume(value); if (stopped_) break; }
        }
        if (!stopped_) {
            const auto remaining = input.gcount();
            for (std::streamsize i = 0; i < remaining && !stopped_; ++i) consume(buffer[i]);
        }
        if (input.bad()) throw errors::ReadError("error reading CSV file: " + path_.string());
        finish();
    }

private:
    // Delimiters and line endings are interpreted only outside quoted fields.  quote_pending_
    // defers the decision until the next byte so doubled quotes and closing quotes are distinct.
    void consume(char value) {
        saw_input_ = true;
        if (in_quotes_) {
            if (value == '"') {
                if (quote_pending_) { field_.push_back('"'); quote_pending_ = false; }
                else quote_pending_ = true;
            } else {
                if (quote_pending_) {
                    in_quotes_ = false;
                    quote_pending_ = false;
                    after_quote_ = true;
                    consume(value);
                } else field_.push_back(value);
            }
            return;
        }
        if (skip_lf_) {
            skip_lf_ = false;
            if (value == '\n') return;
        }
        if (after_quote_) {
            if (value == config_.delimiter) { push_field(); after_quote_ = false; return; }
            if (value == '\r' || value == '\n') {
                push_field(); after_quote_ = false; end_record(); skip_lf_ = value == '\r'; return;
            }
            fail("unexpected byte after closing quote");
        }
        if (value == config_.delimiter) { push_field(); return; }
        if (value == '\r' || value == '\n') {
            push_field(); end_record(); skip_lf_ = value == '\r'; return;
        }
        if (value == '"') {
            if (!field_.empty()) fail("quote inside unquoted field");
            in_quotes_ = true;
            field_quoted_ = true;
            return;
        }
        field_.push_back(value);
    }

    [[noreturn]] void fail(std::string_view reason) const {
        throw errors::CSVParseError("CSV parse error in " + path_.string() + " at record " +
                                    std::to_string(record_index_ + 1) + ": " + std::string(reason));
    }
    void push_field() { fields_.push_back(std::move(field_)); quoted_.push_back(field_quoted_); field_.clear(); field_quoted_ = false; }
    void end_record() {
        const auto index = record_index_++;
        if (config_.has_header && !header_seen_) { header_seen_ = true; fields_.clear(); quoted_.clear(); return; }
        if (config_.skip_empty_rows && blank_row(fields_, quoted_, config_.blank_policy)) { fields_.clear(); quoted_.clear(); return; }
        Row row(index);
        for (std::size_t column = 0; column < fields_.size(); ++column) {
            if (fields_[column].empty() && !quoted_[column]) row.set(column, Value::null());
            else row.set(column, Value(std::move(fields_[column])));
        }
        fields_.clear();
        quoted_.clear();
        callback_(std::move(row));
        if (config_.max_rows != 0 && ++delivered_ >= config_.max_rows) stopped_ = true;
    }
    void finish() {
        if (stopped_ || !saw_input_) return;
        if (in_quotes_ && !quote_pending_) fail("unterminated quoted field");
        if (in_quotes_) { in_quotes_ = false; after_quote_ = true; }
        if (!fields_.empty() || !field_.empty() || after_quote_) { push_field(); end_record(); }
    }

    std::filesystem::path path_;
    const CSVConfig& config_;
    const std::function<void(Row&&)>& callback_;
    std::vector<std::string> fields_;
    std::vector<bool> quoted_;
    std::string field_;
    std::size_t record_index_ = 0;
    std::size_t delivered_ = 0;
    bool in_quotes_ = false;
    bool quote_pending_ = false;
    bool field_quoted_ = false;
    bool after_quote_ = false;
    bool skip_lf_ = false;
    bool saw_input_ = false;
    bool header_seen_ = false;
    bool stopped_ = false;
};

} // namespace

void stream_csv(const std::filesystem::path& path, const CSVConfig& config,
                const std::function<void(Row&&)>& callback) {
    CSVParser(path, config, callback).parse();
}

} // namespace quickxlsx::detail
