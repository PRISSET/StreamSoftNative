#pragma once

// Generic "Check & Install" downloader for optional modules (TTS/RVC) — see
// CLAUDE.md §2. Downloads a manifest of zip parts over HTTPS (httplib, same
// verified-cert client as twitch_auth.hpp) with a progress callback, then
// extracts each zip via the Windows Shell's own zip-folder support
// (IShellDispatch::NameSpace + Folder::CopyHere) — no new vcpkg zip
// dependency (miniz/libzip) for this, Explorer already has it built in on
// every Windows install.

#include <crow/logging.h>
#include <httplib.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// See discord_presence.hpp's identical guard for why this has to come
// before any of comdef.h/shellapi.h/shlobj.h/windows.h — they all pull in
// <windows.h> transitively, and Asio (via crow.h elsewhere in this TU)
// needs winsock2.h, not the legacy winsock.h a bare windows.h include pulls
// in.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <comdef.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shldisp.h>
#include <windows.h>

#include "app_paths.hpp"
#include "twitch_auth.hpp"  // make_https_client()

namespace streamsoft {

struct ModulePackagePart {
    std::string url;  // full https URL, e.g. a GitHub Releases asset
};

struct ModuleManifest {
    std::string name;                     // "tts" | "rvc" — REST path segment
    std::filesystem::path install_dir;    // adapters/<name>
    std::vector<ModulePackagePart> parts;  // downloaded+extracted in order
    bool requires_gpu = false;
    std::uint64_t required_disk_mb = 0;
    std::uint64_t total_download_mb = 0;  // known upfront, shown before install starts
};

// Real package URLs go here once built and uploaded to GitHub Releases on
// PRISSET/StreamSoftNative — placeholders below until then (this is the one
// piece that needs real multi-GB assets uploaded, which has to happen
// outside this codebase). RVC is split into multiple parts because GitHub
// Releases caps a single asset at 2GB.
//
// Both zips just need embeddable Python (self-contained, no pip-install on
// the end user's machine) plus this repo's own adapters/tts/ and
// adapters/rvc/ (server.py + requirements.txt already here) with their
// requirements installed into it. Real upstream sources, for whoever builds
// these zips:
//   - Embeddable Python (Windows): python.org's official
//     python-<ver>-embed-amd64.zip, plus get-pip.py (bootstrap.pypa.io) to
//     add pip to it (the embeddable distribution ships without pip).
//   - TTS zip → extracts to adapters/tts, needs venv/Scripts/python.exe +
//     server.py (see tts_launcher.hpp::is_installed()): the embeddable
//     Python above with adapters/tts/requirements.txt pip-installed, plus
//     adapters/tts/server.py (already in this repo).
//   - RVC zip(s) → extract to adapters/rvc: same embeddable Python approach
//     with adapters/rvc/requirements.txt pip-installed (pulls in
//     `rvc-python`, https://github.com/daswer123/rvc-python) plus
//     adapters/rvc/server.py (already in this repo). Torch itself needs the
//     CUDA build specifically — `torch==2.5.1+cu121`/`torchaudio==2.5.1+cu121`
//     from https://download.pytorch.org/whl/cu121, not the default PyPI
//     wheel. Base models — hubert_base.pt, rmvpe.pt, rmvpe.onnx — come from
//     https://huggingface.co/Daswer123/RVC_Base/resolve/main/ (same URLs
//     rvc-python's own download_model.py uses). The actual voice
//     (ayaka.pth/ayaka.index) has no upstream source — bundle those two
//     files into the RVC zip directly so a fresh install has a working
//     voice out of the box.
inline const ModuleManifest& tts_module_manifest() {
    static const ModuleManifest m{
        "tts",
        resolve_resource_dir("adapters/tts", STREAMSOFT_TTS_ADAPTER_DIR),
        {ModulePackagePart{"https://github.com/PRISSET/StreamSoftNative/releases/download/tts-v1/tts-adapter.zip"}},
        false,
        512,
        60,
    };
    return m;
}

inline const ModuleManifest& rvc_module_manifest() {
    static const ModuleManifest m{
        "rvc",
        resolve_resource_dir("adapters/rvc", STREAMSOFT_RVC_ADAPTER_DIR),
        {
            ModulePackagePart{"https://github.com/PRISSET/StreamSoftNative/releases/download/rvc-v1/rvc-part1-runtime.zip"},
            ModulePackagePart{"https://github.com/PRISSET/StreamSoftNative/releases/download/rvc-v1/rvc-part2-models.zip"},
        },
        true,
        8192,
        6800,
    };
    return m;
}

inline const ModuleManifest* find_module_manifest(const std::string& name) {
    if (name == "tts") return &tts_module_manifest();
    if (name == "rvc") return &rvc_module_manifest();
    return nullptr;
}

enum class ModuleInstallState {
    Idle,
    Downloading,
    Extracting,
    Installed,
    Failed,
};

inline const char* module_state_name(ModuleInstallState s) {
    switch (s) {
        case ModuleInstallState::Idle: return "idle";
        case ModuleInstallState::Downloading: return "downloading";
        case ModuleInstallState::Extracting: return "extracting";
        case ModuleInstallState::Installed: return "installed";
        case ModuleInstallState::Failed: return "failed";
    }
    return "idle";
}

struct ModuleProgress {
    std::mutex mutex;
    ModuleInstallState state = ModuleInstallState::Idle;
    int file_index = 0;   // 1-based, for display ("часть 1 из 2")
    int file_count = 0;
    std::uint64_t bytes_downloaded = 0;
    std::uint64_t bytes_total = 0;
    std::string error;
};

// One tracker per module name, created on first access — same lazy-registry
// shape as auth_prompt_state() in twitch_auth.hpp, just keyed by name since
// there's more than one module.
inline ModuleProgress& module_progress(const std::string& name) {
    static std::mutex registry_mutex;
    static std::map<std::string, std::unique_ptr<ModuleProgress>> registry;
    std::lock_guard<std::mutex> lock(registry_mutex);
    auto it = registry.find(name);
    if (it == registry.end()) {
        it = registry.emplace(name, std::make_unique<ModuleProgress>()).first;
    }
    return *it->second;
}

// A module is "installed" once this marker exists — written only after every
// part downloaded and extracted without error. More robust than checking for
// specific files (which differ per module and might partially exist from a
// half-finished install) as the single source of truth for the GUI's button
// state.
inline bool is_module_installed(const ModuleManifest& manifest) {
    return std::filesystem::exists(manifest.install_dir / ".streamsoft_installed");
}

namespace detail {

// Splits "https://host[:port]/path?query" into ("https://host[:port]",
// "/path?query") — httplib::Client's constructor wants the former, Get()
// wants the latter. No existing helper in the codebase does this since
// every other client here talks to one fixed, hardcoded host.
inline std::pair<std::string, std::string> split_url(const std::string& url) {
    auto scheme_end = url.find("://");
    if (scheme_end == std::string::npos) return {url, "/"};
    auto host_start = scheme_end + 3;
    auto path_start = url.find('/', host_start);
    if (path_start == std::string::npos) return {url, "/"};
    return {url.substr(0, path_start), url.substr(path_start)};
}

inline bool extract_zip_via_shell(const std::filesystem::path& zip_path, const std::filesystem::path& dest_dir,
                                   std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(dest_dir, ec);

    HRESULT init_hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool need_uninit = SUCCEEDED(init_hr);
    if (FAILED(init_hr) && init_hr != RPC_E_CHANGED_MODE) {
        error = "CoInitializeEx failed";
        return false;
    }

    bool ok = false;
    {
        IShellDispatch* shell = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_Shell, nullptr, CLSCTX_INPROC_SERVER, IID_IShellDispatch,
                                       reinterpret_cast<void**>(&shell));
        if (FAILED(hr) || !shell) {
            error = "Не удалось создать Shell.Application (COM)";
        } else {
            VARIANT zip_var{};
            zip_var.vt = VT_BSTR;
            zip_var.bstrVal = SysAllocString(zip_path.wstring().c_str());

            VARIANT dest_var{};
            dest_var.vt = VT_BSTR;
            dest_var.bstrVal = SysAllocString(dest_dir.wstring().c_str());

            Folder* zip_folder = nullptr;
            Folder* dest_folder = nullptr;
            shell->NameSpace(zip_var, &zip_folder);
            shell->NameSpace(dest_var, &dest_folder);

            if (!zip_folder || !dest_folder) {
                error = "Shell не смог открыть архив или папку назначения";
            } else {
                FolderItems* items = nullptr;
                zip_folder->Items(&items);
                if (!items) {
                    error = "Архив пустой или повреждён";
                } else {
                    VARIANT item_var{};
                    item_var.vt = VT_DISPATCH;
                    item_var.pdispVal = items;

                    // FOF_NO_UI = FOF_SILENT | FOF_NOCONFIRMATION | FOF_NOERRORUI |
                    // FOF_NOCONFIRMMKDIR — no progress dialog, no "are you sure",
                    // no error popups; failures just come back as an HRESULT.
                    VARIANT opt{};
                    opt.vt = VT_I4;
                    opt.lVal = FOF_NO_UI;

                    hr = dest_folder->CopyHere(item_var, opt);
                    ok = SUCCEEDED(hr);
                    if (!ok) error = "CopyHere завершился с ошибкой";
                    items->Release();
                }
                dest_folder->Release();
                zip_folder->Release();
            }

            VariantClear(&zip_var);
            VariantClear(&dest_var);
            shell->Release();
        }
    }

