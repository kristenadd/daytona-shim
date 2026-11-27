// The full-screen launcher, for the Steam Deck (and big screens): the title
// with START and OPTIONS, and one list of the game's settings. Driven by a
// pad (d-pad or left stick; A / Cross selects, B / Circle goes back) or the
// keyboard (arrows, Enter, Esc or Backspace).
//
// Drawn with SDL3's renderer at 1280x800 (the Deck's screen), letterboxed on
// other screens. Text is stb_truetype over two OFL fonts and the logo is
// launcher/logo.png, all built into the executable as resources.
//
// The window is borderless full screen at the display's own size, and the
// picture is laid out at 1280x800: on the Deck that fills the screen 1:1,
// elsewhere it is letterboxed.
//
// For testing: DAYTONA_LAUNCHER_SHOT=<file.bmp> opens a window instead and
// keeps saving the latest frame, DAYTONA_LAUNCHER_GLYPHS=ps|xbox|nintendo
// shows that pad's prompts, and --screen=options opens on the list.
#include <windows.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wsign-compare"
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STB_IMAGE_STATIC
#include "stb_image.h"
#pragma clang diagnostic pop

#include "settings.hpp"

namespace launcher {
namespace {

constexpr float kW = 1280, kH = 800;
constexpr float kItalic = 0.21f;  // the lean of headings and menu items

// --- Colours ------------------------------------------------------------------

SDL_FColor rgb(int hex, float a = 1.0f) {
    return {((hex >> 16) & 255) / 255.0f, ((hex >> 8) & 255) / 255.0f, (hex & 255) / 255.0f, a};
}
const SDL_FColor kWhite = rgb(0xFFFFFF), kBlack = rgb(0x000000), kYellow = rgb(0xFFD400), kOrange = rgb(0xF7931E),
                 kRed = rgb(0xE3262F), kSkyTop = rgb(0x0E6FD6), kSkyBottom = rgb(0x041D4A), kGreen = rgb(0x26A54A),
                 kPsBlue = rgb(0x6E8FE6), kPsRed = rgb(0xE5546B);

SDL_FColor with_alpha(SDL_FColor c, float a) { return {c.r, c.g, c.b, c.a * a}; }

// --- Drawing ------------------------------------------------------------------

SDL_Renderer* ren = nullptr;

SDL_Vertex vtx(float x, float y, SDL_FColor c, float u = 0, float v = 0) { return {{x, y}, c, {u, v}}; }

// A quad from four corners (clockwise from top left), each with its colour.
void quad(SDL_FPoint p0, SDL_FPoint p1, SDL_FPoint p2, SDL_FPoint p3, SDL_FColor c0, SDL_FColor c1, SDL_FColor c2,
          SDL_FColor c3) {
    const SDL_Vertex v[4] = {vtx(p0.x, p0.y, c0), vtx(p1.x, p1.y, c1), vtx(p2.x, p2.y, c2), vtx(p3.x, p3.y, c3)};
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(ren, nullptr, v, 4, idx, 6);
}

// A parallelogram leaning right by `slant` pixels over its height; `left`
// and `right` colour it in a horizontal gradient.
void para(float x, float y, float w, float h, float slant, SDL_FColor left, SDL_FColor right) {
    quad({x + slant, y}, {x + w + slant, y}, {x + w, y + h}, {x, y + h}, left, right, right, left);
}
void para(float x, float y, float w, float h, float slant, SDL_FColor c) { para(x, y, w, h, slant, c, c); }

void vgradient(float x, float y, float w, float h, SDL_FColor top, SDL_FColor bottom) {
    quad({x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}, top, top, bottom, bottom);
}

void fan(float cx, float cy, float r, SDL_FColor c, int n = 40) {
    std::vector<SDL_Vertex> v;
    std::vector<int> idx;
    v.push_back(vtx(cx, cy, c));
    for (int i = 0; i <= n; ++i) {
        const float a = 6.2831853f * i / n;
        v.push_back(vtx(cx + r * std::cos(a), cy + r * std::sin(a), c));
        if (i > 0) {
            idx.push_back(0);
            idx.push_back(i);
            idx.push_back(i + 1);
        }
    }
    SDL_RenderGeometry(ren, nullptr, v.data(), static_cast<int>(v.size()), idx.data(), static_cast<int>(idx.size()));
}

void ring(float cx, float cy, float r, float thick, SDL_FColor c, int n = 40) {
    std::vector<SDL_Vertex> v;
    std::vector<int> idx;
    for (int i = 0; i <= n; ++i) {
        const float a = 6.2831853f * i / n, ca = std::cos(a), sa = std::sin(a);
        v.push_back(vtx(cx + r * ca, cy + r * sa, c));
        v.push_back(vtx(cx + (r - thick) * ca, cy + (r - thick) * sa, c));
        if (i > 0) {
            const int b = 2 * i;
            const int q[6] = {b - 2, b - 1, b, b - 1, b + 1, b};
            idx.insert(idx.end(), q, q + 6);
        }
    }
    SDL_RenderGeometry(ren, nullptr, v.data(), static_cast<int>(v.size()), idx.data(), static_cast<int>(idx.size()));
}

void segment(float x0, float y0, float x1, float y1, float thick, SDL_FColor c) {
    const float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
    const float nx = -dy / len * thick / 2, ny = dx / len * thick / 2;
    quad({x0 + nx, y0 + ny}, {x1 + nx, y1 + ny}, {x1 - nx, y1 - ny}, {x0 - nx, y0 - ny}, c, c, c, c);
}

void triangle(SDL_FPoint a, SDL_FPoint b, SDL_FPoint c, SDL_FColor col) {
    const SDL_Vertex v[3] = {vtx(a.x, a.y, col), vtx(b.x, b.y, col), vtx(c.x, c.y, col)};
    SDL_RenderGeometry(ren, nullptr, v, 3, nullptr, 0);
}

// --- Resources ----------------------------------------------------------------

std::vector<unsigned char> resource(int id) {
    HMODULE self = GetModuleHandleW(nullptr);
    HRSRC r = FindResourceW(self, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10));  // RT_RCDATA
    if (r == nullptr) return {};
    HGLOBAL g = LoadResource(self, r);
    const auto* p = static_cast<const unsigned char*>(LockResource(g));
    return std::vector<unsigned char>(p, p + SizeofResource(self, r));
}
enum { kResTitleFont = 2, kResBodyFont = 3, kResLogo = 4 };

// --- Text ---------------------------------------------------------------------

struct Font {
    SDL_Texture* tex = nullptr;
    stbtt_packedchar chars[96] = {};
    int atlas = 0;
    float size = 0, ascent = 0;

