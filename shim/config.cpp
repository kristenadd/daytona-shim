#include "config.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>

#include "log.hpp"

namespace shim {

std::string config_path() {
    char path[MAX_PATH];
    const DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    char* slash = std::strrchr(path, '\\');
    if (slash == nullptr) return {};
    std::strcpy(slash + 1, "shim.ini");
    return path;
}

namespace {

std::string get(const std::string& ini, const char* section, const char* key) {
    char buf[256] = {};
    GetPrivateProfileStringA(section, key, "", buf, sizeof buf, ini.c_str());
    return buf;
}

void read_int(const std::string& ini, const char* section, const char* key, int& out) {
    const std::string v = get(ini, section, key);
    if (!v.empty()) out = std::atoi(v.c_str());
}
void read_bool(const std::string& ini, const char* section, const char* key, bool& out) {
    int v = out ? 1 : 0;
    read_int(ini, section, key, v);
    out = v != 0;
}
void read_float(const std::string& ini, const char* section, const char* key, float& out) {
    const std::string v = get(ini, section, key);
    if (!v.empty()) out = static_cast<float>(std::atof(v.c_str()));
}
void read_percent(const std::string& ini, const char* section, const char* key, float& out) {
    int v = static_cast<int>(out * 100.0f + 0.5f);
    read_int(ini, section, key, v);
    out = std::clamp(v, 0, 200) / 100.0f;
}

const char* const kWheelButtonKeys[12] = {"Start", "Coin",  "ShiftUp", "ShiftDown", "View1", "View2",
                                          "View3", "View4", "Gear1",   "Gear2",     "Gear3", "Gear4"};

}  // namespace

Config load_config(const std::string& ini) {
    Config c;
    if (ini.empty() || GetFileAttributesA(ini.c_str()) == INVALID_FILE_ATTRIBUTES) {
        logf("config: no shim.ini; defaults");
        return c;
    }
    read_bool(ini, "Input", "SDL", c.sdl);
    for (int i = 0; i < kNumActions; ++i) {
        const std::string v = get(ini, "Pad", kActionKeys[i]);
        if (!v.empty()) c.pad[i] = parse_pad_binding(v.c_str());
    }
    c.wheel_name = get(ini, "Wheel", "Name");
    read_int(ini, "Wheel", "SteeringAxis", c.steering_axis);
    read_int(ini, "Wheel", "GasAxis", c.gas_axis);
    read_int(ini, "Wheel", "BrakeAxis", c.brake_axis);
    read_float(ini, "Wheel", "SteeringScale", c.steering_scale);
    const std::string rest = get(ini, "Wheel", "PedalRest");
    if (rest == "high") c.pedal_rest = 1;
    else if (rest == "low") c.pedal_rest = -1;
    for (int i = 0; i < 12; ++i) read_int(ini, "Wheel", kWheelButtonKeys[i], c.buttons[i]);
    read_bool(ini, "ForceFeedback", "Enabled", c.ffb);
    read_percent(ini, "ForceFeedback", "Strength", c.ffb_strength);
    read_bool(ini, "ForceFeedback", "Invert", c.ffb_invert);
    read_percent(ini, "ForceFeedback", "Rumble", c.rumble);
    logf("config: shim.ini read (SDL %d, force feedback %d)", c.sdl, c.ffb);
    return c;
}

bool save_config(const Config& c, const std::string& ini) {
    bool ok = true;
    const auto put = [&](const char* section, const char* key, const std::string& value) {
        ok = WritePrivateProfileStringA(section, key, value.c_str(), ini.c_str()) && ok;
    };
    const auto percent = [](float v) { return std::to_string(static_cast<int>(v * 100.0f + 0.5f)); };
    put("Input", "SDL", c.sdl ? "1" : "0");
    for (int i = 0; i < kNumActions; ++i) put("Pad", kActionKeys[i], pad_binding_name(c.pad[i]));
    put("Wheel", "Name", c.wheel_name);
    put("Wheel", "SteeringAxis", std::to_string(c.steering_axis));
    put("Wheel", "GasAxis", std::to_string(c.gas_axis));
    put("Wheel", "BrakeAxis", std::to_string(c.brake_axis));
    char scale[32];
    std::snprintf(scale, sizeof scale, "%.2f", c.steering_scale);
    put("Wheel", "SteeringScale", scale);
    put("Wheel", "PedalRest", c.pedal_rest > 0 ? "high" : c.pedal_rest < 0 ? "low" : "auto");
    for (int i = 0; i < 12; ++i) put("Wheel", kWheelButtonKeys[i], std::to_string(c.buttons[i]));
    put("ForceFeedback", "Enabled", c.ffb ? "1" : "0");
    put("ForceFeedback", "Strength", percent(c.ffb_strength));
    put("ForceFeedback", "Rumble", percent(c.rumble));
    put("ForceFeedback", "Invert", c.ffb_invert ? "1" : "0");
    return ok;
}

const Config& config() {
    static const Config c = load_config(config_path());
    return c;
}

}  // namespace shim
