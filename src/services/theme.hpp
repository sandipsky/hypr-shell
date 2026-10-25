// Theme: the shell's view of config.json's `ui` block — the derived palette
// (services/palette.hpp), the text font and the dark/light flag. Widgets
// that draw with cairo/GSK read colours from here instead of constants; CSS
// gets the same palette through the theme provider in main.cpp.
#pragma once

#include "services/palette.hpp"

#include <gdkmm/rgba.h>
#include <sigc++/sigc++.h>

#include <string>

namespace hyprshell {

class Theme {
public:
    static Theme& get();

    const Palette& palette() const { return palette_; }
    const std::string& font() const { return font_; }
    bool dark() const { return dark_; }

    const std::string& hex(const char* name) const { return palette_color(palette_, name); }
    Gdk::RGBA rgba(const char* name) const;

    // @define-color tokens for every palette entry
    std::string css() const { return palette_css(palette_); }

    sigc::signal<void()>& signal_changed() { return changed_; }

private:
    Theme();
    void rebuild();

    Palette palette_;
    std::string font_;
    std::string accent_;
    bool dark_ = true;
    sigc::signal<void()> changed_;
};

} // namespace hyprshell
