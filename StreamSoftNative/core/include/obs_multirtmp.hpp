#pragma once

#include "app_paths.hpp"
#include "connections_config.hpp"
#include "module_installer.hpp"
#include "obs_scene_file.hpp"
#include "twitch_auth.hpp"

#include <crow/json.h>
#include <crow/logging.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// Auto-installs and configures sorayuki/obs-multi-rtmp — a free, actively
// maintained OBS plugin that adds extra RTMP/SRT/WHIP output targets, so a
// single "Start Streaming" click in OBS goes out to a second platform too.
// We don't touch OBS's core stream settings (Settings > Stream stays the
// user's main platform, e.g. Twitch) — this only manages ONE extra target,
// the "second platform".
//
// Two things this plugin needs that obs-websocket cannot do for us, since
// neither is exposed by its API: the plugin binary itself has to be dropped
// into a folder OBS scans at startup, and its per-profile target list lives
// in a plain JSON file OBS only reads/writes at its own discretion — so
// both are handled the same way core_app.hpp already handles the CS2/Dota
// GSI .cfg files and the scene-collection JSON fallback (obs_scene_file.hpp):
// direct file I/O, gated on OBS being closed so we're not racing its own
// save/load.
namespace streamsoft::obs::multirtmp {

namespace fs = std::filesystem;
using json = nlohmann::json;

inline const std::string kGithubApiHost = "https://api.github.com";
inline const std::string kGithubApiPath = "/repos/sorayuki/obs-multi-rtmp/releases/latest";

// The one target we manage — identified by this fixed id so re-syncing
// updates our own entry in place instead of appending duplicates, and never
// touches any other target the user might add by hand inside the plugin's
// own OBS-side UI.
inline constexpr const char* kManagedTargetId = "streamsoft-secondary";

inline fs::path plugin_dir() {
    const char* program_data = std::getenv("ProgramData");
    if (!program_data) return {};
    return fs::path(program_data) / "obs-studio" / "plugins" / "obs-multi-rtmp";
}

inline fs::path plugin_dll_path() { return plugin_dir() / "bin" / "64bit" / "obs-multi-rtmp.dll"; }

inline bool is_plugin_installed() { return fs::exists(plugin_dll_path()); }

// user.ini's [Basic] section names the *currently active* OBS profile —
// ProfileDir (not the display-name Profile key) is the actual folder name
// on disk, since OBS sanitizes it for filesystem safety.
inline std::string find_active_profile_dir_name() {
    fs::path user_ini = obs_config_dir() / "user.ini";
    std::ifstream in(user_ini, std::ios::binary);
    if (!in) return {};

    std::string line;
    bool in_basic_section = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() >= 2 && line.front() == '[' && line.back() == ']') {
            in_basic_section = (line == "[Basic]");
            continue;
        }
        if (in_basic_section && line.rfind("ProfileDir=", 0) == 0) {
            return line.substr(std::string("ProfileDir=").size());
        }
    }
    return {};
}

inline fs::path find_active_profile_path() {
    std::string name = find_active_profile_dir_name();
    fs::path profiles_dir = obs_config_dir() / "basic" / "profiles";
    if (!name.empty() && fs::exists(profiles_dir / name)) return profiles_dir / name;

    // Fallback for the (rare) case user.ini doesn't have the key yet —
    // if there's exactly one profile, that's unambiguously the active one.
    if (fs::exists(profiles_dir)) {
        fs::path only;
        int count = 0;
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(profiles_dir, ec)) {
            if (!entry.is_directory()) continue;
            only = entry.path();
            ++count;
        }
        if (count == 1) return only;
    }
    return {};
}

inline fs::path multi_rtmp_json_path() {
    auto profile = find_active_profile_path();
    if (profile.empty()) return {};
    return profile / "obs-multi-rtmp.json";
}

struct ServiceEntry {
    std::string name;
    std::string server;
    std::string key_link;
    bool common = false;
};

// True for a URL obs-multi-rtmp's "RTMP" protocol target can actually use
// (it always creates an rtmp_output + rtmp_custom service, see
// protocols.cpp) — some catalog entries (YouTube's "HLS" variant, a few
// others) point at an HTTP(S) upload endpoint with a {stream_key} template
// instead of a real RTMP ingest, which would silently write a broken target.
inline bool is_rtmp_url(const std::string& url) {
    return url.rfind("rtmp://", 0) == 0 || url.rfind("rtmps://", 0) == 0;
}