    bool load(const std::vector<unsigned char>& ttf, float px) {
        if (ttf.empty()) return false;
        size = px;
        atlas = px > 40 ? 2048 : 1024;
        std::vector<unsigned char> alpha(static_cast<std::size_t>(atlas) * atlas);
        stbtt_pack_context pc;
        if (!stbtt_PackBegin(&pc, alpha.data(), atlas, atlas, 0, 2, nullptr)) return false;
        stbtt_PackSetOversampling(&pc, 2, 2);
        stbtt_PackFontRange(&pc, ttf.data(), 0, px, 32, 96, chars);
        stbtt_PackEnd(&pc);
        stbtt_fontinfo info;
        stbtt_InitFont(&info, ttf.data(), 0);
        int asc = 0, desc = 0, gap = 0;
        stbtt_GetFontVMetrics(&info, &asc, &desc, &gap);
        ascent = asc * stbtt_ScaleForPixelHeight(&info, px);
        std::vector<unsigned char> rgba(alpha.size() * 4);
        for (std::size_t i = 0; i < alpha.size(); ++i) {
            rgba[4 * i] = rgba[4 * i + 1] = rgba[4 * i + 2] = 255;
            rgba[4 * i + 3] = alpha[i];
        }
        tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, atlas, atlas);
        if (tex == nullptr) return false;
        SDL_UpdateTexture(tex, nullptr, rgba.data(), atlas * 4);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
        return true;
    }

    float width(const std::string& s) const {
        float x = 0, y = 0;
        for (unsigned char ch : s) {
            if (ch < 32 || ch >= 128) continue;
            stbtt_aligned_quad q;
            stbtt_GetPackedQuad(chars, atlas, atlas, ch - 32, &x, &y, &q, 0);
        }
        return x;
    }

