#pragma once

#include <gtkmm.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>

typedef struct _GdkTexture GdkTexture;
typedef struct _GskRenderer GskRenderer;

namespace hyprshell {

// Lock screen wallpaper cache — Noctalia's ImageCacheService.getLarge role.
// The configured image is decoded asynchronously at each monitor's pixel size
// (cover-scaled) and, when lock_screen.blur > 0, blurred once into a texture
// with a private GL renderer (gsk_renderer_realize_for_display, no window
// needed). Both are prepared at startup and on every config change, so a
// lock surface's very first frame paints the finished wallpaper instead of
// black followed by a late decode + a full-screen blur on the animation's
// first frames. A full-screen texture is 8–33 MB, so retain() drops every
// entry the current config no longer draws — including the unblurred decode
// once its blurred version exists.
class LockWallpaperCache {
public:
    static LockWallpaperCache& get();

    // Decode (and blur) `path` for a monitor of logical width x height at
    // `scale`; idempotent for identical arguments. Empty path clears nothing.
    void prepare(const std::string& path, int width, int height, int scale, double blur);

    // Ready textures, or null while decoding / when there is no image.
    Glib::RefPtr<Gdk::Texture> texture(const std::string& path, int width, int height, int scale);
    GdkTexture* blurred(const std::string& path, int width, int height, int scale, double blur);

    // Free everything that is not `path` at `blur` (all monitor sizes kept);
    // an empty path clears the cache.
    void retain(const std::string& path, double blur);

    sigc::signal<void()>& signal_changed() { return changed_; }

    static double blur_radius(double blur); // 0..1 → px (Noctalia's blurMax 48)

private:
    struct Entry {
        Glib::RefPtr<Gdk::Texture> texture;
        std::map<double, GdkTexture*> blurred; // by radius, owned
        std::set<double> wanted;               // radii to render once decoded
        Glib::RefPtr<Gio::Cancellable> cancellable;
        bool loading = false;

        Entry() = default;
        Entry(Entry&&) noexcept;
        Entry& operator=(Entry&&) noexcept;
        Entry(const Entry&) = delete;
        Entry& operator=(const Entry&) = delete;
        ~Entry();
        void drop_blurred(double radius);
    };
    using Key = std::tuple<std::string, int, int, int>; // path, width, height, scale
    struct Pending;

    LockWallpaperCache() = default;
    void load(const Key& key, Entry& entry);
    void render_blur(const Key& key, Entry& entry, double radius);
    GskRenderer* renderer();

    std::map<Key, Entry> entries_;
    GskRenderer* renderer_ = nullptr;
    bool renderer_failed_ = false;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    sigc::signal<void()> changed_;
};

// The wallpaper layer of one lock surface: draws the cache's texture for its
// monitor (blurred if configured), black until it is ready. Falls back to a
// live GSK blur only when the private renderer is unavailable.
class LockBackground : public Gtk::Widget {
public:
    LockBackground();
    ~LockBackground() override;

    // `width/height` logical monitor size, `scale` its integer scale factor.
    void set_image(const std::string& path, int width, int height, int scale);
    void set_blur(double strength); // 0..1

protected:
    void snapshot_vfunc(const Glib::RefPtr<Gtk::Snapshot>& snapshot) override;

private:
    std::string path_;
    int width_ = 0;
    int height_ = 0;
    int scale_ = 1;
    double blur_ = 0.0;
    sigc::connection cache_connection_;
};

} // namespace hyprshell
