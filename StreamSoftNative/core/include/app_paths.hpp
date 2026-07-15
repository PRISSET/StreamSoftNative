#pragma once

#include <filesystem>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <shlobj.h>
#include <windows.h>

namespace streamsoft {

inline std::filesystem::path exe_dir() {
    wchar_t buf[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return std::filesystem::current_path();
    return std::filesystem::path(buf).parent_path();
}

// std::filesystem::path::string() converts the path's native (UTF-16 on
// Windows) form to a narrow string using the ACTIVE ANSI CODE PAGE — and
// throws std::system_error ("No mapping for the Unicode character exists
// in the target multi-byte code page") if any character in the path isn't
// representable in that code page. That's not a rare edge case here: any
// Windows account whose profile folder name isn't representable in the
// system's ANSI code page (e.g. a non-Cyrillic-codepage machine hitting a
// Cyrillic username, or vice versa) makes EVERY call that resolves a
// resource path throw before it ever touches the network — which looked
// exactly like a networking failure until the exception message was
// traced back here. CP_UTF8 conversion never fails for valid UTF-16 input,
// so use that instead.
inline std::string wide_to_utf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), needed, nullptr, nullptr);
    return out;
}

inline std::string path_to_utf8(const std::filesystem::path& p) { return wide_to_utf8(p.wstring()); }

inline std::filesystem::path resolve_resource_dir(const std::string& packaged_relative, const std::string& dev_absolute) {
    auto packaged = exe_dir() / packaged_relative;
    if (std::filesystem::exists(packaged)) return packaged;
    return std::filesystem::path(dev_absolute);
}

inline std::string resolve_resource_file(const std::string& packaged_relative, const std::string& dev_absolute) {
    auto packaged = exe_dir() / packaged_relative;
    if (std::filesystem::exists(packaged)) return path_to_utf8(packaged);
    return dev_absolute;
}

// Same resolution as resolve_resource_file, but keeps the result as a
// filesystem::path instead of collapsing it to a narrow string — for
// callers that can open the file directly (ifstream/fopen-with-path
// accept native wide paths on Windows) instead of handing a path string to
// a narrow-string-only C API, sidestepping the encoding problem entirely
// rather than just making the conversion itself not throw.
inline std::filesystem::path resolve_resource_path(const std::string& packaged_relative, const std::string& dev_absolute) {
    auto packaged = exe_dir() / packaged_relative;
    if (std::filesystem::exists(packaged)) return packaged;
    return std::filesystem::path(dev_absolute);
}

inline void ensure_writable_config_cwd() {
    if (std::filesystem::exists("connections.json")) return;

    PWSTR appdata = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appdata))) return;
    std::filesystem::path dir = std::filesystem::path(appdata) / "StreamSoft";
    CoTaskMemFree(appdata);

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) return;
    SetCurrentDirectoryW(dir.c_str());
}

}
