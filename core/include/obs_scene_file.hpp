#pragma once

#include <crow/logging.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>

#include <windows.h>
#include <tlhelp32.h>

namespace streamsoft::obs {

namespace fs = std::filesystem;
using json = nlohmann::json;

inline fs::path obs_config_dir() {
    const char* appdata = std::getenv("APPDATA");
    if (!appdata) return {};
    return fs::path(appdata) / "obs-studio";
}

inline bool is_obs_running() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    bool found = false;
    if (Process32FirstW(snap, &entry)) {
        do {
            std::wstring name(entry.szExeFile);
            for (auto& c : name) c = towlower(c);
            if (name == L"obs64.exe" || name == L"obs32.exe" || name == L"obs.exe") {
                found = true;
                break;
            }
        } while (Process32NextW(snap, &entry));
    }
    CloseHandle(snap);
    return found;
}

inline std::string generate_uuid() {
    static std::mt19937_64 gen(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, 15);
    const char* hex = "0123456789abcdef";
    auto append_hex = [&](std::string& s, int n) {
        for (int i = 0; i < n; i++) s += hex[dist(gen)];
    };
    std::string s;
    append_hex(s, 8);
    s += '-';
    append_hex(s, 4);
    s += '-';
    s += '4';
    append_hex(s, 3);
    s += '-';
    s += hex[8 + (dist(gen) % 4)];
    append_hex(s, 3);
    s += '-';
    append_hex(s, 12);
    return s;
}

inline fs::path find_active_scene_collection() {
    fs::path scenes_dir = obs_config_dir() / "basic" / "scenes";
    if (!fs::exists(scenes_dir)) return {};

    fs::path best;
    fs::file_time_type best_time{};
    for (const auto& entry : fs::directory_iterator(scenes_dir)) {
        if (entry.path().extension() != ".json") continue;
        auto t = fs::last_write_time(entry);
        if (best.empty() || t > best_time) {
            best = entry.path();
            best_time = t;
        }
    }
    return best;
}

inline std::string get_str(const json& j, const char* key, const std::string& def = "") {
    if (!j.is_object()) return def;
    auto it = j.find(key);
    if (it == j.end() || !it->is_string()) return def;
    return it->get<std::string>();
}

inline int get_int(const json& j, const char* key, int def = 0) {
    if (!j.is_object()) return def;
    auto it = j.find(key);
    if (it == j.end() || !it->is_number()) return def;
    return it->get<int>();
}

struct SceneFileResult {
    bool ok = false;
    std::string error;
    std::string collection_name;
};

inline json make_browser_source(const std::string& name, const std::string& url, int width, int height) {
    json src;
    src["name"] = name;
    src["uuid"] = generate_uuid();
    src["id"] = "browser_source";
    src["versioned_id"] = "browser_source";
    src["settings"] = {
        {"url", url}, {"width", width}, {"height", height}, {"fps_custom", false}, {"restart_when_active", true}};
    src["mixers"] = 255;
    src["sync"] = 0;
    src["flags"] = 0;
    src["volume"] = 1.0;
    src["balance"] = 0.5;
    src["enabled"] = true;
    src["muted"] = false;
    src["push-to-mute"] = false;
    src["push-to-mute-delay"] = 0;
    src["push-to-talk"] = false;
    src["push-to-talk-delay"] = 0;
    src["hotkeys"] = json::object();
    src["deinterlace_mode"] = 0;
    src["deinterlace_field_order"] = 0;
    src["monitoring_type"] = 0;
    src["private_settings"] = json::object();
    return src;
}

inline json make_scene_item(const std::string& name, const std::string& source_uuid, int item_id, int width,
                             int height) {
    json item;
    item["name"] = name;
    item["source_uuid"] = source_uuid;
    item["visible"] = true;
    item["locked"] = false;
    item["rot"] = 0.0;
    item["scale_ref"] = {{"x", static_cast<double>(width)}, {"y", static_cast<double>(height)}};
    item["align"] = 5;
    item["bounds_type"] = 0;
    item["bounds_align"] = 0;
    item["bounds_crop"] = false;
    item["crop_left"] = 0;
    item["crop_top"] = 0;
    item["crop_right"] = 0;
    item["crop_bottom"] = 0;
    item["id"] = item_id;
    item["group_item_backup"] = false;
    item["pos"] = {{"x", 0.0}, {"y", 0.0}};
    item["pos_rel"] = {{"x", 0.0}, {"y", 0.0}};
    item["scale"] = {{"x", 1.0}, {"y", 1.0}};
    item["scale_rel"] = {{"x", 1.0}, {"y", 1.0}};
    item["bounds"] = {{"x", 0.0}, {"y", 0.0}};
    item["bounds_rel"] = {{"x", 0.0}, {"y", 0.0}};
    item["scale_filter"] = "disable";
    item["blend_method"] = "default";
    item["blend_type"] = "normal";
    item["show_transition"] = {{"duration", 300}};
    item["hide_transition"] = {{"duration", 300}};
    item["private_settings"] = json::object();
    return item;
}