    // `top` is the top of the line (the baseline is `ascent` below it).
    void draw(const std::string& s, float x, float top, SDL_FColor c, float skew = 0) const {
        if (tex == nullptr) return;
        std::vector<SDL_Vertex> v;
        std::vector<int> idx;
        const float base = top + ascent;
        float px = x, py = base;
        for (unsigned char ch : s) {
            if (ch < 32 || ch >= 128) continue;
            stbtt_aligned_quad q;
            stbtt_GetPackedQuad(chars, atlas, atlas, ch - 32, &px, &py, &q, 0);
            const int b = static_cast<int>(v.size());
            const float k0 = skew * (base - q.y0), k1 = skew * (base - q.y1);
            v.push_back(vtx(q.x0 + k0, q.y0, c, q.s0, q.t0));
            v.push_back(vtx(q.x1 + k0, q.y0, c, q.s1, q.t0));
            v.push_back(vtx(q.x1 + k1, q.y1, c, q.s1, q.t1));
            v.push_back(vtx(q.x0 + k1, q.y1, c, q.s0, q.t1));
            const int q6[6] = {b, b + 1, b + 2, b, b + 2, b + 3};
            idx.insert(idx.end(), q6, q6 + 6);
        }
        if (!v.empty())
            SDL_RenderGeometry(ren, tex, v.data(), static_cast<int>(v.size()), idx.data(), static_cast<int>(idx.size()));
    }

    // With a soft drop shadow, as the game's own captions.
    void shadowed(const std::string& s, float x, float top, SDL_FColor c, float skew = 0, float off = 3) const {
        draw(s, x + off, top + off, rgb(0x000000, 0.55f * c.a), skew);
        draw(s, x, top, c, skew);
    }
};

Font title_font, heading_font, hint_font, row_font, small_font;

// --- Input ----------------------------------------------------------------------

enum class Action { None, Up, Down, Left, Right, Confirm, Back };
enum class Glyphs { Keyboard, Xbox, PlayStation, Nintendo };

struct Input {
    Glyphs glyphs = Glyphs::Keyboard;
    // The left stick, with key-like repeat.
    int stick_dir = 0;  // 0 none, else the Action
    Uint64 stick_next = 0;

    static Glyphs glyphs_for(SDL_Gamepad* pad) {
        switch (SDL_GetGamepadButtonLabel(pad, SDL_GAMEPAD_BUTTON_SOUTH)) {
            case SDL_GAMEPAD_BUTTON_LABEL_CROSS: return Glyphs::PlayStation;
            case SDL_GAMEPAD_BUTTON_LABEL_B: return Glyphs::Nintendo;
            default: return Glyphs::Xbox;
        }
    }

    Action from_event(const SDL_Event& e) {
        switch (e.type) {
            case SDL_EVENT_GAMEPAD_ADDED:
                SDL_OpenGamepad(e.gdevice.which);
                return Action::None;
            case SDL_EVENT_KEY_DOWN:
                glyphs = Glyphs::Keyboard;
                switch (e.key.key) {
                    case SDLK_UP: return Action::Up;
                    case SDLK_DOWN: return Action::Down;
                    case SDLK_LEFT: return Action::Left;
                    case SDLK_RIGHT: return Action::Right;
                    case SDLK_RETURN:
                    case SDLK_KP_ENTER:
                    case SDLK_SPACE: return e.key.repeat ? Action::None : Action::Confirm;
                    case SDLK_ESCAPE:
                    case SDLK_BACKSPACE: return e.key.repeat ? Action::None : Action::Back;
                    default: return Action::None;
                }
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
                if (SDL_Gamepad* pad = SDL_GetGamepadFromID(e.gbutton.which)) glyphs = glyphs_for(pad);
                switch (e.gbutton.button) {
                    case SDL_GAMEPAD_BUTTON_DPAD_UP: return Action::Up;
                    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return Action::Down;
                    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return Action::Left;
                    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return Action::Right;
                    case SDL_GAMEPAD_BUTTON_SOUTH:
                    case SDL_GAMEPAD_BUTTON_START: return Action::Confirm;
                    case SDL_GAMEPAD_BUTTON_EAST: return Action::Back;
                    default: return Action::None;
                }
            }
            default: return Action::None;
        }
    }

    // The stick and the d-pad held down: repeated moves.
    Action poll() {
        int count = 0;
        SDL_JoystickID* ids = SDL_GetGamepads(&count);
        float x = 0, y = 0;
        bool dpad[4] = {};
        for (int i = 0; ids != nullptr && i < count; ++i) {
            SDL_Gamepad* pad = SDL_GetGamepadFromID(ids[i]);
            if (pad == nullptr) continue;
            const float ax = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;
            const float ay = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;
            if (std::fabs(ax) > std::fabs(x)) x = ax;
            if (std::fabs(ay) > std::fabs(y)) y = ay;
            dpad[0] |= SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_UP);
            dpad[1] |= SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
            dpad[2] |= SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
            dpad[3] |= SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
        }
        SDL_free(ids);
        int dir = 0;
        if (y < -0.5f && std::fabs(y) >= std::fabs(x)) dir = static_cast<int>(Action::Up);
        else if (y > 0.5f && std::fabs(y) >= std::fabs(x)) dir = static_cast<int>(Action::Down);
        else if (x < -0.5f) dir = static_cast<int>(Action::Left);
        else if (x > 0.5f) dir = static_cast<int>(Action::Right);
        const bool from_dpad = dir == 0 && (dpad[0] || dpad[1] || dpad[2] || dpad[3]);
        if (from_dpad)
            dir = static_cast<int>(dpad[0] ? Action::Up : dpad[1] ? Action::Down : dpad[2] ? Action::Left : Action::Right);
        const Uint64 now = SDL_GetTicks();
        if (dir == 0) {
            stick_dir = 0;
            return Action::None;
        }
        if (dir != stick_dir) {
            stick_dir = dir;
            stick_next = now + 380;
            // The d-pad's first press came as an event already.
            return from_dpad ? Action::None : static_cast<Action>(dir);
        }
        if (now < stick_next) return Action::None;
        stick_next = now + 110;
        return static_cast<Action>(dir);
    }
};

