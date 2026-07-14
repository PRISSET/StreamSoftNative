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

inline std::filesystem::path resolve_resource_dir(const std::string& packaged_relative, const std::string& dev_absolute) {
    auto packaged = exe_dir() / packaged_relative;
    if (std::filesystem::exists(packaged)) return packaged;
    return std::filesystem::path(dev_absolute);
}

inline std::string resolve_resource_file(const std::string& packaged_relative, const std::string& dev_absolute) {
    auto packaged = exe_dir() / packaged_relative;
    if (std::filesystem::exists(packaged)) return packaged.string();
    return dev_absolute;
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
