#include "bar/lock_background.hpp"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gsk/gsk.h>

#include <algorithm>
#include <cmath>

namespace hyprshell {

namespace {

constexpr double kBlurMaxPx = 48.0; // Noctalia's MultiEffect blurMax

// Image rectangle covering `width` x `height` plus `overscan` on every side
// (PreserveAspectCrop), centered.
graphene_rect_t cover_rect(float width, float height, float overscan, int tex_w, int tex_h) {
    const float box_w = width + 2 * overscan;
    const float box_h = height + 2 * overscan;
    const float scale = std::max(box_w / static_cast<float>(tex_w),
                                 box_h / static_cast<float>(tex_h));
    const float dw = static_cast<float>(tex_w) * scale;
    const float dh = static_cast<float>(tex_h) * scale;
    return GRAPHENE_RECT_INIT((width - dw) / 2, (height - dh) / 2, dw, dh);
}

} // namespace

// -- cache ------------------------------------------------------------------

struct LockWallpaperCache::Pending {
    std::shared_ptr<bool> alive;
    Key key;
};

LockWallpaperCache& LockWallpaperCache::get() {
    static LockWallpaperCache instance;
    return instance;
}

double LockWallpaperCache::blur_radius(double blur) {
    const double radius = std::clamp(blur, 0.0, 1.0) * kBlurMaxPx;
    return radius < 0.5 ? 0.0 : std::round(radius * 2.0) / 2.0;
}

void LockWallpaperCache::prepare(const std::string& path, int width, int height, int scale,
                                 double blur) {
    if (path.empty() || width <= 0 || height <= 0)
        return;
    const Key key{path, width, height, std::max(1, scale)};
    Entry& entry = entries_[key];
    if (!entry.texture && !entry.loading)
        load(key, entry);
    const double radius = blur_radius(blur);
    if (radius <= 0.0)
        return;
    entry.wanted.insert(radius);
    if (entry.texture && !entry.blurred.count(radius))
        render_blur(key, entry, radius);
}

Glib::RefPtr<Gdk::Texture> LockWallpaperCache::texture(const std::string& path, int width,
                                                       int height, int scale) {
    auto it = entries_.find(Key{path, width, height, std::max(1, scale)});
    return it == entries_.end() ? Glib::RefPtr<Gdk::Texture>() : it->second.texture;
}

GdkTexture* LockWallpaperCache::blurred(const std::string& path, int width, int height, int scale,
                                        double blur) {
    const double radius = blur_radius(blur);
    if (radius <= 0.0)
        return nullptr;
    auto it = entries_.find(Key{path, width, height, std::max(1, scale)});
    if (it == entries_.end())
        return nullptr;
    auto blurred = it->second.blurred.find(radius);
    if (blurred == it->second.blurred.end()) {
        if (it->second.texture)
            render_blur(it->first, it->second, radius);
        blurred = it->second.blurred.find(radius);
        if (blurred == it->second.blurred.end())
            return nullptr;
    }
    return blurred->second;
}

// Decode at the monitor's pixel size: header first (cheap, synchronous) to
// pick the axis that covers, then the pixel data asynchronously.
void LockWallpaperCache::load(const Key& key, Entry& entry) {
    const auto& [path, width, height, scale] = key;
    const int pixel_w = width * scale;
    const int pixel_h = height * scale;
    int image_w = 0, image_h = 0;
    if (gdk_pixbuf_get_file_info(path.c_str(), &image_w, &image_h) == nullptr || image_w <= 0 ||
        image_h <= 0) {
        g_warning("lock screen: cannot read image %s", path.c_str());
        return;
    }
    int target_w = -1, target_h = -1;
    const double image_ratio = static_cast<double>(image_w) / image_h;
    const double screen_ratio = static_cast<double>(pixel_w) / pixel_h;
    if (image_ratio > screen_ratio)
        target_h = std::min(image_h, pixel_h); // wider than the screen: fit height
    else
        target_w = std::min(image_w, pixel_w); // taller: fit width

    entry.loading = true;
    entry.cancellable = Gio::Cancellable::create();
    auto cancellable = entry.cancellable;
    auto file = Gio::File::create_for_path(path);
    file->read_async(
        [this, file, key, target_w, target_h, cancellable,
         alive = alive_](Glib::RefPtr<Gio::AsyncResult>& result) {
            if (!*alive)
                return;
            Glib::RefPtr<Gio::FileInputStream> stream;
            try {
                stream = file->read_finish(result);
            } catch (const Glib::Error& e) {
                g_warning("lock screen: cannot open %s: %s", std::get<0>(key).c_str(), e.what());
                if (auto it = entries_.find(key); it != entries_.end())
                    it->second.loading = false;
                return;
            }
            auto* pending = new Pending{alive, key};
            gdk_pixbuf_new_from_stream_at_scale_async(
                G_INPUT_STREAM(stream->gobj()), target_w, target_h, TRUE, cancellable->gobj(),
                [](GObject*, GAsyncResult* async_result, gpointer data) {
                    std::unique_ptr<Pending> pending(static_cast<Pending*>(data));
                    GError* error = nullptr;
                    GdkPixbuf* pixbuf = gdk_pixbuf_new_from_stream_finish(async_result, &error);
                    if (!*pending->alive) {
                        if (pixbuf != nullptr)
                            g_object_unref(pixbuf);
                        g_clear_error(&error);
                        return;
                    }
                    auto& cache = LockWallpaperCache::get();
                    auto it = cache.entries_.find(pending->key);
                    if (it == cache.entries_.end()) {
                        if (pixbuf != nullptr)
                            g_object_unref(pixbuf);
                        g_clear_error(&error);
                        return;
                    }
                    it->second.loading = false;
                    if (pixbuf == nullptr) {
                        if (error != nullptr &&
                            !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                            g_warning("lock screen: image decode failed: %s", error->message);
                        g_clear_error(&error);
                        return;
                    }
                    auto wrapped = Glib::wrap(pixbuf, /*take_copy=*/true);
                    it->second.texture = Gdk::Texture::create_for_pixbuf(wrapped);
                    g_object_unref(pixbuf);
                    for (double radius : it->second.wanted)
                        cache.render_blur(it->first, it->second, radius);
                    cache.changed_.emit(); // surfaces redraw
                },
                pending);
        },
        cancellable);
}

GskRenderer* LockWallpaperCache::renderer() {
    if (renderer_ != nullptr || renderer_failed_)
        return renderer_;
    GskRenderer* renderer = gsk_gl_renderer_new();
    GError* error = nullptr;
    if (!gsk_renderer_realize_for_display(renderer, gdk_display_get_default(), &error)) {
        g_warning("lock screen: no offscreen renderer (%s) — blurring live",
                  error != nullptr ? error->message : "unknown error");
        g_clear_error(&error);
        g_object_unref(renderer);
        renderer_failed_ = true;
        return nullptr;
    }
    renderer_ = renderer;
    return renderer_;
}

// blur(cover image) at device resolution → one texture, rendered once.
void LockWallpaperCache::render_blur(const Key& key, Entry& entry, double radius) {
    GskRenderer* gsk = renderer();
    if (gsk == nullptr || !entry.texture)
        return;
    const auto& [path, width, height, scale] = key;
    const int tex_w = entry.texture->get_width();
    const int tex_h = entry.texture->get_height();

    GtkSnapshot* builder = gtk_snapshot_new();
    gtk_snapshot_scale(builder, static_cast<float>(scale), static_cast<float>(scale));
    gtk_snapshot_push_blur(builder, radius);
    // overscan of twice the radius so the blur never samples the transparent edge
    const graphene_rect_t rect = cover_rect(static_cast<float>(width), static_cast<float>(height),
                                            static_cast<float>(2 * radius), tex_w, tex_h);
    gtk_snapshot_append_texture(builder, entry.texture->gobj(), &rect);
    gtk_snapshot_pop(builder);
    GskRenderNode* node = gtk_snapshot_to_node(builder); // consumes the builder
    if (node == nullptr)
        return;
    const graphene_rect_t viewport = GRAPHENE_RECT_INIT(
        0, 0, static_cast<float>(width * scale), static_cast<float>(height * scale));
    GdkTexture* blurred = gsk_renderer_render_texture(gsk, node, &viewport);
    gsk_render_node_unref(node);
    if (blurred == nullptr) {
        g_warning("lock screen: blur render failed");
        return;
    }
    entry.blurred[radius] = blurred;
    changed_.emit();
}

// -- widget -------------------------------------------------------------------

LockBackground::LockBackground() {
    add_css_class("lock-background");
    set_overflow(Gtk::Overflow::HIDDEN); // clips the overscanned image (live-blur fallback)
    set_can_target(false);
    set_hexpand(true);
    set_vexpand(true);
    cache_connection_ =
        LockWallpaperCache::get().signal_changed().connect([this] { queue_draw(); });
}

LockBackground::~LockBackground() {
    cache_connection_.disconnect();
}

void LockBackground::set_image(const std::string& path, int width, int height, int scale) {
    scale = std::max(1, scale);
    if (path == path_ && width == width_ && height == height_ && scale == scale_)
        return;
    path_ = path;
    width_ = width;
    height_ = height;
    scale_ = scale;
    LockWallpaperCache::get().prepare(path_, width_, height_, scale_, blur_);
    queue_draw();
}

void LockBackground::set_blur(double strength) {
    strength = std::clamp(strength, 0.0, 1.0);
    if (strength == blur_)
        return;
    blur_ = strength;
    LockWallpaperCache::get().prepare(path_, width_, height_, scale_, blur_);
    queue_draw();
}

void LockBackground::snapshot_vfunc(const Glib::RefPtr<Gtk::Snapshot>& snapshot) {
    const float width = static_cast<float>(get_width());
    const float height = static_cast<float>(get_height());
    if (width <= 0 || height <= 0)
        return;
    const graphene_rect_t full = GRAPHENE_RECT_INIT(0, 0, width, height);
    const GdkRGBA black{0.0f, 0.0f, 0.0f, 1.0f};
    gtk_snapshot_append_color(snapshot->gobj(), &black, &full);
    if (path_.empty())
        return;

    auto& cache = LockWallpaperCache::get();
    const double radius = LockWallpaperCache::blur_radius(blur_);
    if (radius > 0.0) {
        if (GdkTexture* blurred = cache.blurred(path_, width_, height_, scale_, blur_)) {
            gtk_snapshot_append_texture(snapshot->gobj(), blurred, &full);
            return;
        }
    }
    auto texture = cache.texture(path_, width_, height_, scale_);
    if (!texture)
        return; // still decoding: black this frame, redrawn on signal_changed
    const int tex_w = texture->get_width();
    const int tex_h = texture->get_height();
    if (radius <= 0.0) {
        const graphene_rect_t rect = cover_rect(width, height, 0.0f, tex_w, tex_h);
        gtk_snapshot_append_texture(snapshot->gobj(), texture->gobj(), &rect);
        return;
    }
    // no offscreen renderer: blur live (costly per frame, but correct)
    gtk_snapshot_push_blur(snapshot->gobj(), radius);
    const graphene_rect_t rect =
        cover_rect(width, height, static_cast<float>(2 * radius), tex_w, tex_h);
    gtk_snapshot_append_texture(snapshot->gobj(), texture->gobj(), &rect);
    gtk_snapshot_pop(snapshot->gobj());
}

} // namespace hyprshell
