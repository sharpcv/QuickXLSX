#include "quickxlsx/reader.hpp"

#include "quickxlsx/errors.hpp"
#include "quickxlsx/worksheet.hpp"
#ifdef QUICKXLSX_ENABLE_XLSX
#include "xlsx_format.hpp"
#endif
#include "utf8_path.hpp"

#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <deque>
#include <exception>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>

namespace quickxlsx::detail {
void stream_csv(const std::filesystem::path&, const CSVConfig&, const std::function<void(Row&&)>&);
}

namespace quickxlsx {
namespace {

enum class Format { CSV, XLSX };

std::string lower_extension(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return extension;
}
Format format_from_path(const std::filesystem::path& path) {
    const auto extension = lower_extension(path);
    if (extension == ".csv") return Format::CSV;
    if (extension == ".xlsx") {
#ifdef QUICKXLSX_ENABLE_XLSX
        return Format::XLSX;
#else
        throw errors::UnsupportedFeature("XLSX support is disabled: " + path.string());
#endif
    }
    throw errors::UnsupportedFeature("unsupported input format for " + path.string() + "; expected .csv or .xlsx");
}
void require_open(bool open) {
    if (!open) throw errors::ReadError("reader is closed");
}
void require_xlsx_enabled(const std::filesystem::path& path) {
#ifndef QUICKXLSX_ENABLE_XLSX
    throw errors::UnsupportedFeature("XLSX support is disabled: " + path.string());
#else
    (void)path;
#endif
}

struct RowChannel {
    std::mutex mutex;
    std::condition_variable ready;
    std::condition_variable consumed;
    std::deque<Row> rows;
    std::exception_ptr error;
    bool finished = false;
    bool cancelled = false;
};
struct Cancelled {};
struct Producer {
    std::shared_ptr<RowChannel> channel;
    std::thread thread;
    ~Producer() {
        {
            std::lock_guard lock(channel->mutex);
            channel->cancelled = true;
        }
        channel->ready.notify_all();
        channel->consumed.notify_all();
        if (thread.joinable()) thread.join();
    }
};

template<typename Read>
Generator<Row> generate_rows(Read read) {
    auto channel = std::make_shared<RowChannel>();
    Producer producer{channel, std::thread([channel, read = std::move(read)]() mutable {
        try {
            read([&](Row row) {
                std::unique_lock lock(channel->mutex);
                channel->consumed.wait(lock, [&] { return channel->rows.empty() || channel->cancelled; });
                if (channel->cancelled) throw Cancelled{};
                channel->rows.push_back(std::move(row));
                lock.unlock();
                channel->ready.notify_one();
            });
        } catch (const Cancelled&) {
        } catch (...) {
            std::lock_guard lock(channel->mutex);
            channel->error = std::current_exception();
        }
        {
            std::lock_guard lock(channel->mutex);
            channel->finished = true;
        }
        channel->ready.notify_all();
    })};
    for (;;) {
        std::unique_lock lock(channel->mutex);
        channel->ready.wait(lock, [&] { return !channel->rows.empty() || channel->finished; });
        if (!channel->rows.empty()) {
            Row row = std::move(channel->rows.front());
            channel->rows.pop_front();
            channel->consumed.notify_one();
            lock.unlock();
            co_yield std::move(row);
            continue;
        }
        if (channel->error) std::rethrow_exception(channel->error);
        co_return;
    }
}

} // namespace

struct Reader::State {
    std::filesystem::path path;
    Format format;
    CSVConfig csv;
    XLSXConfig xlsx;
    DateTimeOptions datetime;
    bool open = true;
};

Reader::Reader(const std::string& path) : state_(std::make_unique<State>()) {
    state_->path = detail::path_from_utf8(path);
    state_->format = format_from_path(state_->path);
    state_->open = true;
}
Reader::Reader(const std::string& path, const CSVConfig& config) : state_(std::make_unique<State>()) {
    state_->path = detail::path_from_utf8(path);
    state_->format = Format::CSV;
    state_->csv = config;
    state_->open = true;
}
Reader::Reader(const std::string& path, const XLSXConfig& config) : state_(std::make_unique<State>()) {
    state_->path = detail::path_from_utf8(path);
    state_->format = Format::XLSX;
    state_->xlsx = config;
    state_->open = true;
    require_xlsx_enabled(state_->path);
}
Reader::~Reader() = default;
Reader::Reader(Reader&&) noexcept = default;
Reader& Reader::operator=(Reader&&) noexcept = default;

std::vector<std::string> Reader::sheet_names() const {
    require_open(is_open());
    if (state_->format == Format::CSV) {
        auto name = detail::utf8_bytes(state_->path.stem());
        if (!Worksheet::valid_name(name)) name = "Sheet1";
        return {std::move(name)};
    }
#ifdef QUICKXLSX_ENABLE_XLSX
    return detail::xlsx_sheet_names(state_->path);
#else
    throw errors::UnsupportedFeature("XLSX support is disabled: " + state_->path.string());
#endif
}
std::size_t Reader::sheet_count() const { return sheet_names().size(); }

void Reader::read_rows(const RowCallback& callback) {
    require_open(is_open());
    if (!callback) throw errors::ReadError("row callback is empty");
    if (state_->format == Format::CSV) {
        detail::stream_csv(state_->path, state_->csv, [&](Row&& row) { callback(row); });
        return;
    }
#ifdef QUICKXLSX_ENABLE_XLSX
    const auto names = detail::xlsx_sheet_names(state_->path);
    if (names.empty()) throw errors::WorksheetNotFound("workbook contains no worksheets: " + state_->path.string());
    detail::stream_xlsx(state_->path, names.front(), state_->xlsx, callback);
#else
    throw errors::UnsupportedFeature("XLSX support is disabled: " + state_->path.string());
#endif
}
void Reader::read_rows(const std::string& sheet_name, const RowCallback& callback) {
    require_open(is_open());
    if (!callback) throw errors::ReadError("row callback is empty");
    if (state_->format == Format::CSV) {
        const auto names = sheet_names();
        if (sheet_name != names.front()) throw errors::WorksheetNotFound("CSV worksheet not found: " + sheet_name);
        detail::stream_csv(state_->path, state_->csv, [&](Row&& row) { callback(row); });
        return;
    }
#ifdef QUICKXLSX_ENABLE_XLSX
    detail::stream_xlsx(state_->path, sheet_name, state_->xlsx, callback);
#else
    throw errors::UnsupportedFeature("XLSX support is disabled: " + state_->path.string());
#endif
}
// Generators capture path/config by value rather than State. They therefore remain valid after
// Reader moves or destruction while still owning only the currently yielded Row.
Generator<Row> Reader::rows() {
    require_open(is_open());
    const auto path = state_->path;
    if (state_->format == Format::CSV) {
        const auto config = state_->csv;
        return generate_rows([path, config](auto callback) {
            detail::stream_csv(path, config, [&](Row&& row) { callback(std::move(row)); });
        });
    }
#ifdef QUICKXLSX_ENABLE_XLSX
    const auto config = state_->xlsx;
    const auto names = detail::xlsx_sheet_names(path);
    if (names.empty()) throw errors::WorksheetNotFound("workbook contains no worksheets: " + path.string());
    const auto name = names.front();
    return generate_rows([path, config, name](auto callback) {
        detail::stream_xlsx(path, name, config, [&](const Row& row) { callback(Row(row)); });
    });
#else
    throw errors::UnsupportedFeature("XLSX support is disabled: " + path.string());
#endif
}
Generator<Row> Reader::rows(const std::string& sheet_name) {
    require_open(is_open());
    const auto path = state_->path;
    if (state_->format == Format::CSV) {
        if (sheet_name != sheet_names().front()) throw errors::WorksheetNotFound("CSV worksheet not found: " + sheet_name);
        const auto config = state_->csv;
        return generate_rows([path, config](auto callback) {
            detail::stream_csv(path, config, [&](Row&& row) { callback(std::move(row)); });
        });
    }
#ifdef QUICKXLSX_ENABLE_XLSX
    const auto config = state_->xlsx;
    return generate_rows([path, config, sheet_name](auto callback) {
        detail::stream_xlsx(path, sheet_name, config, [&](const Row& row) { callback(Row(row)); });
    });
#else
    throw errors::UnsupportedFeature("XLSX support is disabled: " + path.string());
#endif
}

void Reader::set_delimiter(char value) { require_open(is_open()); if (state_->format != Format::CSV) throw errors::UnsupportedFeature("delimiter is only valid for CSV input"); state_->csv.delimiter = value; }
void Reader::set_skip_empty_rows(bool value) { require_open(is_open()); state_->csv.skip_empty_rows = value; state_->xlsx.skip_empty_rows = value; }
void Reader::set_max_rows(std::size_t value) { require_open(is_open()); state_->csv.max_rows = value; state_->xlsx.max_rows = value; }
void Reader::set_blank_policy(BlankPolicy value) { require_open(is_open()); state_->csv.blank_policy = value; state_->xlsx.blank_policy = value; }
void Reader::set_datetime_options(const DateTimeOptions& value) { require_open(is_open()); state_->datetime = value; }
bool Reader::is_open() const noexcept { return state_ && state_->open; }
void Reader::close() noexcept { if (state_) state_->open = false; }

} // namespace quickxlsx
