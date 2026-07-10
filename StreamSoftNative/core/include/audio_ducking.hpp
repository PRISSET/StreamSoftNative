#pragma once

// Volume ducking: temporarily lowers every other app's audio session volume
// (across every active playback device, not just the default one) while a
// TTS/RVC line is playing, then restores it — the same "duck the game while
// someone talks" behavior OBS/Streamlabs/NVIDIA Broadcast all have,
// implemented directly against Windows' own Core Audio session API
// (IAudioSessionManager2) — no third-party mixer library needed, just the
// Windows SDK headers already available everywhere else in core/.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>

#include <crow/logging.h>

#include <vector>

namespace streamsoft {

// Not thread-safe by design — TtsWorker owns exactly one instance and only
// ever touches it from its own single playback thread (see speak() in
// tts_worker.hpp), same as the MCI alias state it already manages there.
class AudioDucker {
public:
    // No-op if already ducked (consecutive queued lines just extend the
    // ducked window instead of restoring-then-re-ducking between them,
    // which would otherwise cause an audible volume blip on rapid chat).
    void duck(int target_percent) {
        if (ducked_) return;

        IMMDeviceEnumerator* enumerator = nullptr;
        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                       reinterpret_cast<void**>(&enumerator));
        if (FAILED(hr)) {
            CROW_LOG_ERROR << "[duck] CoCreateInstance(MMDeviceEnumerator) failed hr=0x" << std::hex << hr;
            return;
        }

        // All active *render* (playback) endpoints, not just the current
        // default one. An app can be outputting to a device that isn't the
        // system default — a per-app output override (Settings > Sound >
        // App volume and device preferences), a second headset/speaker set,
        // HDMI/monitor audio — and its session only shows up on *that*
        // device's session manager. Querying just GetDefaultAudioEndpoint()
        // silently missed every session on any other active device, which
        // is exactly why some apps never got ducked at all.
        IMMDeviceCollection* devices = nullptr;
        hr = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices);
        if (FAILED(hr)) {
            CROW_LOG_ERROR << "[duck] EnumAudioEndpoints failed hr=0x" << std::hex << hr;
            enumerator->Release();
            return;
        }

        UINT device_count = 0;
        devices->GetCount(&device_count);
        DWORD self_pid = GetCurrentProcessId();

        int total_ducked = 0;
        for (UINT d = 0; d < device_count; ++d) {
            IMMDevice* device = nullptr;
            if (FAILED(devices->Item(d, &device)) || !device) continue;

            IAudioSessionManager2* session_manager = nullptr;
            HRESULT hr_act = device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                                               reinterpret_cast<void**>(&session_manager));
            if (SUCCEEDED(hr_act) && session_manager) {
                IAudioSessionEnumerator* session_enum = nullptr;
                if (SUCCEEDED(session_manager->GetSessionEnumerator(&session_enum)) && session_enum) {
                    int count = 0;
                    session_enum->GetCount(&count);
                    for (int i = 0; i < count; ++i) {
                        IAudioSessionControl* control = nullptr;
                        if (FAILED(session_enum->GetSession(i, &control)) || !control) continue;

                        IAudioSessionControl2* control2 = nullptr;
                        if (SUCCEEDED(control->QueryInterface(__uuidof(IAudioSessionControl2),
                                                               reinterpret_cast<void**>(&control2))) &&
                            control2) {
                            DWORD pid = 0;
                            control2->GetProcessId(&pid);
                            // Skip our own session (don't duck ourselves)
                            // and pid 0 (the system sounds session —
                            // ducking that mutes things like the
                            // volume-change chime, not another app).
                            if (pid != self_pid && pid != 0) {
                                ISimpleAudioVolume* volume = nullptr;
                                HRESULT hr_qi = control->QueryInterface(__uuidof(ISimpleAudioVolume),
                                                                         reinterpret_cast<void**>(&volume));
                                if (SUCCEEDED(hr_qi) && volume) {
                                    float current = 1.0f;
                                    volume->GetMasterVolume(&current);
                                    volume->SetMasterVolume(current * (target_percent / 100.0f), nullptr);
                                    // Ownership of this reference moves into
                                    // saved_ — released in restore(), not
                                    // here, since we need it alive to set
                                    // the level back later.
                                    saved_.push_back({volume, current});
                                    ++total_ducked;
                                }
                            }
                            control2->Release();
                        }
                        control->Release();
                    }
                    session_enum->Release();
                }
                session_manager->Release();
            }
            device->Release();
        }
        CROW_LOG_INFO << "[duck] ducked " << total_ducked << " session(s) across " << device_count << " device(s), target=" << target_percent << "%";

        devices->Release();
        enumerator->Release();
        ducked_ = true;
    }

    void restore() {
        if (!ducked_) return;
        CROW_LOG_INFO << "[duck] restoring " << saved_.size() << " session(s)";
        for (auto& s : saved_) {
            s.volume->SetMasterVolume(s.original_level, nullptr);
            s.volume->Release();
        }
        saved_.clear();
        ducked_ = false;
    }

private:
    struct Saved {
        ISimpleAudioVolume* volume;
        float original_level;
    };
    std::vector<Saved> saved_;
    bool ducked_ = false;
};

}  // namespace streamsoft
