#pragma once

#include "app_paths.hpp"

#include <crow/logging.h>

#include <filesystem>
#include <string>
#include <vector>

#include <windows.h>

namespace streamsoft::rvc {

namespace fs = std::filesystem;

inline fs::path adapter_dir() { return resolve_resource_dir("adapters/rvc", STREAMSOFT_RVC_ADAPTER_DIR); }
inline fs::path adapter_python() { return adapter_dir() / "venv" / "Scripts" / "python.exe"; }

inline bool is_installed() {
    return fs::exists(adapter_python()) && fs::exists(adapter_dir() / "server.py");
}

struct AdapterProcess {
    PROCESS_INFORMATION pi{};
    HANDLE job = nullptr;
    bool running = false;
};

inline AdapterProcess start(int port) {
    AdapterProcess result;
    if (!is_installed()) {
        CROW_LOG_INFO << "RVC-адаптер не найден локально (adapters/rvc/venv отсутствует) — смена голоса отключена";
        return result;
    }

    std::wstring cmd = L"\"" + adapter_python().wstring() + L"\" -m uvicorn server:app --host 127.0.0.1 --port " +
                        std::to_wstring(port);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    std::wstring cwd = adapter_dir().wstring();
    std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
    cmd_buf.push_back(L'\0');

    BOOL ok = CreateProcessW(nullptr, cmd_buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
                              nullptr, cwd.c_str(), &si, &pi);
    if (!ok) {
        CROW_LOG_ERROR << "Не удалось запустить RVC-адаптер (код " << GetLastError() << ")";
        return result;
    }

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info));
        AssignProcessToJobObject(job, pi.hProcess);
    }
    ResumeThread(pi.hThread);

    CROW_LOG_INFO << "Запускаю RVC-адаптер в фоне на порту " << port;
    result.pi = pi;
    result.job = job;
    result.running = true;
    return result;
}

inline void stop(AdapterProcess& proc) {
    if (!proc.running) return;
    CROW_LOG_INFO << "Останавливаю RVC-адаптер";
    TerminateProcess(proc.pi.hProcess, 0);
    WaitForSingleObject(proc.pi.hProcess, 5000);
    CloseHandle(proc.pi.hProcess);
    CloseHandle(proc.pi.hThread);
    if (proc.job) CloseHandle(proc.job);
    proc.running = false;
}

}
