#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

namespace streamsoft {

struct GpuCheckResult {
    bool cuda_capable = false;
    std::string gpu_name;
    std::uint64_t vram_mb = 0;
    std::string reason;
};

struct DiskSpaceResult {
    bool ok = false;
    std::uint64_t free_mb = 0;
    std::string reason;
};

inline constexpr std::uint32_t kNvidiaVendorId = 0x10DE;

inline GpuCheckResult check_gpu_for_rvc() {
    using Microsoft::WRL::ComPtr;
    GpuCheckResult result;

    ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        result.reason = "Не удалось получить доступ к видеоподсистеме Windows (DXGI)";
        return result;
    }

    ComPtr<IDXGIAdapter1> best_adapter;
    DXGI_ADAPTER_DESC1 best_desc{};
    bool found_any_gpu = false;

    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) break;

        DXGI_ADAPTER_DESC1 desc{};
        if (FAILED(adapter->GetDesc1(&desc))) continue;
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;

        found_any_gpu = true;

        if (desc.VendorId != kNvidiaVendorId) continue;
        if (!best_adapter || desc.DedicatedVideoMemory > best_desc.DedicatedVideoMemory) {
            best_adapter = adapter;
            best_desc = desc;
        }
    }

    if (!best_adapter) {
        result.reason = found_any_gpu
            ? "Нужна видеокарта Nvidia с поддержкой CUDA — у тебя видеокарта другого производителя"
            : "Нужна видеокарта Nvidia с поддержкой CUDA — у тебя интегрированная графика";
        return result;
    }

    std::wstring wname(best_desc.Description);
    result.gpu_name.reserve(wname.size());
    for (wchar_t wc : wname) result.gpu_name.push_back(static_cast<char>(wc));
    result.vram_mb = static_cast<std::uint64_t>(best_desc.DedicatedVideoMemory) / (1024 * 1024);
    result.cuda_capable = true;
    return result;
}

struct CudaWheelTag {
    bool available = false;
    std::string tag;
    int driver_cuda_version = 0;
    std::string reason;
};

inline std::string pick_cuda_wheel_tag(int driver_cuda_version) {
    if (driver_cuda_version >= 12010) return "cu121";
    if (driver_cuda_version >= 11080) return "cu118";
    return "";
}

inline CudaWheelTag detect_cuda_wheel_tag() {
    CudaWheelTag result;

    HMODULE nvml = LoadLibraryW(L"nvml.dll");
    if (!nvml) {
        result.reason = "Не удалось загрузить nvml.dll — обнови драйвер Nvidia";
        return result;
    }

    using InitFn = int(__cdecl*)();
    using GetVersionFn = int(__cdecl*)(int*);
    using ShutdownFn = int(__cdecl*)();

    auto init = reinterpret_cast<InitFn>(GetProcAddress(nvml, "nvmlInit_v2"));
    auto get_version = reinterpret_cast<GetVersionFn>(GetProcAddress(nvml, "nvmlSystemGetCudaDriverVersion_v2"));
    auto shutdown = reinterpret_cast<ShutdownFn>(GetProcAddress(nvml, "nvmlShutdown"));

    if (!init || !get_version || !shutdown) {
        FreeLibrary(nvml);
        result.reason = "nvml.dll не содержит ожидаемых функций";
        return result;
    }

    if (init() == 0) {
        int version = 0;
        if (get_version(&version) == 0) {
            result.driver_cuda_version = version;
            result.tag = pick_cuda_wheel_tag(version);
            if (result.tag.empty()) {
                result.reason = "Драйвер Nvidia слишком старый для CUDA (нужен минимум CUDA 11.8) — обнови драйвер";
            } else {
                result.available = true;
            }
        } else {
            result.reason = "Не удалось определить версию CUDA драйвера";
        }
        shutdown();
    } else {
        result.reason = "Не удалось инициализировать NVML";
    }

    FreeLibrary(nvml);
    return result;
}

inline DiskSpaceResult check_disk_space(const std::filesystem::path& target_dir, std::uint64_t required_mb) {
    DiskSpaceResult result;
    std::error_code ec;

    std::filesystem::path probe_dir = target_dir;
    while (!probe_dir.empty() && !std::filesystem::exists(probe_dir, ec)) {
        auto parent = probe_dir.parent_path();
        if (parent == probe_dir) break;
        probe_dir = parent;
    }

    auto space = std::filesystem::space(probe_dir, ec);
    if (ec) {
        result.reason = "Не удалось проверить свободное место на диске";
        return result;
    }

    result.free_mb = static_cast<std::uint64_t>(space.available) / (1024 * 1024);
    result.ok = result.free_mb >= required_mb;
    if (!result.ok) {
        result.reason = "Недостаточно места на диске: нужно " + std::to_string(required_mb) +
                         " МБ, свободно " + std::to_string(result.free_mb) + " МБ";
    }
    return result;
}

}
