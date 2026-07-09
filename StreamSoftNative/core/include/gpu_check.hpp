#pragma once

// GPU/CUDA capability check for the RVC module — DXGI-only, no NVML/CUDA
// runtime dependency (that would mean shipping/loading Nvidia's own DLLs
// just to answer "is there a compatible card"). DXGI ships with Windows
// itself (part of the D3D stack), so this needs nothing beyond linking
// dxgi.lib. Same "native, no heavy SDK" approach as twitch_auth.hpp/
// discord_presence.hpp use for their own protocols.

// See discord_presence.hpp's identical guard — dxgi1_2.h pulls in
// <windows.h> transitively, and Asio (via crow.h elsewhere in this TU)
// needs winsock2.h, not the legacy winsock.h a bare windows.h include
// pulls in.
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
    std::string reason;  // human-readable, only set when !cuda_capable
};

struct DiskSpaceResult {
    bool ok = false;
    std::uint64_t free_mb = 0;
    std::string reason;
};

// Nvidia's PCI vendor ID — every CUDA-capable card enumerates under this,
// regardless of generation/driver version. We don't attempt to check
// compute capability or driver version here: DXGI has no notion of CUDA
// itself, this is just "is there an Nvidia GPU at all" as a fast, dependency-
// free first filter. The installer step (module_installer.hpp) still has to
// handle "driver too old for this torch build" as an install-time failure.
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
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;  // WARP/basic render driver

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

    // DXGI adapter descriptions are marketing names ("NVIDIA GeForce RTX
    // 4090") — always ASCII in practice, so a narrowing cast per char is
    // safe and avoids pulling in <codecvt>/WideCharToMultiByte for this.
    std::wstring wname(best_desc.Description);
    result.gpu_name.reserve(wname.size());
    for (wchar_t wc : wname) result.gpu_name.push_back(static_cast<char>(wc));
    result.vram_mb = static_cast<std::uint64_t>(best_desc.DedicatedVideoMemory) / (1024 * 1024);
    result.cuda_capable = true;
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

} // namespace streamsoft
