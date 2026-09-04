#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace quickxlsx::detail {

// 公共路径入口约定：std::string 参数一律按 UTF-8 解释（源码以 /utf-8 编译）。
// Windows 上手工 UTF-8 -> UTF-16 再构 path，避免 fs::path(窄串) 按 ACP 解析中文导致乱码/抛异常。
inline std::filesystem::path path_from_utf8(std::string_view utf8) {
#if defined(_WIN32)
    if (utf8.empty()) return {};
    const int length = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
        static_cast<int>(utf8.size()), nullptr, 0);
    if (length > 0) {
        std::wstring wide(static_cast<std::size_t>(length), L'\0');
        ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
            wide.data(), length);
        return std::filesystem::path(wide);
    }
    // 非法 UTF-8（历史调用方按 ACP 传参）回退到系统代码页解释，保持兼容
#endif
    return std::filesystem::path(std::string(utf8));
}

// fs::path -> UTF-8 窄字符串（minizip 在 Windows 上按 UTF-8 打开文件）
inline std::string utf8_bytes(const std::filesystem::path& path) {
#if defined(_WIN32)
    const auto bytes = path.u8string();
    return bytes.empty() ? std::string{}
        : std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
#else
    return path.string();
#endif
}

} // namespace quickxlsx::detail