inline void ensure_one_source(json& root, size_t scene_index, const std::string& name, const std::string& url,
                               int width, int height) {
    for (const auto& src : root["sources"]) {
        if (get_str(src, "id") == "browser_source" && get_str(src, "name") == name) {
            CROW_LOG_INFO << "OBS: источник '" << name << "' уже существует, пропускаю";
            return;
        }
    }

    json new_source = make_browser_source(name, url, width, height);
    std::string uuid = new_source["uuid"];
    root["sources"].push_back(std::move(new_source));

    json& scene_source = root["sources"][scene_index];
    int item_id = get_int(scene_source["settings"], "id_counter", 1);
    scene_source["settings"]["id_counter"] = item_id + 1;

    scene_source["settings"]["items"].push_back(make_scene_item(name, uuid, item_id, width, height));
    CROW_LOG_INFO << "OBS: добавлен источник '" << name << "' в сцену '" << get_str(scene_source, "name") << "'";
}

inline SceneFileResult ensure_browser_sources_via_file(int overlay_port) {
    SceneFileResult result;

    if (is_obs_running()) {
        result.error =
            "OBS сейчас запущен. Закрой OBS и попробуй снова — изменения на лету не подхватятся, а при "
            "выходе OBS перезапишет файл своими же данными.";
        return result;
    }

    fs::path scene_file = find_active_scene_collection();
    if (scene_file.empty()) {
        result.error = "Не найдена коллекция сцен OBS (%APPDATA%/obs-studio/basic/scenes/). "
                        "Запусти OBS хотя бы один раз, создай сцену, и попробуй снова.";
        return result;
    }

    std::error_code ec;
    auto backup_path = scene_file;
    backup_path += ".streamsoft-backup";
    fs::copy_file(scene_file, backup_path, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        result.error = "Не удалось сделать резервную копию файла сцены — прерываю, ничего не трогаю: " + ec.message();
        return result;
    }

    json root;
    try {
        std::ifstream in(scene_file, std::ios::binary);
        in >> root;
    } catch (const std::exception& e) {
        result.error = std::string("Не удалось прочитать файл сцены OBS (повреждён JSON?): ") + e.what();
        return result;
    }

    if (!root.contains("sources") || !root.contains("current_program_scene")) {
        result.error = "Файл сцены OBS не похож на ожидаемый формат — ничего не меняю.";
        return result;
    }

    std::string current_scene_name = get_str(root, "current_program_scene", get_str(root, "current_scene"));
    size_t scene_index = 0;
    bool found_scene = false;
    for (size_t i = 0; i < root["sources"].size(); ++i) {
        if (get_str(root["sources"][i], "id") == "scene" && get_str(root["sources"][i], "name") == current_scene_name) {
            scene_index = i;
            found_scene = true;
            break;
        }
    }
    if (!found_scene) {
        result.error = "Не нашёл текущую сцену ('" + current_scene_name + "') в файле — ничего не меняю.";
        return result;
    }
    if (!root["sources"][scene_index]["settings"].contains("items")) {
        root["sources"][scene_index]["settings"]["items"] = json::array();
    }

    std::string base = "http://127.0.0.1:" + std::to_string(overlay_port);

    ensure_one_source(root, scene_index, "StreamSoft Chat", base + "/chat", 940, 960);
    ensure_one_source(root, scene_index, "StreamSoft Alerts", base + "/events", 820, 900);
    ensure_one_source(root, scene_index, "StreamSoft Poll", base + "/poll", 640, 680);
    ensure_one_source(root, scene_index, "StreamSoft Now Playing", base + "/nowplaying", 480, 270);

    try {
        std::ofstream out(scene_file, std::ios::binary | std::ios::trunc);
        out << root.dump(1, ' ');
    } catch (const std::exception& e) {
        result.error = std::string("Не удалось сохранить файл сцены (бэкап цел: ") + backup_path.string() +
                        "): " + e.what();
        return result;
    }

    result.ok = true;
    result.collection_name = get_str(root, "name", scene_file.stem().string());
    return result;
}

}
