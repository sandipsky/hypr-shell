#pragma once

#include <giomm.h>
#include <sigc++/sigc++.h>

#include <map>
#include <string>

namespace hyprshell {

// ~/.config/hypr-shell/config.json, hot-reloaded. Written by hand or by
// hypr-shell-settings; absent keys mean defaults, invalid JSON keeps defaults.
class Config {
public:
    enum class BarPosition { Top, Bottom };

    static Config& get();

    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    BarPosition bar_position() const { return bar_position_; }
    int bar_height() const { return bar_height_; } // px; 0 = automatic (CSS)
    // Modules absent from bar.modules are enabled, so new modules default on.
    bool module_enabled(const std::string& name) const;

    sigc::signal<void()>& signal_changed() { return changed_; }

private:
    Config();

    void load();

    std::string path_;
    Glib::RefPtr<Gio::FileMonitor> monitor_;
    BarPosition bar_position_ = BarPosition::Top;
    int bar_height_ = 0;
    std::map<std::string, bool> modules_;
    sigc::signal<void()> changed_;
};

} // namespace hyprshell
