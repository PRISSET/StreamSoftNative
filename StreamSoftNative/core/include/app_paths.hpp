#pragma once

// Packaged installs ship web/, certs/, adapters/tts, adapters/rvc as
// siblings of the exe (see installer/streamsoft.iss) — every STREAMSOFT_*_DIR
// / STREAMSOFT_CACERT_PATH macro (core/CMakeLists.txt, gui/CMakeLists.txt)
// points straight into the dev source tree at compile time, which only ever
// worked because nobody had built a real installer yet (see CLAUDE.md §8's
// "Дев-тайм only" note on STREAMSOFT_WEB_DIR — this is exactly the follow-up
// it flagged). This resolves each resource against the exe's own directory
// first, falling back to the compile-time dev path when the packaged copy
// isn't there, so a dev build run straight from build/gui/Release still
// works with zero copy step.

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

// connections.json/runtime_settings.json/twitch_token.json/chat_commands.json
// are all plain cwd-relative paths (see connections_config.hpp etc.) — fine
// for a dev checkout, broken for a real install: Program Files isn't
// writable without elevation, and a Start Menu/autostart launch's cwd isn't
// reliably the install directory anyway. Call once at startup, before any
// config loads. Only switches cwd to the per-user AppData folder when
// connections.json isn't already sitting in the current directory, so an
// existing dev setup (or an already-configured install someone's used
// before this existed) never gets silently orphaned from its saved config.
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

}  // namespace streamsoft