    if (need_uninit) CoUninitialize();
    return ok;
}

}  // namespace detail

// Runs on a detached background thread — see install_module_async(). All
// state changes go through the module's ModuleProgress under its mutex so
// GET /api/modules/<name>/progress (polled from the GUI, same pattern as
// /api/twitch/auth-status) always sees a consistent snapshot.
inline void run_module_install(ModuleManifest manifest) {
    auto& progress = module_progress(manifest.name);

    auto set_state = [&](ModuleInstallState s) {
        std::lock_guard<std::mutex> lock(progress.mutex);
        progress.state = s;
    };
    auto fail = [&](const std::string& msg) {
        std::lock_guard<std::mutex> lock(progress.mutex);
        progress.state = ModuleInstallState::Failed;
        progress.error = msg;
        CROW_LOG_ERROR << "Установка модуля " << manifest.name << " не удалась: " << msg;
    };

    {
        std::lock_guard<std::mutex> lock(progress.mutex);
        progress.state = ModuleInstallState::Downloading;
        progress.file_index = 0;
        progress.file_count = static_cast<int>(manifest.parts.size());
        progress.bytes_downloaded = 0;
        progress.bytes_total = 0;
        progress.error.clear();
    }

    std::filesystem::path tmp_dir = manifest.install_dir.parent_path() / (manifest.name + "_download_tmp");
    std::error_code ec;
    std::filesystem::create_directories(tmp_dir, ec);

    std::vector<std::filesystem::path> downloaded_zips;

    for (size_t i = 0; i < manifest.parts.size(); ++i) {
        const auto& part = manifest.parts[i];
        {
            std::lock_guard<std::mutex> lock(progress.mutex);
            progress.file_index = static_cast<int>(i) + 1;
            progress.bytes_downloaded = 0;
            progress.bytes_total = 0;
        }

        auto [host, path] = detail::split_url(part.url);
        auto cli = twitch::make_https_client(host);
        cli.set_follow_location(true);

        std::filesystem::path zip_path = tmp_dir / ("part" + std::to_string(i) + ".zip");
        std::ofstream out(zip_path, std::ios::binary | std::ios::trunc);
        if (!out) {
            fail("Не удалось создать временный файл для скачивания");
            return;
        }

        auto result = cli.Get(
            path,
            [&](const char* data, size_t len) {
                out.write(data, static_cast<std::streamsize>(len));
                return true;
            },
            [&](std::uint64_t current, std::uint64_t total) {
                std::lock_guard<std::mutex> lock(progress.mutex);
                progress.bytes_downloaded = current;
                progress.bytes_total = total;
                return true;
            });
        out.close();

        if (!result || result->status != 200) {
            fail("Скачивание не удалось (часть " + std::to_string(i + 1) + "/" + std::to_string(manifest.parts.size()) +
                 ")");
            return;
        }
        downloaded_zips.push_back(zip_path);
    }

    set_state(ModuleInstallState::Extracting);
    for (const auto& zip_path : downloaded_zips) {
        std::string extract_error;
        if (!detail::extract_zip_via_shell(zip_path, manifest.install_dir, extract_error)) {
            fail("Распаковка не удалась: " + extract_error);
            return;
        }
    }

    for (const auto& zip_path : downloaded_zips) {
        std::filesystem::remove(zip_path, ec);
    }
    std::filesystem::remove(tmp_dir, ec);

    std::ofstream marker(manifest.install_dir / ".streamsoft_installed", std::ios::trunc);
    marker << "ok";
    marker.close();

    set_state(ModuleInstallState::Installed);
    CROW_LOG_INFO << "Модуль " << manifest.name << " успешно установлен";
}

// Fire-and-forget: REST handler calls this and returns immediately, the GUI
// polls progress separately. Detached because the handler's lifetime ends
// long before installation does — nothing needs to join this thread, only
// read its published progress.
inline bool install_module_async(const ModuleManifest& manifest) {
    auto& progress = module_progress(manifest.name);
    {
        std::lock_guard<std::mutex> lock(progress.mutex);
        if (progress.state == ModuleInstallState::Downloading || progress.state == ModuleInstallState::Extracting) {
            return false;  // already running
        }
    }
    std::thread(run_module_install, manifest).detach();
    return true;
}

}  // namespace streamsoft
