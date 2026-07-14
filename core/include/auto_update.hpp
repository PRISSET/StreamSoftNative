#pragma once

#include "app_paths.hpp"
#include "module_installer.hpp"
#include "twitch_auth.hpp"

#include <crow/json.h>
#include <crow/logging.h>
#include <httplib.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#ifndef STREAMSOFT_VERSION
#define STREAMSOFT_VERSION "0.0.0"
#endif

namespace streamsoft {

inline const std::string kUpdateApiHost = "https://api.github.com";
inline const std::string kUpdateApiPath = "/repos/PRISSET/StreamSoftNative/releases?per_page=20";
inline constexpr const char* kUpdateAssetName = "StreamSoftSetup.exe";

inline std::vector<int> parse_version(const std::string& raw) {
    std::vector<int> parts;
    std::string s = raw;
    if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) s = s.substr(1);

    std::string current;
    for (char c : s) {
        if (c == '.') {
            parts.push_back(current.empty() ? 0 : std::stoi(current));
            current.clear();
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            current.push_back(c);
        } else {
            break;
        }
    }
    if (!current.empty()) parts.push_back(std::stoi(current));
    return parts;
}

inline bool version_greater(const std::vector<int>& a, const std::vector<int>& b) {
    for (size_t i = 0; i < (std::max)(a.size(), b.size()); ++i) {
        int av = i < a.size() ? a[i] : 0;
        int bv = i < b.size() ? b[i] : 0;
        if (av != bv) return av > bv;
    }
    return false;
}

struct UpdateCheckResult {
    bool update_available = false;
    std::string latest_version;
    std::string installer_url;
};

inline UpdateCheckResult check_for_update() {
    UpdateCheckResult result;

    auto cli = twitch::make_https_client(kUpdateApiHost);

    httplib::Headers headers{{"User-Agent", "StreamSoft-Native"}, {"Accept", "application/vnd.github+json"}};
    auto resp = cli.Get(kUpdateApiPath, headers);
    if (!resp) {
        CROW_LOG_WARNING << "Проверка обновлений не удалась: " << httplib::to_string(resp.error());
        return result;
    }
    if (resp->status != 200) {
        CROW_LOG_WARNING << "Проверка обновлений: GitHub ответил статусом " << resp->status;
        return result;
    }

    auto releases = crow::json::load(resp->body);
    if (!releases) return result;

    std::vector<int> best_version;
    for (const auto& release : releases) {
        if (!release.has("tag_name") || !release.has("assets")) continue;
        if (release.has("draft") && release["draft"].b()) continue;
        if (release.has("prerelease") && release["prerelease"].b()) continue;

        for (const auto& asset : release["assets"]) {
            if (!asset.has("name") || !asset.has("browser_download_url")) continue;
            if (std::string(asset["name"].s()) != kUpdateAssetName) continue;

            std::string tag = std::string(release["tag_name"].s());
            auto version = parse_version(tag);
            if (best_version.empty() || version_greater(version, best_version)) {
                best_version = version;
                result.latest_version = tag;
                result.installer_url = std::string(asset["browser_download_url"].s());
            }
            break;
        }
    }

    result.update_available = !best_version.empty() && version_greater(best_version, parse_version(STREAMSOFT_VERSION));
    if (!result.update_available) {
        result.latest_version.clear();
        result.installer_url.clear();
    }
    return result;
}

struct ReleaseInfo {
    std::string version;
    std::string name;
    std::string notes;
    std::string published_at;
};

inline std::vector<ReleaseInfo> fetch_release_history() {
    std::vector<ReleaseInfo> result;

    auto cli = twitch::make_https_client(kUpdateApiHost);

    httplib::Headers headers{{"User-Agent", "StreamSoft-Native"}, {"Accept", "application/vnd.github+json"}};
    auto resp = cli.Get(kUpdateApiPath, headers);
    if (!resp) {
        CROW_LOG_WARNING << "Не удалось получить список релизов: " << httplib::to_string(resp.error());
        return result;
    }
    if (resp->status != 200) {
        CROW_LOG_WARNING << "Список релизов: GitHub ответил статусом " << resp->status;
        return result;
    }

    auto releases = crow::json::load(resp->body);
    if (!releases) return result;

    for (const auto& release : releases) {
        if (!release.has("tag_name") || !release.has("assets")) continue;
        if (release.has("draft") && release["draft"].b()) continue;
        if (release.has("prerelease") && release["prerelease"].b()) continue;

        bool has_installer = false;
        for (const auto& asset : release["assets"]) {
            if (asset.has("name") && std::string(asset["name"].s()) == kUpdateAssetName) {
                has_installer = true;
                break;
            }
        }
        if (!has_installer) continue;

        ReleaseInfo info;
        info.version = std::string(release["tag_name"].s());
        info.name = release.has("name") ? std::string(release["name"].s()) : info.version;
        info.notes = release.has("body") ? std::string(release["body"].s()) : "";
        info.published_at = release.has("published_at") ? std::string(release["published_at"].s()) : "";
        result.push_back(std::move(info));
    }

    std::sort(result.begin(), result.end(), [](const ReleaseInfo& a, const ReleaseInfo& b) {
        return version_greater(parse_version(a.version), parse_version(b.version));
    });
    return result;
}

inline void launch_silent_update(const std::filesystem::path& installer_path) {
    std::wstring cmd = L"\"" + installer_path.wstring() +
                        L"\" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CLOSEAPPLICATIONS /RESTARTAPPLICATIONS";
    std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
    cmd_buf.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(nullptr, cmd_buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
                              &pi);
    if (ok) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        CROW_LOG_INFO << "Обновление запущено (" << installer_path.string() << "), приложение скоро перезапустится";
    } else {
        CROW_LOG_ERROR << "Не удалось запустить установщик обновления (код " << GetLastError() << ")";
    }
}

inline void run_auto_updater() {
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(60s);

    while (true) {
        auto update = check_for_update();
        if (update.update_available) {
            CROW_LOG_INFO << "Доступно обновление " << update.latest_version << " (текущая версия "
                          << STREAMSOFT_VERSION << "), скачиваю...";

            wchar_t temp_dir[MAX_PATH];
            GetTempPathW(MAX_PATH, temp_dir);
            std::filesystem::path installer_path = std::filesystem::path(temp_dir) / "StreamSoftUpdate.exe";

            std::string error;
            bool ok = detail::download_file(
                update.installer_url, installer_path, [](std::uint64_t, std::uint64_t) {}, error);
            if (ok) {
                launch_silent_update(installer_path);
                return;
            }
            CROW_LOG_ERROR << "Не удалось скачать обновление: " << error;
        }

        std::this_thread::sleep_for(6h);
    }
}

}