// --- The options list -------------------------------------------------------------

struct Row {
    enum Kind { Header, Toggle, Choice, Slider, Done } kind;
    std::string label;
    int rule = -1;    // Choice: index into rules()
    int volume = -1;  // Slider: index into GameOptions::volume
};

// Friendlier names for the rule values.
std::string value_name(const std::string& key, const std::string& v) {
    static const struct {
        const char* id;
        const char* name;
    } tracks[] = {{"37_SPEED", "THREE SEVEN SPEEDWAY"}, {"DINOSAUR", "DINOSAUR CANYON"},
                  {"SEASIDE", "SEASIDE STREET GALAXY"}, {"DAYTONA", "DAYTONA SPEEDWAY"},
                  {"LAKESIDE", "LAKESIDE CASTLE"},       {"METRO", "METRO CITY"}};
    for (const auto& t : tracks)
        if (v == t.id) return t.name;
    if ((key == "CAR SELECT" || key == "CUSTOM TOURNAMENT CAR") && v.size() == 1) return "CAR " + v;
    if (key == "DEFAULT CAMERA") return "VIEW " + v;
    if (key == "TOURNAMENT DURATION") return v + (v == "1" ? " DAY" : " DAYS");
    return v;
}

std::vector<Row> build_rows(const GameOptions& o) {
    std::vector<Row> rows;
    rows.push_back({Row::Header, "GAME"});
    rows.push_back({Row::Toggle, "FREE PLAY"});
    if (o.has_audio) {
        rows.push_back({Row::Slider, "MUSIC VOLUME", -1, 0});
        rows.push_back({Row::Slider, "EFFECTS VOLUME", -1, 1});
        rows.push_back({Row::Slider, "VOICE VOLUME", -1, 2});
    }
    rows.push_back({Row::Header, "RULES"});
    const auto& r = rules();
    for (std::size_t i = 0; i < r.size(); ++i) rows.push_back({Row::Choice, narrow(r[i].key), static_cast<int>(i)});
    rows.push_back({Row::Done, "DONE"});
    return rows;
}

// --- The screens -----------------------------------------------------------------

struct Big {
    const Install* in = nullptr;
    SDL_Window* win = nullptr;
    SDL_Texture* logo = nullptr;
    float logo_w = 0, logo_h = 0;
    GameOptions opts;
    Input input;
    enum Screen { Title, Options } screen = Title;
    int title_sel = 0;  // 0 START, 1 OPTIONS
    std::vector<Row> rows;
    int row_sel = 1;
    float scroll = 0;  // rows, eased toward the target
    bool dirty = false;
    Uint64 fade_start = 0;  // screen changes fade in
    bool starting = false;
    Uint64 start_at = 0;
    std::string note;  // a line under the title (no game found, saved...)

    float t() const { return SDL_GetTicks() / 1000.0f; }

