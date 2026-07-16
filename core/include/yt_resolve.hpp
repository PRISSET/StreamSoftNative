#pragma once

#include "app_paths.hpp"

#include <crow/logging.h>

#include <filesystem>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>

namespace streamsoft::ytresolve {

inline std::string ytdlp_path() { return resolve_resource_file("tools/yt-dlp.exe", STREAMSOFT_YTDLP_PATH); }

inline bool is_available() { return std::filesystem::exists(ytdlp_path()); }

inline std::optional<std::string> resolve_direct_audio_url(const std::string& video_url, DWORD timeout_ms = 9000) {
    if (!is_available()) return std::nullopt;

    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};

    HANDLE read_pipe = nullptr;
    HANDLE write_pipe = nullptr;
    if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0)) return std::nullopt;
    SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

    HANDLE nul_in = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ, &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE nul_err = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    std::wstring exe = std::filesystem::path(ytdlp_path()).wstring();
    std::wstring url_w(video_url.begin(), video_url.end());
    std::wstring cmd = L"\"" + exe +
                        L"\" -f bestaudio --get-url --no-playlist --no-warnings --socket-timeout 6 \"" + url_w + L"\"";

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nul_in;
    si.hStdOutput = write_pipe;
    si.hStdError = nul_err;

    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
    cmd_buf.push_back(L'\0');

    BOOL ok = CreateProcessW(nullptr, cmd_buf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(write_pipe);
    if (nul_in) CloseHandle(nul_in);
    if (nul_err) CloseHandle(nul_err);
    if (!ok) {
        CloseHandle(read_pipe);
        return std::nullopt;
    }

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info));
        AssignProcessToJobObject(job, pi.hProcess);
    }

    std::string output;
    std::thread reader([&] {
        char buf[4096];
        DWORD read_bytes = 0;
        while (ReadFile(read_pipe, buf, sizeof(buf), &read_bytes, nullptr) && read_bytes > 0) {
            output.append(buf, read_bytes);
        }
    });

    DWORD wait_result = WaitForSingleObject(pi.hProcess, timeout_ms);
    if (wait_result != WAIT_OBJECT_0) {
        CROW_LOG_WARNING << "yt-dlp: резолв ссылки завис дольше " << timeout_ms << " мс, убиваю процесс";
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 2000);
    }
    reader.join();

    CloseHandle(read_pipe);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (job) CloseHandle(job);

    size_t nl = output.find_first_of("\r\n");
    if (nl != std::string::npos) output = output.substr(0, nl);
    if (output.rfind("http", 0) != 0) return std::nullopt;
    return output;
}

}
