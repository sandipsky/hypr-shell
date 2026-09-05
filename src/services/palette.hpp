// Theme palette derivation, shared by the shell and hypr-shell-settings
// (header-only, plain C++ — no GTK types).
//
// One accent colour + a dark/light flag give the full Noctalia-style token
// set (mPrimary, mOnPrimary, mSurface, ...) the way Material's tonal-spot
// scheme does: in dark mode mPrimary is a pastel of the accent (tone 80 —
// the accent lightened, never darkened) with a deep shade of the same hue on
// it; in light mode the accent is darkened until white text reads on it.
// The neutrals are tinted with the accent's hue. The default accent + dark
// reproduces the palette the CSS was written with (a snapshot of the user's
// Noctalia colors.json: #bfc2ff on #131316).
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace hyprshell {

constexpr const char* kDefaultAccent = "#bfc2ff";
constexpr const char* kDefaultFont = "Fira Sans";

struct Rgb {
    double r = 0, g = 0, b = 0; // 0..1
};

struct Hsl {
    double h = 0, s = 0, l = 0; // h in degrees
};

inline bool parse_hex_color(const std::string& text, Rgb& out) {
    if (text.size() != 7 || text[0] != '#')
        return false;
    unsigned v = 0;
    for (size_t i = 1; i < 7; ++i) {
        const char c = text[i];
        unsigned d;
        if (c >= '0' && c <= '9')
            d = c - '0';
        else if (c >= 'a' && c <= 'f')
            d = 10 + c - 'a';
        else if (c >= 'A' && c <= 'F')
            d = 10 + c - 'A';
        else
            return false;
        v = v * 16 + d;
    }
    out = {((v >> 16) & 0xff) / 255.0, ((v >> 8) & 0xff) / 255.0, (v & 0xff) / 255.0};
    return true;
}

inline std::string to_hex(const Rgb& c) {
    const auto byte = [](double v) {
        return static_cast<int>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0));
    };
    char buf[8];
    std::snprintf(buf, sizeof buf, "#%02x%02x%02x", byte(c.r), byte(c.g), byte(c.b));
    return buf;
}

inline Hsl rgb_to_hsl(const Rgb& c) {
    const double max = std::max({c.r, c.g, c.b});
    const double min = std::min({c.r, c.g, c.b});
    Hsl out;
    out.l = (max + min) / 2.0;
    const double d = max - min;
    if (d < 1e-9) {
        out.h = 0;
        out.s = 0;
        return out;
    }
    out.s = out.l > 0.5 ? d / (2.0 - max - min) : d / (max + min);
    if (max == c.r)
        out.h = 60.0 * std::fmod((c.g - c.b) / d, 6.0);
    else if (max == c.g)
        out.h = 60.0 * ((c.b - c.r) / d + 2.0);
    else
        out.h = 60.0 * ((c.r - c.g) / d + 4.0);
    if (out.h < 0)
        out.h += 360.0;
    return out;
}

inline Rgb hsl_to_rgb(const Hsl& c) {
    const double h = std::fmod(std::fmod(c.h, 360.0) + 360.0, 360.0);
    const double s = std::clamp(c.s, 0.0, 1.0);
    const double l = std::clamp(c.l, 0.0, 1.0);
    const double chroma = (1.0 - std::fabs(2.0 * l - 1.0)) * s;
    const double x = chroma * (1.0 - std::fabs(std::fmod(h / 60.0, 2.0) - 1.0));
    const double m = l - chroma / 2.0;
    double r = 0, g = 0, b = 0;
    if (h < 60) {
        r = chroma; g = x;
    } else if (h < 120) {
        r = x; g = chroma;
    } else if (h < 180) {
        g = chroma; b = x;
    } else if (h < 240) {
        g = x; b = chroma;
    } else if (h < 300) {
        r = x; b = chroma;
    } else {
        r = chroma; b = x;
    }
    return {r + m, g + m, b + m};
}

struct PaletteEntry {
    const char* name;
    std::string hex;
};