    // Sky, speed stripes, checkered strip.
    void background() {
        vgradient(0, 0, kW, kH, kSkyTop, kSkyBottom);
        // Light streaks drifting across the sky.
        const float drift = std::fmod(t() * 60.0f, 400.0f);
        for (int i = -1; i < 5; ++i)
            para(i * 400.0f + drift - 200, 0, 90, kH, 260, rgb(0xFFFFFF, 0.035f));
        // Racing stripes, bottom left to right, as on the cabinet art.
        const struct {
            SDL_FColor c;
            float y;
        } stripes[] = {{kYellow, 0}, {kOrange, 18}, {kRed, 36}};
        // Low, under the menu, rising to the right.
        for (const auto& s : stripes)
            quad({0, 682 + s.y * 0.6f}, {kW, 652 + s.y * 0.6f}, {kW, 661 + s.y * 0.6f}, {0, 691 + s.y * 0.6f}, s.c, s.c,
                 s.c, s.c);
        for (const auto& s : stripes)
            quad({kW * 0.55f, 0 + s.y * 0.8f}, {kW, s.y * 0.8f}, {kW, 14 + s.y * 0.8f}, {kW * 0.55f + 40, 14 + s.y * 0.8f},
                 with_alpha(s.c, 0), s.c, s.c, with_alpha(s.c, 0));
        // The footer: a checkered strip over a black bar.
        const float cy = kH - 66, sq = 11;
        SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
        SDL_FRect bar{0, cy, kW, 2 * sq};
        SDL_RenderFillRect(ren, &bar);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        for (int i = 0; i * sq < kW; ++i)
            for (int r = 0; r < 2; ++r)
                if ((i + r) % 2 == 0) {
                    SDL_FRect f{i * sq, cy + r * sq, sq, sq};
                    SDL_RenderFillRect(ren, &f);
                }
        SDL_FRect footer{0, cy + 2 * sq, kW, kH - cy - 2 * sq};
        SDL_SetRenderDrawColor(ren, 8, 8, 12, 255);
        SDL_RenderFillRect(ren, &footer);
    }

    // A button prompt: the glyph and its word, right to left from `x`.
    float hint(float x, float y, bool confirm, const char* word) {
        const float w = hint_font.width(word);
        x -= w;
        hint_font.draw(word, x, y - 13, kWhite, kItalic * 0.5f);
        x -= 44;
        const float cx = x + 16, cy = y;
        switch (input.glyphs) {
            case Glyphs::Keyboard: {
                const char* key = confirm ? "ENTER" : "ESC";
                const float kw = small_font.width(key) + 18;
                const float kx = x + 32 - kw;
                SDL_FRect cap{kx, cy - 15, kw, 30};
                SDL_SetRenderDrawColor(ren, 235, 235, 235, 255);
                SDL_RenderFillRect(ren, &cap);
                small_font.draw(key, kx + 9, cy - 14, kBlack);
                return kx - 34;
            }
            case Glyphs::Xbox:
                fan(cx, cy, 16, confirm ? kGreen : kRed);
                small_font.draw(confirm ? "A" : "B", cx - small_font.width(confirm ? "A" : "B") / 2, cy - 14, kWhite);
                break;
            case Glyphs::Nintendo:
                fan(cx, cy, 16, rgb(0x3A3A3A));
                small_font.draw(confirm ? "B" : "A", cx - small_font.width(confirm ? "B" : "A") / 2, cy - 14, kWhite);
                break;
            case Glyphs::PlayStation:
                fan(cx, cy, 16, rgb(0x202024));
                if (confirm) {
                    segment(cx - 7, cy - 7, cx + 7, cy + 7, 3.5f, kPsBlue);
                    segment(cx - 7, cy + 7, cx + 7, cy - 7, 3.5f, kPsBlue);
                } else {
                    ring(cx, cy, 8.5f, 3.2f, kPsRed);
                }
                break;
        }
        return x - 34;
    }

    void footer(bool change) {
        float x = kW - 40;
        const float y = kH - 22;
        x = hint(x, y, false, screen == Title ? "EXIT" : "BACK");
        x = hint(x, y, true, "SELECT");
        if (change) {
            const float w = hint_font.width("CHANGE");
            hint_font.draw("CHANGE", x - w, y - 13, kWhite, kItalic * 0.5f);
            const float ax = x - w - 40;
            triangle({ax, y}, {ax + 12, y - 9}, {ax + 12, y + 9}, kYellow);
            triangle({ax + 30, y}, {ax + 18, y - 9}, {ax + 18, y + 9}, kYellow);
        }
    }

