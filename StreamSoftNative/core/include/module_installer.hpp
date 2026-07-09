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
#include <functional>
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
#include "gpu_check.hpp"  // detect_cuda_wheel_tag()
#include "twitch_auth.hpp"  // make_https_client()

namespace streamsoft {

struct ModulePackagePart {
    std::string url;  // full https URL, e.g. a GitHub Releases asset
};

// Zip: download prebuilt zip part(s), extract via Shell — used by TTS, whose
// dependencies (edge-tts, fastapi) are the same on every machine.
//
// PipLive: bootstrap embeddable Python on the end user's own machine and
// pip-install everything fresh from PyPI/pytorch.org — used by RVC, because
// torch needs a CUDA build matched to *that machine's* driver
// (detect_cuda_wheel_tag() in gpu_check.hpp), which a single fixed prebuilt
// zip can't be. Trades a bigger install-time failure surface (network to
// PyPI/pytorch.org, not just one GitHub asset) for actually being adaptive
// and for never going stale relative to upstream torch/rvc-python releases.
enum class ModuleInstallKind {
    Zip,
    PipLive,
};

struct ModuleManifest {
    std::string name;                     // "tts" | "rvc" — REST path segment
    std::filesystem::path install_dir;    // adapters/<name>
    ModuleInstallKind kind = ModuleInstallKind::Zip;
    std::vector<ModulePackagePart> parts;  // Zip: downloaded+extracted in order
    bool requires_gpu = false;
    std::uint64_t required_disk_mb = 0;
    std::uint64_t total_download_mb = 0;  // estimate shown before install starts

