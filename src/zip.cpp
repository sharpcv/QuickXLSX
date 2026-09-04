#include "xlsx_format.hpp"

#include "quickxlsx/errors.hpp"

#include <minizip/mz.h>
#include <minizip/mz_strm.h>
#include <minizip/mz_zip.h>
#include <minizip/mz_zip_rw.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <system_error>

#include "utf8_path.hpp"

namespace quickxlsx::detail::xlsx_internal {
namespace {

// minizip 在 Windows 上按 UTF-8 解释文件名，因此把宽路径转成 UTF-8 字节交给它。
std::string native_path(const std::filesystem::path& path) {
    return detail::utf8_bytes(path);
}

[[noreturn]] void throw_zip(const std::filesystem::path& path, std::string_view action,
                            int32_t code) {
    const std::string message = "XLSX ZIP " + std::string(action) + " failed for '" +
                                path.string() + "' (minizip error " + std::to_string(code) + ")";
    if (code == MZ_CRYPT_ERROR || code == MZ_PASSWORD_ERROR)
        throw errors::UnsupportedFeature(message + ": encrypted XLSX packages are not supported");
    if (code == MZ_OPEN_ERROR && !std::filesystem::exists(path)) throw errors::FileNotFound(message);
    if (code == MZ_OPEN_ERROR) throw errors::FilePermissionDenied(message);
    throw errors::ZipError(message);
}

std::string gib_suffix(std::uint64_t bytes) {
    return std::to_string(bytes >> 30) + " GiB";
}

} // namespace

ZipReader::ZipReader(const std::filesystem::path& path) : path_(path) {
    handle_ = mz_zip_reader_create();
    if (handle_ == nullptr) throw errors::ZipError("failed to allocate XLSX ZIP reader");
    const std::string filename = native_path(path_);
    const int32_t result = mz_zip_reader_open_file(handle_, filename.c_str());
    if (result != MZ_OK) {
        mz_zip_reader_delete(&handle_);
        throw_zip(path_, "open", result);
    }
}

ZipReader::~ZipReader() {
    if (handle_ != nullptr) {
        mz_zip_reader_close(handle_);
        mz_zip_reader_delete(&handle_);
    }
}

bool ZipReader::contains(std::string_view name) {
    const std::string entry(name);
    const int32_t result = mz_zip_reader_locate_entry(handle_, entry.c_str(), 0);
    if (result == MZ_OK) return true;
    if (result == MZ_END_OF_LIST) return false;
    throw_zip(path_, "locate entry '" + entry + "'", result);
}

// Inflate in bounded chunks. The callback may stop early; limits are checked against both
// declared metadata and actual output so malformed ZIPs cannot bypass the safety caps.
void ZipReader::read_chunks(std::string_view name,
                            const std::function<bool(std::string_view)>& callback) {
    if (!callback) throw errors::ReadError("XLSX ZIP chunk callback is empty");
    const std::string entry(name);
    int32_t result = mz_zip_reader_locate_entry(handle_, entry.c_str(), 0);
    if (result == MZ_END_OF_LIST)
        throw errors::MissingRequiredElement("XLSX package is missing required part '" + entry + "'");
    if (result != MZ_OK) throw_zip(path_, "locate entry '" + entry + "'", result);

    mz_zip_file* info = nullptr;
    result = mz_zip_reader_entry_get_info(handle_, &info);
    if (result != MZ_OK || info == nullptr) throw_zip(path_, "inspect entry '" + entry + "'", result);
    if ((info->flag & MZ_ZIP_FLAG_ENCRYPTED) != 0 || info->aes_version != 0)
        throw errors::UnsupportedFeature("encrypted XLSX part is not supported: " + entry);
    if (info->uncompressed_size < 0)
        throw errors::ZipError("invalid uncompressed size for XLSX part '" + entry + "'");
    const auto expected_size = static_cast<std::uint64_t>(info->uncompressed_size);
    if (expected_size > max_xlsx_part_uncompressed_bytes)
        throw errors::ZipError("XLSX part '" + entry + "' exceeds the " + gib_suffix(max_xlsx_part_uncompressed_bytes) + " uncompressed safety limit");
    if (expected_size > max_xlsx_package_uncompressed_bytes - total_uncompressed_bytes_)
        throw errors::ZipError("XLSX package '" + path_.string() + "' exceeds the " + gib_suffix(max_xlsx_package_uncompressed_bytes) + " aggregate uncompressed safety limit while reading '" + entry + "'");

    result = mz_zip_reader_entry_open(handle_);
    if (result != MZ_OK) throw_zip(path_, "open entry '" + entry + "'", result);
    struct EntryCloser {
        void* handle;
        ~EntryCloser() { if (handle != nullptr) mz_zip_reader_entry_close(handle); }
    } closer{handle_};

    std::uint64_t inflated = 0;
    bool consume = true;
    std::array<char, 64 * 1024> buffer{};
    while (consume) {
        result = mz_zip_reader_entry_read(handle_, buffer.data(), static_cast<int32_t>(buffer.size()));
        if (result < 0) throw_zip(path_, "read entry '" + entry + "'", result);
        if (result == 0) break;
        const auto count = static_cast<std::uint64_t>(result);
        if (count > max_xlsx_part_uncompressed_bytes - inflated)
            throw errors::ZipError("XLSX part '" + entry + "' exceeded the " + gib_suffix(max_xlsx_part_uncompressed_bytes) + " uncompressed safety limit while inflating");
        if (count > max_xlsx_package_uncompressed_bytes - total_uncompressed_bytes_)
            throw errors::ZipError("XLSX package '" + path_.string() + "' exceeded the " + gib_suffix(max_xlsx_package_uncompressed_bytes) + " aggregate uncompressed safety limit while inflating '" + entry + "'");
        inflated += count;
        total_uncompressed_bytes_ += count;
        consume = callback(std::string_view(buffer.data(), static_cast<std::size_t>(result)));
    }
    result = mz_zip_reader_entry_close(handle_);
    closer.handle = nullptr;
    if (result != MZ_OK) throw_zip(path_, "verify entry '" + entry + "'", result);
    if (consume && inflated != expected_size)
        throw errors::FileCorrupted("truncated XLSX part '" + entry + "'");
}

std::string ZipReader::read(std::string_view name) {
    std::string contents;
    read_chunks(name, [&contents](std::string_view chunk) {
        contents.append(chunk);
        return true;
    });
    return contents;
}

ZipWriter::ZipWriter(const std::filesystem::path& path, int compression_level)
    : path_(path), compression_level_(std::clamp(compression_level, 0, 9)) {
    handle_ = mz_zip_writer_create();
    if (handle_ == nullptr) throw errors::ZipError("failed to allocate XLSX ZIP writer");
    mz_zip_writer_set_compress_method(handle_, compression_level_ == 0 ?
        MZ_COMPRESS_METHOD_STORE : MZ_COMPRESS_METHOD_DEFLATE);
    mz_zip_writer_set_compress_level(handle_, static_cast<int16_t>(compression_level_));
    const std::string filename = native_path(path_);
    const int32_t result = mz_zip_writer_open_file(handle_, filename.c_str(), 0, 0);
    if (result != MZ_OK) {
        mz_zip_writer_delete(&handle_);
        throw_zip(path_, "create", result);
    }
}

ZipWriter::~ZipWriter() {
    if (handle_ != nullptr) {
        if (entry_open_) mz_zip_writer_entry_close(handle_);
        mz_zip_writer_close(handle_);
        mz_zip_writer_delete(&handle_);
    }
}

// ZIP entries are opened independently so worksheet XML can be produced progressively.
// current_entry_bytes_ and total_uncompressed_bytes_ enforce the same limits on writers.
void ZipWriter::begin_entry(std::string_view name) {
    if (handle_ == nullptr) throw errors::WriteError("cannot add a part to a closed XLSX package");
    if (entry_open_) throw errors::WriteError("cannot open XLSX part while '" + current_entry_ + "' is open");
    if (name.empty() || name.size() > std::numeric_limits<uint16_t>::max())
        throw errors::WriteError("invalid XLSX ZIP part name");
    current_entry_.assign(name);
    mz_zip_file info{};
    info.filename = current_entry_.c_str();
    info.filename_size = static_cast<uint16_t>(current_entry_.size());
    info.flag = MZ_ZIP_FLAG_UTF8;
    info.compression_method = compression_level_ == 0 ? MZ_COMPRESS_METHOD_STORE : MZ_COMPRESS_METHOD_DEFLATE;
    info.uncompressed_size = 0;
    info.zip64 = MZ_ZIP64_AUTO;
    const int32_t result = mz_zip_writer_entry_open(handle_, &info);
    if (result != MZ_OK) throw_zip(path_, "open output entry '" + current_entry_ + "'", result);
    current_entry_bytes_ = 0;
    entry_open_ = true;
}

void ZipWriter::write_chunk(std::string_view contents) {
    if (!entry_open_) throw errors::WriteError("cannot write XLSX data without an open part");
    if (contents.size() > max_xlsx_part_uncompressed_bytes - current_entry_bytes_)
        throw errors::WriteError("XLSX part '" + current_entry_ + "' exceeds the " + gib_suffix(max_xlsx_part_uncompressed_bytes) + " uncompressed safety limit");
    if (contents.size() > max_xlsx_package_uncompressed_bytes - total_uncompressed_bytes_)
        throw errors::WriteError("XLSX package exceeds the " + gib_suffix(max_xlsx_package_uncompressed_bytes) + " aggregate uncompressed safety limit");
    while (!contents.empty()) {
        const std::size_t count = std::min<std::size_t>(contents.size(), 1U << 20);
        const int32_t result = mz_zip_writer_entry_write(handle_, contents.data(), static_cast<int32_t>(count));
        if (result < 0) throw_zip(path_, "write output entry '" + current_entry_ + "'", result);
        if (result == 0) throw errors::WriteError("short write for XLSX part '" + current_entry_ + "'");
        contents.remove_prefix(static_cast<std::size_t>(result));
        current_entry_bytes_ += static_cast<std::uint64_t>(result);
        total_uncompressed_bytes_ += static_cast<std::uint64_t>(result);
    }
}

void ZipWriter::end_entry() {
    if (!entry_open_) throw errors::WriteError("cannot close XLSX part: no part is open");
    const std::string entry = current_entry_;
    const int32_t result = mz_zip_writer_entry_close(handle_);
    entry_open_ = false;
    current_entry_.clear();
    if (result != MZ_OK) throw_zip(path_, "close output entry '" + entry + "'", result);
}

void ZipWriter::add(std::string_view name, std::string_view contents) {
    begin_entry(name);
    try {
        write_chunk(contents);
        end_entry();
    } catch (...) {
        if (entry_open_) {
            mz_zip_writer_entry_close(handle_);
            entry_open_ = false;
            current_entry_.clear();
        }
        throw;
    }
}

void ZipWriter::close() {
    if (handle_ == nullptr) return;
    if (entry_open_) throw errors::WriteError("cannot finalize XLSX package while part '" + current_entry_ + "' is open");
    const int32_t result = mz_zip_writer_close(handle_);
    mz_zip_writer_delete(&handle_);
    if (result != MZ_OK) throw_zip(path_, "finalize", result);
}

} // namespace quickxlsx::detail::xlsx_internal