constexpr int kPaletteSize = 17;
using Palette = std::array<PaletteEntry, kPaletteSize>;

inline Palette derive_palette(const std::string& accent_hex, bool dark) {
    Rgb accent;
    if (!parse_hex_color(accent_hex, accent))
        parse_hex_color(kDefaultAccent, accent);
    const Hsl a = rgb_to_hsl(accent);
    const auto hsl = [](double h, double s, double l) { return to_hex(hsl_to_rgb({h, s, l})); };

    Palette p;
    int i = 0;
    const auto put = [&](const char* name, std::string hex) { p[i++] = {name, std::move(hex)}; };
    if (dark) {
        // tone-80 pastel of the accent; its "on" colour a deep shade of the hue
        const double primary_l = std::max(a.l, 0.80);
        put("mPrimary", hsl(a.h, a.s, primary_l));
        put("mOnPrimary", hsl(a.h, std::min(1.0, a.s * 0.6 + 0.1), 0.30));
        put("mPrimaryHover", hsl(a.h, a.s, std::min(1.0, primary_l + 0.05)));
        put("mSecondary", hsl(a.h + 5, a.s * 0.27, 0.82));
        put("mOnSecondary", hsl(a.h, a.s * 0.18, 0.22));
        put("mTertiary", hsl(a.h + 87, 0.51, 0.82));
        put("mOnTertiary", hsl(a.h + 83, 0.30, 0.21));
        put("mError", "#ffb4ab");
        put("mOnError", "#690005");
        put("mSurface", hsl(a.h + 3, 0.07, 0.08));
        put("mOnSurface", hsl(a.h + 51, 0.09, 0.89));
        put("mSurfaceVariant", hsl(a.h + 18, 0.06, 0.13));
        put("mOnSurfaceVariant", hsl(a.h + 14, 0.10, 0.79));
        put("mOutline", hsl(a.h + 3, 0.06, 0.29));
        put("mShadow", "#000000");
        put("mHover", hsl(a.h + 87, 0.51, 0.82));
        put("mOnHover", hsl(a.h + 83, 0.30, 0.21));
    } else {
        // Material light tones: primary dark enough for white text (tone 40)
        put("mPrimary", hsl(a.h, a.s, std::min(a.l, 0.42)));
        put("mOnPrimary", "#ffffff");
        put("mPrimaryHover", hsl(a.h, a.s, std::min(a.l, 0.42) + 0.06));
        put("mSecondary", hsl(a.h + 5, a.s * 0.35, 0.38));
        put("mOnSecondary", "#ffffff");
        put("mTertiary", hsl(a.h + 87, 0.45, 0.38));
        put("mOnTertiary", "#ffffff");
        put("mError", "#ba1a1a");
        put("mOnError", "#ffffff");
        put("mSurface", hsl(a.h + 3, 0.30, 0.985));
        put("mOnSurface", hsl(a.h + 51, 0.09, 0.11));
        put("mSurfaceVariant", hsl(a.h + 18, 0.20, 0.92));
        put("mOnSurfaceVariant", hsl(a.h + 14, 0.10, 0.29));
        put("mOutline", hsl(a.h + 3, 0.06, 0.47));
        put("mShadow", "#000000");
        put("mHover", hsl(a.h + 87, 0.45, 0.38));
        put("mOnHover", "#ffffff");
    }
    return p;
}

inline const std::string& palette_color(const Palette& p, const char* name) {
    for (const auto& e : p)
        if (std::string(e.name) == name)
            return e.hex;
    static const std::string black = "#000000";
    return black;
}

// "@define-color mPrimary #bfc2ff;\n..." — GTK named colours, overriding the
// defaults declared in data/style.css when loaded at a higher priority.
inline std::string palette_css(const Palette& p) {
    std::string css;
    for (const auto& e : p)
        css += "@define-color " + std::string(e.name) + " " + e.hex + ";\n";
    return css;
}

} // namespace hyprshell