    // PipLive only:
    std::string python_version;             // embeddable Python to bootstrap, e.g. "3.10.11"
    std::vector<std::string> pip_packages;  // installed after torch, plain `pip install <name>...`
    std::string voice_model_url;            // small zip (ayaka.pth/ayaka.index) — no upstream source, hosted by us
};

// TTS: real package uploaded to GitHub Releases on PRISSET/StreamSoftNative
// (embeddable Python + adapters/tts/requirements.txt pip-installed +
// adapters/tts/server.py, already in this repo — same "no pip-install in
// front of the user" shape as the plan originally called for, since none of
// TTS's dependencies are hardware-specific).
inline const ModuleManifest& tts_module_manifest() {
    static const ModuleManifest m{
        "tts",
        resolve_resource_dir("adapters/tts", STREAMSOFT_TTS_ADAPTER_DIR),
        ModuleInstallKind::Zip,
        {ModulePackagePart{"https://github.com/PRISSET/StreamSoftNative/releases/download/tts-v1/tts-adapter.zip"}},
        false,
        512,
        60,
        "",
        {},
        "",
    };
    return m;
}

// RVC: live pip install, adaptive to the machine's actual CUDA driver
// version (see gpu_check.hpp). torch/torchaudio come straight from
// download.pytorch.org, rvc-python/fastapi/uvicorn/pydantic straight from
// PyPI — same real upstream sources as softforstream/rvc_service's own
// requirements.txt. Only the ayaka voice model has no upstream source (it's
// not a library, just one specific trained checkpoint) and is hosted as a
// small separate GitHub release, kept deliberately tiny compared to the
// ~5.7GB the full runtime would be if we tried to prebuild it ourselves.
inline const ModuleManifest& rvc_module_manifest() {
    static const ModuleManifest m{
        "rvc",
        resolve_resource_dir("adapters/rvc", STREAMSOFT_RVC_ADAPTER_DIR),
        ModuleInstallKind::PipLive,
        {},
        true,
        12288,
        3500,
        "3.10.11",
        {"rvc-python", "fastapi", "uvicorn", "pydantic"},
        "https://github.com/PRISSET/StreamSoftNative/releases/download/rvc-voice-v1/ayaka.zip",
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
    Installing,  // PipLive only: pip/subprocess steps running (no byte progress)
    Installed,
    Failed,
};

inline const char* module_state_name(ModuleInstallState s) {
    switch (s) {
        case ModuleInstallState::Idle: return "idle";
        case ModuleInstallState::Downloading: return "downloading";
        case ModuleInstallState::Extracting: return "extracting";
        case ModuleInstallState::Installing: return "installing";
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
    std::string current_step;  // PipLive only: human-readable stage ("Установка torch (cu121)…")
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

// Streams one URL straight to disk with a progress callback — the same
// download logic run_module_install()'s Zip path uses per-part, pulled out
// so the PipLive path (embeddable Python zip, get-pip.py, voice model zip)
// can reuse it for its own single-file downloads instead of duplicating the
// httplib ceremony three times.
inline bool download_file(const std::string& url, const std::filesystem::path& dest,
                           const std::function<void(std::uint64_t, std::uint64_t)>& on_progress,
                           std::string& error) {
    auto [host, path] = split_url(url);
    auto cli = twitch::make_https_client(host);
    cli.set_follow_location(true);

    std::ofstream out(dest, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "Не удалось создать файл " + dest.string();
        return false;
    }

    auto result = cli.Get(
        path,
        [&](const char* data, size_t len) {
            out.write(data, static_cast<std::streamsize>(len));
            return true;
        },
        [&](std::uint64_t current, std::uint64_t total) {
            if (on_progress) on_progress(current, total);
            return true;
        });
    out.close();

    if (!result || result->status != 200) {
        error = "Скачивание не удалось: " + url;
        return false;
    }
    return true;
}

// Runs one command line to completion, discarding its output — used for
// pip/python invocations during the PipLive install, where the only thing
// that matters is the exit code (stdout/stderr would just be pip's own
// progress bars, not useful once mediated through our own stage-based
// progress reporting). Returns false on launch failure or non-zero exit.
inline bool run_subprocess_and_wait(const std::wstring& exe_path, const std::wstring& args,
                                     const std::filesystem::path& working_dir, std::string& error) {
    std::wstring cmd = L"\"" + exe_path + L"\" " + args;
    std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
    cmd_buf.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    std::wstring cwd = working_dir.wstring();
    BOOL ok = CreateProcessW(nullptr, cmd_buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, cwd.c_str(),
                              &si, &pi);
    if (!ok) {
        error = "Не удалось запустить процесс (код " + std::to_string(GetLastError()) + ")";
        return false;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 1;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (exit_code != 0) {
        error = "Процесс завершился с кодом " + std::to_string(exit_code);
        return false;
    }
    return true;
}

inline bool extract_zip_via_shell(const std::filesystem::path& zip_path_in, const std::filesystem::path& dest_dir_in,
                                   std::string& error) {
    // IShellDispatch::NameSpace() silently returns null for a path that
    // mixes '/' and '\' separators (verified: std::filesystem::exists()
    // resolves the exact same string fine, since Win32 file APIs normalize
    // separators — Shell Automation's string-based path parsing doesn't).
    // Paths built from STREAMSOFT_*_DIR (a CMake variable, forward-slash by
    // convention) concatenated via std::filesystem::path's operator/
    // (native backslash) end up mixed, so this always needs to run.
    std::filesystem::path zip_path = std::filesystem::path(zip_path_in).make_preferred();
    std::filesystem::path dest_dir = std::filesystem::path(dest_dir_in).make_preferred();

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

using FailFn = std::function<void(const std::string&)>;
using SetStateFn = std::function<void(ModuleInstallState)>;

inline void run_zip_install(const ModuleManifest& manifest, ModuleProgress& progress, const SetStateFn& set_state,
                             const FailFn& fail) {
    {
        std::lock_guard<std::mutex> lock(progress.mutex);
        progress.file_index = 0;
        progress.file_count = static_cast<int>(manifest.parts.size());
    }

    std::filesystem::path tmp_dir = manifest.install_dir.parent_path() / (manifest.name + "_download_tmp");
    std::error_code ec;
    std::filesystem::create_directories(tmp_dir, ec);

    std::vector<std::filesystem::path> downloaded_zips;

    for (size_t i = 0; i < manifest.parts.size(); ++i) {
        {
            std::lock_guard<std::mutex> lock(progress.mutex);
            progress.file_index = static_cast<int>(i) + 1;
            progress.bytes_downloaded = 0;
            progress.bytes_total = 0;
        }

        std::filesystem::path zip_path = tmp_dir / ("part" + std::to_string(i) + ".zip");
        std::string error;
        bool ok = detail::download_file(
            manifest.parts[i].url, zip_path,
            [&](std::uint64_t current, std::uint64_t total) {
                std::lock_guard<std::mutex> lock(progress.mutex);
                progress.bytes_downloaded = current;
                progress.bytes_total = total;
            },
            error);

        if (!ok) {
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
}

namespace detail {

// "3.10.11" -> "310" (the bit embeddable Python uses in pythonXY[Z]._pth /
// pythonXY.dll) — just the first two dot-separated components, digits only.
inline std::string python_abi_tag(const std::string& version) {
    std::string tag;
    int dots = 0;
    for (char c : version) {
        if (c == '.') {
            if (++dots >= 2) break;
            continue;
        }
        tag.push_back(c);
    }
    return tag;
}

}  // namespace detail

// RVC's install path: bootstrap a fresh embeddable Python on this specific
// machine, then pip-install torch matched to its actual CUDA driver — see
// the ModuleManifest::kind comment for why this trades a bigger failure
// surface for genuinely being adaptive instead of one fixed prebuilt zip.
inline void run_pip_live_install(const ModuleManifest& manifest, ModuleProgress& progress, const SetStateFn& set_state,
                                  const FailFn& fail) {
    auto set_step = [&](const std::string& step) {
        std::lock_guard<std::mutex> lock(progress.mutex);
        progress.current_step = step;
    };

    auto cuda = detect_cuda_wheel_tag();
    if (!cuda.available) {
        fail(cuda.reason.empty() ? "Не удалось определить версию CUDA-драйвера" : cuda.reason);
        return;
    }

    std::filesystem::path venv_scripts = manifest.install_dir / "venv" / "Scripts";
    std::error_code ec;
    std::filesystem::create_directories(venv_scripts, ec);
    std::filesystem::path tmp_dir = manifest.install_dir.parent_path() / (manifest.name + "_download_tmp");
    std::filesystem::create_directories(tmp_dir, ec);

    // 1. Embeddable Python — python.org's own official distribution.
    set_step("Скачивание Python " + manifest.python_version + "...");
    {
        std::lock_guard<std::mutex> lock(progress.mutex);
        progress.file_index = 1;
        progress.file_count = 5;
        progress.bytes_downloaded = 0;
        progress.bytes_total = 0;
    }
    std::string url = "https://www.python.org/ftp/python/" + manifest.python_version + "/python-" +
                       manifest.python_version + "-embed-amd64.zip";
    std::filesystem::path python_zip = tmp_dir / "python-embed.zip";
    std::string error;
    if (!detail::download_file(
            url, python_zip,
            [&](std::uint64_t current, std::uint64_t total) {
                std::lock_guard<std::mutex> lock(progress.mutex);
                progress.bytes_downloaded = current;
                progress.bytes_total = total;
            },
            error)) {
        fail(error);
        return;
    }

    set_state(ModuleInstallState::Extracting);
    if (!detail::extract_zip_via_shell(python_zip, venv_scripts, error)) {
        fail("Не удалось распаковать embeddable Python: " + error);
        return;
    }
    std::filesystem::remove(python_zip, ec);
    set_state(ModuleInstallState::Downloading);

    // 2. Enable `import site` in the ._pth file — embeddable Python ships
    // with it commented out, which keeps pip (and anything it installs into
    // Lib/site-packages) invisible to the interpreter.
    std::filesystem::path pth_path = venv_scripts / ("python" + detail::python_abi_tag(manifest.python_version) + "._pth");
    {
        std::ifstream pth_in(pth_path, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(pth_in)), std::istreambuf_iterator<char>());
        pth_in.close();
        auto pos = content.find("#import site");
        if (pos != std::string::npos) content.replace(pos, 12, "import site");
        std::ofstream pth_out(pth_path, std::ios::binary | std::ios::trunc);
        pth_out << content;
    }

    std::filesystem::path python_exe = venv_scripts / "python.exe";

    // 3. pip, via get-pip.py (bootstrap.pypa.io) — the embeddable
    // distribution doesn't include it.
    set_step("Установка pip...");
    {
        std::lock_guard<std::mutex> lock(progress.mutex);
        progress.file_index = 2;
        progress.bytes_downloaded = 0;
        progress.bytes_total = 0;
    }
    std::filesystem::path get_pip = venv_scripts / "get-pip.py";
    if (!detail::download_file(
            "https://bootstrap.pypa.io/get-pip.py", get_pip,
            [&](std::uint64_t current, std::uint64_t total) {
                std::lock_guard<std::mutex> lock(progress.mutex);
                progress.bytes_downloaded = current;
                progress.bytes_total = total;
            },
            error)) {
        fail(error);
        return;
    }
    set_state(ModuleInstallState::Installing);
    // Pinned, not "whatever's latest": rvc-python pins omegaconf==2.0.6,
    // whose metadata pip >=24.1 rejects outright ("invalid metadata: .*
    // suffix can only be used with == or != operators") — verified this
    // exact failure live, pip 23.3.2 is the last version before that
    // stricter PEP 440 validation shipped and installs it fine.
    if (!detail::run_subprocess_and_wait(python_exe.wstring(), L"get-pip.py \"pip==23.3.2\" --no-warn-script-location",
                                          venv_scripts, error)) {
        fail("Не удалось установить pip: " + error);
        return;
    }
    std::filesystem::remove(get_pip, ec);

    // 4. torch/torchaudio matched to this machine's actual CUDA driver.
    {
        std::lock_guard<std::mutex> lock(progress.mutex);
        progress.file_index = 3;
    }
    set_step("Установка torch (CUDA " + cuda.tag + ")...");
    std::wstring torch_args = L"-m pip install torch==2.5.1+" + std::wstring(cuda.tag.begin(), cuda.tag.end()) +
                               L" torchaudio==2.5.1+" + std::wstring(cuda.tag.begin(), cuda.tag.end()) +
                               L" --index-url https://download.pytorch.org/whl/" +
                               std::wstring(cuda.tag.begin(), cuda.tag.end()) + L" --no-warn-script-location";
    if (!detail::run_subprocess_and_wait(python_exe.wstring(), torch_args, venv_scripts, error)) {
        fail("Не удалось установить torch: " + error);
        return;
    }

    // 5. rvc-python + the small web-server deps — straight from PyPI.
    {
        std::lock_guard<std::mutex> lock(progress.mutex);
        progress.file_index = 4;
    }
    set_step("Установка rvc-python...");
    std::wstring pkgs;
    for (const auto& pkg : manifest.pip_packages) pkgs += L" " + std::wstring(pkg.begin(), pkg.end());
    if (!detail::run_subprocess_and_wait(python_exe.wstring(), L"-m pip install" + pkgs + L" --no-warn-script-location",
                                          venv_scripts, error)) {
        fail("Не удалось установить зависимости: " + error);
        return;
    }

    // 6. The one thing with no upstream source — the actual trained voice.
    if (!manifest.voice_model_url.empty()) {
        {
            std::lock_guard<std::mutex> lock(progress.mutex);
            progress.file_index = 5;
            progress.bytes_downloaded = 0;
            progress.bytes_total = 0;
        }
        set_step("Загрузка голосовой модели...");
        std::filesystem::path voice_zip = tmp_dir / "voice.zip";
        if (!detail::download_file(
                manifest.voice_model_url, voice_zip,
                [&](std::uint64_t current, std::uint64_t total) {
                    std::lock_guard<std::mutex> lock(progress.mutex);
                    progress.bytes_downloaded = current;
                    progress.bytes_total = total;
                },
                error)) {
            fail(error);
            return;
        }
        set_state(ModuleInstallState::Extracting);
        if (!detail::extract_zip_via_shell(voice_zip, manifest.install_dir, error)) {
            fail("Не удалось распаковать голосовую модель: " + error);
            return;
        }
        std::filesystem::remove(voice_zip, ec);
    }

    std::filesystem::remove(tmp_dir, ec);
    set_step("");
}

// Runs on a detached background thread — see install_module_async(). All
// state changes go through the module's ModuleProgress under its mutex so
// GET /api/modules/<name>/progress (polled from the GUI, same pattern as
// /api/twitch/auth-status) always sees a consistent snapshot.
inline void run_module_install(ModuleManifest manifest) {
    auto& progress = module_progress(manifest.name);

    SetStateFn set_state = [&](ModuleInstallState s) {
        std::lock_guard<std::mutex> lock(progress.mutex);
        progress.state = s;
    };
    bool failed = false;
    FailFn fail = [&](const std::string& msg) {
        std::lock_guard<std::mutex> lock(progress.mutex);
        progress.state = ModuleInstallState::Failed;
        progress.error = msg;
        failed = true;
        CROW_LOG_ERROR << "Установка модуля " << manifest.name << " не удалась: " << msg;
    };

    {
        std::lock_guard<std::mutex> lock(progress.mutex);
        progress.state = ModuleInstallState::Downloading;
        progress.file_index = 0;
        progress.file_count = 0;
        progress.bytes_downloaded = 0;
        progress.bytes_total = 0;
        progress.current_step.clear();
        progress.error.clear();
    }

    if (manifest.kind == ModuleInstallKind::Zip) {
        run_zip_install(manifest, progress, set_state, fail);
    } else {
        run_pip_live_install(manifest, progress, set_state, fail);
    }
    if (failed) return;

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
        if (progress.state == ModuleInstallState::Downloading || progress.state == ModuleInstallState::Extracting ||
            progress.state == ModuleInstallState::Installing) {
            return false;  // already running
        }
    }
    std::thread(run_module_install, manifest).detach();
    return true;
}

}  // namespace streamsoft