    void draw_title() {
        // The logo on a white band, as on the cabinet's marquee.
        const float band_y = 120, band_h = 330;
        para(-60, band_y - 14, kW + 120, 10, 40, kRed);
        para(-60, band_y, kW + 120, band_h, 40, kWhite);
        para(-60, band_y + band_h + 4, kW + 120, 10, 40, kYellow);
        if (logo != nullptr) {
            const float w = 680, h = w * logo_h / logo_w;
            SDL_FRect dst{(kW - w) / 2 + 20, band_y + (band_h - h) / 2, w, h};
            SDL_RenderTexture(ren, logo, nullptr, &dst);
        }
        const char* edition = in->edition == Edition::Nse2019   ? "NEW SEASON EDITION"
                              : in->edition == Edition::Dcu2017 ? "2017 EDITION"
                                                                : "";
        if (*edition) {
            const float w = heading_font.width(edition);
            heading_font.shadowed(edition, (kW - w) / 2 + 10, band_y + band_h + 22, kYellow, kItalic);
        }
        if (!note.empty()) small_font.draw(note, 40, kH - 36, kYellow, kItalic * 0.5f);
        const char* items[2] = {"START", "OPTIONS"};
        for (int i = 0; i < 2; ++i) {
            const float y = 524 + i * 72;
            const float w = title_font.width(items[i]);
            const float x = (kW - w) / 2;
            const bool sel = title_sel == i;
            if (sel) {
                para(x - 54, y - 2, w + 96, 66, 16, kYellow, kOrange);
                // START pulses as the arcade's "PRESS START"; it flashes once chosen.
                const bool pulse = i == 0 && !starting && std::fmod(t(), 1.0f) > 0.6f;
                const bool flash = starting && std::fmod(t() * 8, 1.0f) > 0.5f;
                title_font.draw(items[i], x, y, flash ? kWhite : pulse ? kRed : kBlack, kItalic);
            } else {
                title_font.shadowed(items[i], x, y, kWhite, kItalic);
            }
        }
        footer(false);
    }

    std::string row_value(const Row& r) const {
        switch (r.kind) {
            case Row::Toggle: return opts.freeplay ? "ON" : "OFF";
            case Row::Choice: {
                const Rule& rule = rules()[r.rule];
                return value_name(narrow(rule.key), narrow(rule.options[opts.rule[r.rule]]));
            }
            default: return "";
        }
    }

