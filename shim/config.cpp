#include "config.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "log.hpp"

namespace shim {
namespace {

std::string ini_path() {
    char path[MAX_PATH];
    const DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    char* slash = std::strrchr(path, '\\');
    if (slash == nullptr) return {};
    std::strcpy(slash + 1, "shim.ini");
    return path;
}

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

Config load() {
    Config c;
    const std::string ini = ini_path();
    if (ini.empty() || GetFileAttributesA(ini.c_str()) == INVALID_FILE_ATTRIBUTES) {
        logf("config: no shim.ini; defaults");
        return c;
    }
    read_bool(ini, "Input", "SDL", c.sdl);
    c.wheel_name = get(ini, "Wheel", "Name");
    read_int(ini, "Wheel", "SteeringAxis", c.steering_axis);
    read_int(ini, "Wheel", "GasAxis", c.gas_axis);
    read_int(ini, "Wheel", "BrakeAxis", c.brake_axis);
    read_float(ini, "Wheel", "SteeringScale", c.steering_scale);
    const std::string rest = get(ini, "Wheel", "PedalRest");
    if (rest == "high") c.pedal_rest = 1;
    else if (rest == "low") c.pedal_rest = -1;
    static const char* const names[12] = {"Start", "Coin", "ShiftUp", "ShiftDown", "View1", "View2",
                                          "View3", "View4", "Gear1", "Gear2", "Gear3", "Gear4"};
    for (int i = 0; i < 12; ++i) read_int(ini, "Wheel", names[i], c.buttons[i]);
    read_bool(ini, "ForceFeedback", "Enabled", c.ffb);
    read_percent(ini, "ForceFeedback", "Strength", c.ffb_strength);
    read_bool(ini, "ForceFeedback", "Invert", c.ffb_invert);
    read_percent(ini, "ForceFeedback", "Rumble", c.rumble);
    logf("config: shim.ini read (SDL %d, force feedback %d)", c.sdl, c.ffb);
    return c;
}

}  // namespace

const Config& config() {
    static const Config c = load();
    return c;
}

}  // namespace shim
