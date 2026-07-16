#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace streamsoft::faceit {

// A shared FACEIT Data API key so a viewer of this app doesn't have to
// register their own developer app just to see a stats widget — users can
// still set their own key in Faceit settings (e.g. if this shared one ever
// gets rate-limited), which takes priority when present.
//
// XOR-obfuscated at rest so it doesn't sit as a bare, grep-able API key in
// a public GitHub repo (secret-scanners, casual scraping) — this is NOT
// real protection. The app has to reconstruct the plain key in memory to
// actually call the API with it, so anyone willing to read the compiled
// binary's memory or disassemble it can recover it; obfuscation only stops
// the trivial "ctrl+F the source" case.
namespace detail {
inline constexpr std::array<uint8_t, 36> kObfuscatedKey = {
    53,  70,  17, 4,  2,   12, 97, 95, 75, 64, 47, 4,  22, 68, 66,  1, 119, 4,
    78,  7,   12, 77, 52,  76, 86, 83, 1,  6,  14, 64, 24, 106, 70, 69, 1,  87};
inline constexpr const char* kPad = "StreamSoftNativeFaceitPad2026!!";
}  // namespace detail

inline std::string shared_api_key() {
    std::string out;
    out.reserve(detail::kObfuscatedKey.size());
    size_t pad_len = std::char_traits<char>::length(detail::kPad);
    for (size_t i = 0; i < detail::kObfuscatedKey.size(); ++i) {
        out.push_back(static_cast<char>(detail::kObfuscatedKey[i] ^ static_cast<uint8_t>(detail::kPad[i % pad_len])));
    }
    return out;
}

}  // namespace streamsoft::faceit