    void draw_options() {
        // The header tab and a small logo chip.
        para(40, 34, 420, 64, 18, kRed, rgb(0xB0141C));
        heading_font.shadowed("OPTIONS", 84, 44, kWhite, kItalic);
        if (logo != nullptr) {
            para(kW - 300, 28, 250, 82, 18, kWhite);
            const float w = 176, h = w * logo_h / logo_w;
            SDL_FRect dst{kW - 260, 28 + (82 - h) / 2, w, h};
            SDL_RenderTexture(ren, logo, nullptr, &dst);
        }

        const float top = 128, row_h = 52, visible = 10;
        const float target = std::clamp(row_sel - visible / 2 + 0.5f, 0.0f,
                                        std::max(0.0f, static_cast<float>(rows.size()) - visible));
        scroll += (target - scroll) * 0.25f;
        SDL_Rect clip{0, static_cast<int>(top) - 4, static_cast<int>(kW), static_cast<int>(visible * row_h) + 6};
        SDL_SetRenderClipRect(ren, &clip);
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const float y = top + (i - scroll) * row_h;
            if (y < top - row_h || y > top + visible * row_h) continue;
            const Row& r = rows[i];
            const bool sel = static_cast<int>(i) == row_sel;
            const float x = 110, w = kW - 2 * x - 20;
            if (r.kind == Row::Header) {
                heading_font.shadowed(r.label, x + 4, y + 2, kYellow, kItalic);
                const float lw = heading_font.width(r.label);
                para(x + lw + 30, y + 26, w - lw - 30, 4, 2, kYellow, with_alpha(kYellow, 0));
                continue;
            }
            if (sel) para(x, y + 4, w, row_h - 8, 12, kYellow, kOrange);
            else para(x, y + 4, w, row_h - 8, 12, rgb(0x000000, 0.32f), rgb(0x000000, 0.12f));
            const SDL_FColor text = sel ? kBlack : kWhite;
            const float ty = y + (row_h - row_font.size) / 2 - 2;
            if (sel) row_font.draw(r.label, x + 34, ty, text, kItalic * 0.6f);
            else row_font.shadowed(r.label, x + 34, ty, text, kItalic * 0.6f, 2);
            const float right = x + w - 30;
            if (r.kind == Row::Slider) {
                const int v = opts.volume[r.volume];
                const std::string num = std::to_string(v);
                const float bw = 300, bx = right - bw - 70, by = y + row_h / 2 - 7;
                para(bx, by, bw, 14, 6, sel ? rgb(0x000000, 0.25f) : rgb(0xFFFFFF, 0.2f));
                para(bx, by, bw * v / 100.0f, 14, 6, sel ? kRed : kYellow, sel ? rgb(0xB0141C) : kOrange);
                const float nw = row_font.width(num);
                row_font.draw(num, right - nw, ty, text, kItalic * 0.6f);
                if (sel) {
                    triangle({bx - 34, y + row_h / 2}, {bx - 20, y + row_h / 2 - 10}, {bx - 20, y + row_h / 2 + 10}, kBlack);
                    triangle({right - 52, y + row_h / 2}, {right - 66, y + row_h / 2 - 10}, {right - 66, y + row_h / 2 + 10}, kBlack);
                }
            } else if (r.kind != Row::Done) {
                const std::string v = row_value(r);
                const float vw = row_font.width(v);
                const float vx = right - vw - (sel ? 34 : 0);
                if (sel) {
                    row_font.draw(v, vx, ty, text, kItalic * 0.6f);
                    triangle({vx - 34, y + row_h / 2}, {vx - 20, y + row_h / 2 - 10}, {vx - 20, y + row_h / 2 + 10}, kBlack);
                    triangle({right, y + row_h / 2}, {right - 14, y + row_h / 2 - 10}, {right - 14, y + row_h / 2 + 10}, kBlack);
                } else {
                    row_font.shadowed(v, vx, ty, kYellow, kItalic * 0.6f, 2);
                }
            }
        }
        SDL_SetRenderClipRect(ren, nullptr);
        // Scroll position.
        const float track_h = visible * row_h - 8, frac = visible / rows.size();
        const float pos = scroll / std::max(1.0f, static_cast<float>(rows.size()) - visible);
        SDL_FRect track{kW - 70, top + 4, 6, track_h};
        SDL_SetRenderDrawColor(ren, 255, 255, 255, 50);
        SDL_RenderFillRect(ren, &track);
        SDL_FRect thumb{kW - 70, top + 4 + pos * track_h * (1 - frac), 6, track_h * frac};
        SDL_SetRenderDrawColor(ren, 255, 212, 0, 255);
        SDL_RenderFillRect(ren, &thumb);
        footer(rows[row_sel].kind != Row::Done);
    }

    void change(int dir) {
        Row& r = rows[row_sel];
        switch (r.kind) {
            case Row::Toggle: opts.freeplay = !opts.freeplay; break;
            case Row::Slider: opts.volume[r.volume] = std::clamp(opts.volume[r.volume] + 5 * dir, 0, 100); break;
            case Row::Choice: {
                const int n = static_cast<int>(rules()[r.rule].options.size());
                opts.rule[r.rule] = (opts.rule[r.rule] + dir + n) % n;
                break;
            }
            default: return;
        }
        dirty = true;
    }

    void move(int dir) {
        int i = row_sel;
        do i = (i + dir + static_cast<int>(rows.size())) % static_cast<int>(rows.size());
        while (rows[i].kind == Row::Header);
        row_sel = i;
    }

    void leave_options() {
        if (dirty) note = opts.save(*in) ? "SETTINGS SAVED" : "SETTINGS COULD NOT BE SAVED";
        dirty = false;
        screen = Title;
        title_sel = 1;
        fade_start = SDL_GetTicks();
    }

    // Returns false to quit.
    bool act(Action a) {
        if (a == Action::None || starting) return true;
        if (screen == Title) {
            switch (a) {
                case Action::Up:
                case Action::Down: title_sel ^= 1; break;
                case Action::Confirm:
                    if (title_sel == 0) {
                        if (!in->has_game()) {
                            note = "PUT THE LAUNCHER IN THE FOLDER THAT HOLDS DAYTONA.EXE";
                            break;
                        }
                        starting = true;
                        start_at = SDL_GetTicks();
                    } else {
                        screen = Options;
                        note.clear();
                        fade_start = SDL_GetTicks();
                    }
                    break;
                case Action::Back: return false;
                default: break;
            }
            return true;
        }
        switch (a) {
            case Action::Up: move(-1); break;
            case Action::Down: move(1); break;
            case Action::Left: change(-1); break;
            case Action::Right: change(1); break;
            case Action::Confirm:
                if (rows[row_sel].kind == Row::Done) leave_options();
                else change(1);
                break;
            case Action::Back: leave_options(); break;
            default: break;
        }
        return true;
    }

    void frame() {
        SDL_SetRenderDrawColor(ren, 4, 20, 50, 255);
        SDL_RenderClear(ren);
        background();
        if (screen == Title) draw_title();
        else draw_options();
        // Fade in after a screen change; fade out when starting.
        float fade = 1.0f - std::min(1.0f, (SDL_GetTicks() - fade_start) / 250.0f);
        if (starting) fade = std::min(1.0f, (SDL_GetTicks() - start_at) / 700.0f);
        if (fade > 0) {
            SDL_SetRenderDrawColor(ren, 0, 0, 0, static_cast<Uint8>(fade * 255));
            SDL_RenderFillRect(ren, nullptr);
        }
    }
};

