#include "services/theme.hpp"

#include "services/config.hpp"

namespace hyprshell {

Theme& Theme::get() {
    static Theme instance;
    return instance;
}

Theme::Theme() {
    rebuild();
    Config::get().signal_changed().connect([this] {
        const auto& ui = Config::get().ui();
        if (ui.accent == accent_ && ui.dark_mode == dark_ && ui.font == font_)
            return;
        rebuild();
        changed_.emit();
    });
}

void Theme::rebuild() {
    const auto& ui = Config::get().ui();
    accent_ = ui.accent;
    dark_ = ui.dark_mode;
    font_ = ui.font;
    palette_ = derive_palette(accent_, dark_);
}

Gdk::RGBA Theme::rgba(const char* name) const {
    Rgb c;
    parse_hex_color(hex(name), c);
    return Gdk::RGBA(static_cast<float>(c.r), static_cast<float>(c.g), static_cast<float>(c.b),
                     1.0f);
}

} // namespace hyprshell