// OBS ships (and keeps auto-updated in AppData) the exact same known-service
// catalog its own "Settings > Stream > Service" dropdown uses — Twitch,
// YouTube, Facebook, Trovo, etc. with their real ingest URLs and a direct
// link to each platform's own "get your stream key" page. Reading it means
// the user picks a platform by name and pastes only the stream key (exactly
// like OBS's own native UI), instead of having to go dig up a raw RTMP
// server URL themselves for anything other than a truly custom/unlisted
// service (VK Video Live, Kick — not in OBS's catalog as of this writing —
// still work via the plain "custom RTMP" server+key fields).
//
// Some services (Twitch especially, 46 regional entries) list many servers
// with no single obvious "closest" one — every entry's server/key_link here
// come straight from OBS's own data, so picking the first regional server is
// the same starting point OBS's own service dropdown would show too; the
// server field stays user-editable afterward for anyone who wants to swap
// regions.
inline std::vector<ServiceEntry> list_known_services() {
    std::vector<ServiceEntry> result;

    fs::path services_path = obs_config_dir() / "plugin_config" / "rtmp-services" / "services.json";
    if (!fs::exists(services_path)) return result;

    std::ifstream in(services_path, std::ios::binary);
    json root;
    try {
        in >> root;
    } catch (...) {
        return result;
    }
    if (!root.contains("services") || !root["services"].is_array()) return result;

    for (const auto& s : root["services"]) {
        if (!s.contains("name") || !s.contains("servers") || !s["servers"].is_array()) continue;

        std::string url;
        for (const auto& srv : s["servers"]) {
            if (!srv.contains("url")) continue;
            std::string candidate = srv["url"].get<std::string>();
            if (is_rtmp_url(candidate)) {
                url = candidate;
                break;
            }
        }
        if (url.empty()) continue;

        ServiceEntry entry;
        entry.name = s["name"].get<std::string>();
        entry.server = url;
        entry.key_link = s.value("stream_key_link", "");
        entry.common = s.value("common", false);
        result.push_back(std::move(entry));
    }
    return result;
}

// Reads OBS's own main "Settings > Stream" config (service.json, plaintext,
// same file OBS itself reads/writes) — if it's currently pointed at the same
// named service the user picked for the second platform, we can hand back
// the key they already entered there instead of making them go find it a
// second time. Read-only, so unlike sync_targets()/install_plugin() this is
// safe to call even while OBS is running.
inline std::string find_matching_stream_key(const std::string& service_name) {
    auto profile = find_active_profile_path();
    if (profile.empty()) return "";

    std::ifstream in(profile / "service.json", std::ios::binary);
    if (!in) return "";

    json root;
    try {
        in >> root;
    } catch (...) {
        return "";
    }
    if (!root.contains("settings")) return "";
    const auto& settings = root["settings"];
    if (settings.value("service", "") != service_name) return "";
    return settings.value("key", "");
}

struct Status {
    bool plugin_installed = false;
    bool obs_running = false;
    bool profile_found = false;
    std::string profile_name;
    bool synced = false;
};

inline Status read_status(const ConnectionsConfig& config) {
    Status s;
    s.plugin_installed = is_plugin_installed();
    s.obs_running = is_obs_running();
    auto profile = find_active_profile_path();
    s.profile_found = !profile.empty();
    s.profile_name = s.profile_found ? path_to_utf8(profile.filename()) : "";

    if (!s.profile_found || !config.should_run_multistream()) {
        s.synced = false;
        return s;
    }

    fs::path json_path = multi_rtmp_json_path();
    std::ifstream in(json_path, std::ios::binary);
    if (!in) return s;
    try {
        json root;
        in >> root;
        if (!root.contains("targets") || !root["targets"].is_array()) return s;
        for (const auto& t : root["targets"]) {
            if (!t.contains("id") || t["id"] != kManagedTargetId) continue;
            if (!t.contains("service-param")) continue;
            const auto& sp = t["service-param"];
            std::string server = sp.value("server", "");
            std::string key = sp.value("key", "");
            s.synced = (server == config.multistream_server && key == config.multistream_key);
            return s;
        }
    } catch (...) {
        // Malformed/unreadable file — just report "not synced", the
        // caller's sync() will overwrite it fresh once OBS is closed.
    }
    return s;
}

struct InstallResult {
    bool ok = false;
    std::string error;
};

