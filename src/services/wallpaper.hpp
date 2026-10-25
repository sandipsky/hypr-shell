#pragma once

#include <giomm.h>
#include <sigc++/sigc++.h>

#include <string>
#include <vector>

namespace hyprshell {

// Desktop wallpaper backend (Noctalia's WallpaperService, single directory,
// same image on every monitor). Scans `wallpaper.directory` for images (and
// rescans when it changes on disk), remembers the current image in
// ~/.cache/hypr-shell/wallpaper.json — the shell never writes config.json,
// so slideshow picks live there while `wallpaper.current` carries the settings
// app's explicit choice, adopted on value-change edges — and runs the
// slideshow timer. Rendering is `WallpaperWindow`'s job.
class Wallpaper {
public:
    static Wallpaper& get();

    Wallpaper(const Wallpaper&) = delete;
    Wallpaper& operator=(const Wallpaper&) = delete;

    // Sorted (case-insensitive) image paths of the configured directory.
    const std::vector<std::string>& images() const { return images_; }
    // Path of the wallpaper on screen; empty = nothing to show.
    const std::string& current() const { return current_; }

    // Show `path` now (persisted; restarts the slideshow timer). `animate`
    // false skips the transition (startup, config-driven fill changes).
    void set_current(const std::string& path, bool animate = true);
    void next();     // slideshow step (also usable from a keybind later)

    // see services/wallpaper_files.hpp (shared with the settings app)
    static bool is_image_file(const std::string& name);

    // emitted after current() changed: (previous, animate)
    sigc::signal<void(const std::string&, bool)>& signal_current_changed() { return current_changed_; }
    sigc::signal<void()>& signal_images_changed() { return images_changed_; }

private:
    Wallpaper();

    void apply_config();
    void scan();
    void save_state();
    void restart_timer();
    std::string pick_next();

    std::string state_path_;
    std::string directory_;
    std::string config_current_; // last seen wallpaper.current (edge detection)
    std::string current_;
    std::vector<std::string> images_;
    std::vector<std::string> used_random_; // Noctalia's usedRandomWallpapers shuffle bag
    bool slideshow_ = false;               // last seen wallpaper.slideshow
    int order_ = 0;                        // last seen wallpaper.slideshow_order
    Glib::RefPtr<Gio::FileMonitor> dir_monitor_;
    Glib::RefPtr<Gio::Cancellable> scan_cancellable_;
    sigc::connection timer_;
    sigc::connection rescan_debounce_;
    bool loaded_ = false;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    sigc::signal<void(const std::string&, bool)> current_changed_;
    sigc::signal<void()> images_changed_;
};

} // namespace hyprshell
