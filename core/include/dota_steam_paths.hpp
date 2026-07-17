#pragma once

#include "app_paths.hpp"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace streamsoft::dota {

// Mirrors cs2::find_steam_path()/find_cs2_cfg_dir() in steam_paths.hpp.
inline std::optional<std::filesystem::path> find_steam_path() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    wchar_t buf[MAX_PATH];
    DWORD size = sizeof(buf);
    DWORD type = 0;
    LSTATUS st = RegQueryValueExW(key, L"SteamPath", nullptr, &type, reinterpret_cast<BYTE*>(buf), &size);
    RegCloseKey(key);
    if (st != ERROR_SUCCESS || type != REG_SZ) return std::nullopt;
    return std::filesystem::path(buf);
}

// Dota 2 is still installed under the "dota 2 beta" folder name on disk —
// Valve never renamed the Steam app/folder despite the game exiting beta
// status years ago. Same multi-library scan as find_cs2_cfg_dir().
inline std::optional<std::filesystem::path> find_dota_cfg_dir() {
    auto steam = find_steam_path();
    if (!steam) return std::nullopt;

    std::vector<std::filesystem::path> libraries{*steam};

    std::ifstream f(*steam / "steamapps" / "libraryfolders.vdf", std::ios::binary);
    if (f) {
        std::ostringstream ss;
        ss << f.rdbuf();
        std::string content = ss.str();
        std::regex path_re("\"path\"\\s*\"([^\"]+)\"");
        for (auto it = std::sregex_iterator(content.begin(), content.end(), path_re); it != std::sregex_iterator();
             ++it) {
            std::string raw = (*it)[1].str();
            std::string unescaped;
            for (size_t i = 0; i < raw.size(); ++i) {
                if (raw[i] == '\\' && i + 1 < raw.size() && raw[i + 1] == '\\') {
                    unescaped += '\\';
                    ++i;
                } else {
                    unescaped += raw[i];
                }
            }
            libraries.emplace_back(unescaped);
        }
    }

    for (const auto& lib : libraries) {
        auto candidate = lib / "steamapps" / "common" / "dota 2 beta" / "game" / "dota" / "cfg";
        if (std::filesystem::exists(candidate)) return candidate;
    }
    return std::nullopt;
}

struct GsiInstallResult {
    bool ok = false;
    std::string error;
    std::string path;
};

// Same pattern as cs2::install_gsi_cfg(): the game itself pushes live match
// state to our local /gsi-dota endpoint once this .cfg exists — no Steam
// account privacy setting involved, unlike the OpenDota API (which reads
// from a setting off by default and can't be worked around by any third
// party, OpenDota included).
inline GsiInstallResult install_gsi_cfg(const std::string& token, int overlay_port) {
    GsiInstallResult r;
    auto cfg_dir = find_dota_cfg_dir();
    if (!cfg_dir) {
        r.error = "Dota 2 не найдена — проверь, что Steam установлен и игра скачана";
        return r;
    }

    std::filesystem::path cfg_path = *cfg_dir / "gamestate_integration_streamsoft_dota.cfg";
    std::ofstream out(cfg_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        r.error = "Нет доступа на запись в папку cfg (" + streamsoft::path_to_utf8(*cfg_dir) + ")";
        return r;
    }

    out << "\"StreamSoft Dota GSI\"\n"
        << "{\n"
        << " \"uri\" \"http://127.0.0.1:" << overlay_port << "/gsi-dota\"\n"
        << " \"timeout\" \"5.0\"\n"
        << " \"buffer\" \"0.1\"\n"
        << " \"throttle\" \"0.1\"\n"
        << " \"heartbeat\" \"30.0\"\n"
        << " \"auth\"\n"
        << " {\n"
        << "  \"token\" \"" << token << "\"\n"
        << " }\n"
        << " \"data\"\n"
        << " {\n"
        << "  \"provider\" \"1\"\n"
        << "  \"map\"      \"1\"\n"
        << "  \"player\"   \"1\"\n"
        << "  \"hero\"     \"1\"\n"
        << " }\n"
        << "}\n";

    r.ok = true;
    r.path = streamsoft::path_to_utf8(cfg_path);
    return r;
}

}