// Downloads the latest Windows x64 release zip and remaps its layout
// (obs-plugins/64bit/*.dll, data/obs-plugins/<name>/*) into the
// %ProgramData%\obs-studio\plugins\<name>\{bin\64bit,data}\ layout OBS's
// user-plugin loader expects — deliberately not Program Files, so no
// elevation/UAC is needed, consistent with this app's lowest-privilege
// install (see installer/streamsoft.iss's PrivilegesRequired=lowest).
inline InstallResult install_plugin() {
    InstallResult r;
    if (is_obs_running()) {
        r.error = "Закрой OBS перед установкой — плагин подхватится только при следующем запуске, а пока OBS открыт, файлы могут быть заняты.";
        return r;
    }

    auto cli = twitch::make_https_client(kGithubApiHost);
    httplib::Headers headers{{"User-Agent", "StreamSoft-Native"}, {"Accept", "application/vnd.github+json"}};
    auto resp = cli.Get(kGithubApiPath, headers);
    if (!resp || resp->status != 200) {
        r.error = "Не удалось получить информацию о последней версии плагина с GitHub";
        return r;
    }

    auto release = crow::json::load(resp->body);
    if (!release || !release.has("assets")) {
        r.error = "GitHub вернул неожиданный ответ";
        return r;
    }

    std::string zip_url;
    for (const auto& asset : release["assets"]) {
        if (!asset.has("name") || !asset.has("browser_download_url")) continue;
        std::string name = std::string(asset["name"].s());
        if (name.rfind("obs-multi-rtmp-", 0) == 0 && name.find("windows-x64.zip") != std::string::npos) {
            zip_url = std::string(asset["browser_download_url"].s());
            break;
        }
    }
    if (zip_url.empty()) {
        r.error = "Не нашёл сборку под Windows x64 в последнем релизе плагина";
        return r;
    }

    fs::path tmp_dir = fs::temp_directory_path() / "streamsoft_multirtmp_tmp";
    std::error_code ec;
    fs::remove_all(tmp_dir, ec);
    fs::create_directories(tmp_dir, ec);

    fs::path zip_path = tmp_dir / "plugin.zip";
    std::string error;
    if (!detail::download_file(zip_url, zip_path, [](std::uint64_t, std::uint64_t) {}, error)) {
        r.error = "Скачивание плагина не удалось: " + error;
        fs::remove_all(tmp_dir, ec);
        return r;
    }

    fs::path extracted = tmp_dir / "extracted";
    if (!detail::extract_zip_via_shell(zip_path, extracted, error)) {
        r.error = "Распаковка плагина не удалась: " + error;
        fs::remove_all(tmp_dir, ec);
        return r;
    }

    fs::path dest_bin = plugin_dir() / "bin" / "64bit";
    fs::path dest_locale = plugin_dir() / "data" / "locale";
    fs::create_directories(dest_bin, ec);
    fs::create_directories(dest_locale, ec);

    fs::path src_dll = extracted / "obs-plugins" / "64bit" / "obs-multi-rtmp.dll";
    if (!fs::exists(src_dll)) {
        r.error = "В архиве плагина не нашёл obs-multi-rtmp.dll — формат релиза мог поменяться";
        fs::remove_all(tmp_dir, ec);
        return r;
    }
    fs::copy_file(src_dll, dest_bin / "obs-multi-rtmp.dll", fs::copy_options::overwrite_existing, ec);
    if (ec) {
        r.error = "Не удалось скопировать obs-multi-rtmp.dll: " + ec.message();
        fs::remove_all(tmp_dir, ec);
        return r;
    }

    fs::path src_locale = extracted / "data" / "obs-plugins" / "obs-multi-rtmp" / "locale";
    if (fs::exists(src_locale)) {
        for (const auto& entry : fs::directory_iterator(src_locale)) {
            fs::copy_file(entry.path(), dest_locale / entry.path().filename(), fs::copy_options::overwrite_existing, ec);
        }
    }

    fs::remove_all(tmp_dir, ec);

    r.ok = true;
    return r;
}

struct SyncResult {
    bool ok = false;
    std::string error;
};

// Upserts (or removes) our one managed target inside the active profile's
// obs-multi-rtmp.json — everything else in that file (targets the user
// added by hand in the plugin's own dock, encoder configs) is read back
// and preserved untouched.
inline SyncResult sync_targets(const ConnectionsConfig& config) {
    SyncResult r;
    if (is_obs_running()) {
        r.error = "Закрой OBS, чтобы применить — иначе он перезапишет файл своими же настройками при выходе.";
        return r;
    }

    fs::path json_path = multi_rtmp_json_path();
    if (json_path.empty()) {
        r.error = "Не нашёл активный профиль OBS (%APPDATA%/obs-studio/basic/profiles/). Запусти OBS хотя бы один раз и попробуй снова.";
        return r;
    }

    json root;
    root["targets"] = json::array();
    root["video_configs"] = json::array();
    root["audio_configs"] = json::array();
    if (fs::exists(json_path)) {
        std::ifstream in(json_path, std::ios::binary);
        try {
            in >> root;
        } catch (const std::exception& e) {
            r.error = std::string("Не удалось прочитать текущий obs-multi-rtmp.json (повреждён?): ") + e.what();
            return r;
        }
        if (!root.contains("targets") || !root["targets"].is_array()) root["targets"] = json::array();
    }

    auto& targets = root["targets"];
    json::iterator existing = targets.end();
    for (auto it = targets.begin(); it != targets.end(); ++it) {
        if (it->contains("id") && (*it)["id"] == kManagedTargetId) {
            existing = it;
            break;
        }
    }

    if (config.should_run_multistream()) {
        json entry;
        entry["id"] = kManagedTargetId;
        entry["name"] = config.multistream_label.empty() ? "StreamSoft: вторая площадка" : config.multistream_label;
        entry["protocol"] = "RTMP";
        entry["sync-start"] = true;
        entry["sync-stop"] = true;
        entry["service-param"] = {{"server", config.multistream_server}, {"key", config.multistream_key}};
        entry["output-param"] = json::object();

        if (existing != targets.end()) {
            *existing = entry;
        } else {
            targets.push_back(entry);
        }
    } else if (existing != targets.end()) {
        targets.erase(existing);
    }

    try {
        std::ofstream out(json_path, std::ios::binary | std::ios::trunc);
        out << root.dump(1, ' ');
    } catch (const std::exception& e) {
        r.error = std::string("Не удалось сохранить obs-multi-rtmp.json: ") + e.what();
        return r;
    }

    r.ok = true;
    return r;
}

}
