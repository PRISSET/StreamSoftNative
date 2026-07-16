#pragma once

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <atomic>
#include <cwctype>
#include <string>

namespace streamsoft {

enum class ActiveGame { Cs2, Dota2 };

// Decides which game's card the shared Faceit/Dota OBS widget shows, purely
// from "which of these two known process names is currently running" — not
// from GSI, since GSI only fires once inside an actual match and we want the
// switch to happen the moment the user launches the game (menu, queue,
// picking a hero, etc). Never reports "neither" once a game has been seen —
// the widget always has a sensible resting identity — and if both processes
// are somehow running at once, keeps whatever was last decided instead of
// flip-flopping every poll.
class ProcessWatcher {
public:
    ActiveGame current() const { return current_.load(); }

    void tick() {
        bool cs2 = is_process_running(L"cs2.exe");
        bool dota2 = is_process_running(L"dota2.exe");
        if (dota2 && !cs2) {
            current_.store(ActiveGame::Dota2);
        } else if (cs2 && !dota2) {
            current_.store(ActiveGame::Cs2);
        }
        // both or neither running: leave current_ as-is
    }

private:
    static bool is_process_running(const wchar_t* target_name) {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return false;

        PROCESSENTRY32W entry;
        entry.dwSize = sizeof(entry);
        bool found = false;
        if (Process32FirstW(snapshot, &entry)) {
            do {
                if (iequals(entry.szExeFile, target_name)) {
                    found = true;
                    break;
                }
            } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
        return found;
    }

    static bool iequals(const wchar_t* a, const wchar_t* b) {
        while (*a && *b) {
            if (std::towlower(*a) != std::towlower(*b)) return false;
            ++a;
            ++b;
        }
        return *a == *b;
    }

    std::atomic<ActiveGame> current_{ActiveGame::Cs2};
};

}