SDL_Texture* load_logo(float& w, float& h) {
    const std::vector<unsigned char> png = resource(kResLogo);
    int iw = 0, ih = 0, n = 0;
    unsigned char* px = stbi_load_from_memory(png.data(), static_cast<int>(png.size()), &iw, &ih, &n, 4);
    if (px == nullptr) return nullptr;
    SDL_Texture* t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, iw, ih);
    if (t != nullptr) {
        SDL_UpdateTexture(t, nullptr, px, iw * 4);
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
    }
    stbi_image_free(px);
    w = static_cast<float>(iw);
    h = static_cast<float>(ih);
    return t;
}

}  // namespace

int run_big(const Install& in) {
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_STEAM, "0");  // Steam Input's pad, as the shim
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_STEAMDECK, "0");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        MessageBoxA(nullptr, SDL_GetError(), "Daytona", MB_ICONERROR);
        return 1;
    }
    const char* shot = std::getenv("DAYTONA_LAUNCHER_SHOT");
    const SDL_WindowFlags flags = shot != nullptr ? 0 : SDL_WINDOW_FULLSCREEN;
    Big big;
    big.in = &in;
    big.win = SDL_CreateWindow("Daytona Championship USA", static_cast<int>(kW), static_cast<int>(kH), flags);
    ren = big.win != nullptr ? SDL_CreateRenderer(big.win, nullptr) : nullptr;
    if (ren == nullptr) {
        MessageBoxA(nullptr, SDL_GetError(), "Daytona", MB_ICONERROR);
        return 1;
    }
    SDL_SetRenderVSync(ren, 1);
    SDL_SetRenderLogicalPresentation(ren, static_cast<int>(kW), static_cast<int>(kH), SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_HideCursor();

    const std::vector<unsigned char> title_ttf = resource(kResTitleFont), body_ttf = resource(kResBodyFont);
    title_font.load(title_ttf, 60);
    heading_font.load(title_ttf, 40);
    hint_font.load(title_ttf, 24);
    row_font.load(body_ttf, 30);
    small_font.load(body_ttf, 22);
    big.logo = load_logo(big.logo_w, big.logo_h);
    big.opts.load(in);
    big.rows = build_rows(big.opts);
    big.fade_start = SDL_GetTicks();
    if (const char* g = std::getenv("DAYTONA_LAUNCHER_GLYPHS"))  // testing: ps, xbox, nintendo
        big.input.glyphs = g[0] == 'p' ? Glyphs::PlayStation : g[0] == 'x' ? Glyphs::Xbox : Glyphs::Nintendo;
    if (GetCommandLineW() != nullptr && wcsstr(GetCommandLineW(), L"--screen=options") != nullptr) big.screen = Big::Options;
    if (!in.has_game()) big.note = "PUT THE LAUNCHER IN THE FOLDER THAT HOLDS DAYTONA.EXE";

    int frames = 0;
    for (bool running = true; running;) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) running = false;
            running = big.act(big.input.from_event(e)) && running;
        }
        running = big.act(big.input.poll()) && running;
        big.frame();
        if (shot != nullptr && ++frames % 20 == 0) {  // testing: the latest frame
            if (SDL_Surface* s = SDL_RenderReadPixels(ren, nullptr)) {
                SDL_SaveBMP(s, shot);
                SDL_DestroySurface(s);
            }
        }
        SDL_RenderPresent(ren);
        if (big.starting && SDL_GetTicks() - big.start_at > 750) {
            SDL_DestroyRenderer(ren);
            SDL_DestroyWindow(big.win);
            SDL_Quit();
            play(in);
        }
    }
    if (big.dirty) big.opts.save(in);
    SDL_Quit();
    return 0;
}

}  // namespace launcher
