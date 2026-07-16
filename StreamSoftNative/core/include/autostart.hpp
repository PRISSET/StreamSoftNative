#pragma once

#include "app_paths.hpp"

#include <windows.h>

#include <string>

namespace streamsoft {

// Mirrors the HKCU Run-key entry the installer writes for the "Запускать
// StreamSoft при входе в Windows" task (see [Registry] in streamsoft.iss) —
// same key/value name, so toggling this at runtime and toggling the
// installer task both agree on the same piece of state.
inline const wchar_t* kAutostartRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
inline const wchar_t* kAutostartValueName = L"StreamSoft";

// Separate from the Run key itself: Task Manager's Startup tab tracks its own
// "user approved" flag here, independent of whether the Run value exists.
// If a name was ever toggled off in Task Manager (or is left over from a
// previous install/test), re-writing the Run key alone does NOT clear this
// flag — Task Manager keeps showing "Disabled" even though the app really is
// set to launch. First DWORD of the 12-byte blob is the state: 02 = enabled,
// 03 = disabled by user; the trailing 8 bytes are just an informational
// FILETIME Explorer stamps on the toggle, not something anything reads back.
inline const wchar_t* kAutostartApprovedKey =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";

inline void autostart_clear_approved_disabled_flag() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kAutostartApprovedKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
    BYTE enabled_blob[12] = {0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    RegSetValueExW(key, kAutostartValueName, 0, REG_BINARY, enabled_blob, sizeof(enabled_blob));
    RegCloseKey(key);
}

inline bool autostart_is_enabled() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kAutostartRunKey, 0, KEY_READ, &key) != ERROR_SUCCESS) return false;
    DWORD type = 0;
    LSTATUS st = RegQueryValueExW(key, kAutostartValueName, nullptr, &type, nullptr, nullptr);
    RegCloseKey(key);
    return st == ERROR_SUCCESS && type == REG_SZ;
}

inline bool autostart_set_enabled(bool enabled) {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kAutostartRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return false;

    bool ok;
    if (enabled) {
        wchar_t buf[MAX_PATH];
        DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
        if (len == 0 || len >= MAX_PATH) {
            RegCloseKey(key);
            return false;
        }
        std::wstring quoted = L"\"" + std::wstring(buf, len) + L"\"";
        LSTATUS st = RegSetValueExW(key, kAutostartValueName, 0, REG_SZ,
                                     reinterpret_cast<const BYTE*>(quoted.c_str()),
                                     static_cast<DWORD>((quoted.size() + 1) * sizeof(wchar_t)));
        ok = st == ERROR_SUCCESS;
        if (ok) autostart_clear_approved_disabled_flag();
    } else {
        LSTATUS st = RegDeleteValueW(key, kAutostartValueName);
        ok = st == ERROR_SUCCESS || st == ERROR_FILE_NOT_FOUND;
    }
    RegCloseKey(key);
    return ok;
}

}
