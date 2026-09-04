#pragma once

#include <gtkmm.h>

#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace hyprshell {

// Decoded wallpaper textures, one per (path, monitor pixel size, fill mode):
// the image is decoded asynchronously (GdkPixbuf at scale) straight to the
// size the fill mode needs — cover for crop, contain for fit/center, exact for
// stretch — so drawing is a single texture node per frame.
class WallpaperTextureCache {
public:
    enum class Fit { Cover, Contain, Exact, Native };

    static WallpaperTextureCache& get();

    // start decoding (idempotent); signal_ready fires when a texture lands
    void prepare(const std::string& path, int pixel_w, int pixel_h, Fit fit);
    Glib::RefPtr<Gdk::Texture> texture(const std::string& path, int pixel_w, int pixel_h, Fit fit);
    // drop every texture except the given paths (previous + current)
    void retain(const std::vector<std::string>& paths);

    sigc::signal<void()>& signal_ready() { return ready_; }

private:
    using Key = std::tuple<std::string, int, int, Fit>;
    struct Entry {
        Glib::RefPtr<Gdk::Texture> texture;
        Glib::RefPtr<Gio::Cancellable> cancellable;
        bool loading = false;
        bool failed = false;
    };
    struct Pending;

    WallpaperTextureCache() = default;
    void load(const Key& key, Entry& entry);

    std::map<Key, Entry> entries_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    sigc::signal<void()> ready_;
};

// Draws the wallpaper for one monitor and animates the change from the
// previous image with Noctalia's transitions (fade / wipe / disc / stripes),
// built from GSK cross-fade and mask nodes — no shaders.
class WallpaperView : public Gtk::Widget {
public:
    enum class Transition { None, Fade, Wipe, Disc, Stripes };

    WallpaperView();
    ~WallpaperView() override;

    // logical monitor size + integer scale factor (textures are pixel-sized)
    void set_monitor_size(int width, int height, int scale);
    // show `path`; `transition` None swaps instantly. `startup` uses
    // Noctalia's fixed "elegant" parameters (centered disc) instead of random ones.
    void show_image(const std::string& path, Transition transition, bool startup = false);
    // re-decode for the current fill mode (config change) and repaint
    void refresh() { set_monitor_size(width_, height_, scale_); }

    static Transition transition_from_key(const std::string& key);

    // dev hook: render the current frame offscreen (HS_WALLPAPER_DUMP)
    void dump_frame(const std::string& png_path);

protected:
    void snapshot_vfunc(const Glib::RefPtr<Gtk::Snapshot>& snapshot) override;

private:
    void prepare(const std::string& path);
    Glib::RefPtr<Gdk::Texture> texture_for(const std::string& path);
    void draw_image(const Glib::RefPtr<Gtk::Snapshot>& snapshot,
                    const Glib::RefPtr<Gdk::Texture>& texture, double w, double h);
    void start_animation();

    std::string current_;
    std::string previous_;
    int width_ = 0, height_ = 0, scale_ = 1;
    Transition transition_ = Transition::None;
    bool animating_ = false;
    bool waiting_for_texture_ = false; // transition starts once the new image decoded
    gint64 start_us_ = 0;
    double progress_ = 1.0;
    // per-change randomized parameters (Noctalia's Background.qml rolls)
    double wipe_direction_ = 0.0;             // float in [0,4), bucketed like the shader
    double disc_cx_ = 0.5, disc_cy_ = 0.5;
    int stripes_count_ = 16;
    double stripes_angle_ = 0.0;              // degrees
    int dumped_ = 0;                          // HS_WALLPAPER_DUMP: thresholds written this run
    sigc::connection ready_connection_;
};

// Background layer window covering one monitor.
class WallpaperWindow : public Gtk::Window {
public:
    explicit WallpaperWindow(const Glib::RefPtr<Gdk::Monitor>& monitor);
    ~WallpaperWindow() override;

    const Glib::RefPtr<Gdk::Monitor>& monitor() const { return monitor_; }
    WallpaperView& view() { return view_; }

private:
    void apply_geometry();

    Glib::RefPtr<Gdk::Monitor> monitor_;
    WallpaperView view_;
};

// Owns one WallpaperWindow per connected monitor and feeds them from the
// Wallpaper service + config (fill mode, transitions).
class WallpaperManager {
public:
    WallpaperManager(Gtk::Application& app);
    ~WallpaperManager();

private:
    void sync_monitors();
    void on_current_changed(const std::string& previous, bool animate);
    void apply_config();
    WallpaperView::Transition pick_transition() const;
    void show_all(WallpaperView::Transition transition, bool startup);

    Gtk::Application& app_;
    std::vector<std::unique_ptr<WallpaperWindow>> windows_;
    std::string fill_key_; // last applied fill mode (redraw on change)
    bool started_ = false; // startup transition done — hotplugged monitors show instantly
};

} // namespace hyprshell
