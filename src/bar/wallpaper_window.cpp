#include "bar/wallpaper_window.hpp"

#include "services/config.hpp"
#include "services/wallpaper.hpp"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gtk4-layer-shell.h>

#include <algorithm>
#include <cmath>
#include <random>

namespace hyprshell {

namespace {

constexpr double kPi = 3.14159265358979323846;

double random_unit() {
    static std::mt19937 rng{std::random_device{}()};
    static std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(rng);
}

// Noctalia's NumberAnimation easing for the wallpaper progress
double ease_in_out_cubic(double t) {
    return t < 0.5 ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3) / 2;
}

WallpaperTextureCache::Fit fit_for(Config::Wallpaper::FillMode mode) {
    using F = Config::Wallpaper::FillMode;
    switch (mode) {
    case F::Crop:
        return WallpaperTextureCache::Fit::Cover;
    case F::Fit:
        return WallpaperTextureCache::Fit::Contain;
    case F::Stretch:
        return WallpaperTextureCache::Fit::Exact;
    case F::Center:
    case F::Repeat:
        return WallpaperTextureCache::Fit::Native;
    }
    return WallpaperTextureCache::Fit::Cover;
}

GskColorStop stop(float offset, float alpha) {
    return GskColorStop{offset, GdkRGBA{1.0f, 1.0f, 1.0f, alpha}};
}

} // namespace

// ---------------------------------------------------------------------------
// texture cache

struct WallpaperTextureCache::Pending {
    std::shared_ptr<bool> alive;
    Key key;
};

WallpaperTextureCache& WallpaperTextureCache::get() {
    static WallpaperTextureCache instance;
    return instance;
}

void WallpaperTextureCache::prepare(const std::string& path, int pixel_w, int pixel_h, Fit fit) {
    if (path.empty() || pixel_w <= 0 || pixel_h <= 0)
        return;
    const Key key{path, pixel_w, pixel_h, fit};
    auto& entry = entries_[key];
    if (!entry.texture && !entry.loading && !entry.failed)
        load(key, entry);
}

Glib::RefPtr<Gdk::Texture> WallpaperTextureCache::texture(const std::string& path, int pixel_w,
                                                          int pixel_h, Fit fit) {
    const auto it = entries_.find(Key{path, pixel_w, pixel_h, fit});
    return it == entries_.end() ? Glib::RefPtr<Gdk::Texture>() : it->second.texture;
}

void WallpaperTextureCache::retain(const std::vector<std::string>& paths) {
    for (auto it = entries_.begin(); it != entries_.end();) {
        const bool keep = std::find(paths.begin(), paths.end(), std::get<0>(it->first)) != paths.end();
        if (keep) {
            ++it;
            continue;
        }
        if (it->second.cancellable)
            it->second.cancellable->cancel();
        it = entries_.erase(it);
    }
}

// Header first (cheap, synchronous) to pick the decode size, then the pixel
// data asynchronously — the same shape as the lock screen's cache.
void WallpaperTextureCache::load(const Key& key, Entry& entry) {
    const auto& [path, pixel_w, pixel_h, fit] = key;
    int image_w = 0, image_h = 0;
    if (gdk_pixbuf_get_file_info(path.c_str(), &image_w, &image_h) == nullptr || image_w <= 0 ||
        image_h <= 0) {
        g_warning("wallpaper: cannot read image %s", path.c_str());
        entry.failed = true;
        return;
    }
    int target_w = -1, target_h = -1;
    bool preserve_aspect = true;
    const double image_ratio = static_cast<double>(image_w) / image_h;
    const double screen_ratio = static_cast<double>(pixel_w) / pixel_h;
    switch (fit) {
    case Fit::Cover:
        // decode the axis that covers, never above the image's own size
        if (image_ratio > screen_ratio)
            target_h = std::min(image_h, pixel_h);
        else
            target_w = std::min(image_w, pixel_w);
        break;
    case Fit::Contain:
        if (image_ratio > screen_ratio)
            target_w = std::min(image_w, pixel_w);
        else
            target_h = std::min(image_h, pixel_h);
        break;
    case Fit::Exact:
        target_w = pixel_w;
        target_h = pixel_h;
        preserve_aspect = false;
        break;
    case Fit::Native: {
        // 1:1 pixels (center / repeat), capped at twice the screen to bound memory
        const int cap = 2 * std::max(pixel_w, pixel_h);
        if (image_w > cap || image_h > cap) {
            if (image_w >= image_h)
                target_w = cap;
            else
                target_h = cap;
        } else {
            target_w = image_w;
            target_h = image_h;
        }
        break;
    }
    }

    entry.loading = true;
    entry.cancellable = Gio::Cancellable::create();
    auto cancellable = entry.cancellable;
    auto file = Gio::File::create_for_path(path);
    file->read_async(
        [this, file, key, target_w, target_h, preserve_aspect, cancellable,
         alive = alive_](Glib::RefPtr<Gio::AsyncResult>& result) {
            if (!*alive)
                return;
            Glib::RefPtr<Gio::FileInputStream> stream;
            try {
                stream = file->read_finish(result);
            } catch (const Glib::Error& e) {
                g_warning("wallpaper: cannot open %s: %s", std::get<0>(key).c_str(), e.what());
                if (auto it = entries_.find(key); it != entries_.end()) {
                    it->second.loading = false;
                    it->second.failed = true;
                }
                return;
            }
            auto* pending = new Pending{alive, key};
            gdk_pixbuf_new_from_stream_at_scale_async(
                G_INPUT_STREAM(stream->gobj()), target_w, target_h, preserve_aspect,
                cancellable->gobj(),
                [](GObject*, GAsyncResult* async_result, gpointer data) {
                    std::unique_ptr<Pending> pending(static_cast<Pending*>(data));
                    GError* error = nullptr;
                    GdkPixbuf* pixbuf = gdk_pixbuf_new_from_stream_finish(async_result, &error);
                    if (!*pending->alive) {
                        g_clear_object(&pixbuf);
                        g_clear_error(&error);
                        return;
                    }
                    auto& cache = WallpaperTextureCache::get();
                    auto it = cache.entries_.find(pending->key);
                    if (it == cache.entries_.end()) {
                        g_clear_object(&pixbuf);
                        g_clear_error(&error);
                        return;
                    }
                    it->second.loading = false;
                    if (pixbuf == nullptr) {
                        if (error != nullptr &&
                            !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                            g_warning("wallpaper: image decode failed: %s", error->message);
                        it->second.failed = true;
                        g_clear_error(&error);
                        return;
                    }
                    // EXIF orientation like Noctalia's -auto-orient
                    GdkPixbuf* oriented = gdk_pixbuf_apply_embedded_orientation(pixbuf);
                    g_object_unref(pixbuf);
                    auto wrapped = Glib::wrap(oriented, /*take_copy=*/true);
                    it->second.texture = Gdk::Texture::create_for_pixbuf(wrapped);
                    g_object_unref(oriented);
                    cache.ready_.emit();
                },
                pending);
        },
        cancellable);
}

// ---------------------------------------------------------------------------
// view

WallpaperView::WallpaperView() {
    set_can_target(false);
    set_focusable(false);
    ready_connection_ = WallpaperTextureCache::get().signal_ready().connect([this] {
        if (waiting_for_texture_ && texture_for(current_)) {
            waiting_for_texture_ = false;
            start_animation();
        }
        queue_draw();
    });
}

WallpaperView::~WallpaperView() {
    ready_connection_.disconnect();
}

WallpaperView::Transition WallpaperView::transition_from_key(const std::string& key) {
    if (key == "fade")
        return Transition::Fade;
    if (key == "wipe")
        return Transition::Wipe;
    if (key == "disc")
        return Transition::Disc;
    if (key == "stripes")
        return Transition::Stripes;
    return Transition::None;
}

void WallpaperView::set_monitor_size(int width, int height, int scale) {
    width_ = width;
    height_ = height;
    scale_ = std::max(1, scale);
    prepare(previous_);
    prepare(current_);
    queue_draw();
}

void WallpaperView::prepare(const std::string& path) {
    if (path.empty() || width_ <= 0 || height_ <= 0)
        return;
    WallpaperTextureCache::get().prepare(path, width_ * scale_, height_ * scale_,
                                         fit_for(Config::get().wallpaper().fill_mode));
}

Glib::RefPtr<Gdk::Texture> WallpaperView::texture_for(const std::string& path) {
    if (path.empty() || width_ <= 0 || height_ <= 0)
        return {};
    return WallpaperTextureCache::get().texture(path, width_ * scale_, height_ * scale_,
                                                fit_for(Config::get().wallpaper().fill_mode));
}

void WallpaperView::show_image(const std::string& path, Transition transition, bool startup) {
    if (path == current_ && !animating_ && !waiting_for_texture_)
        return;
    // an interrupted transition snaps to its end (Noctalia promotes next → current)
    previous_ = current_;
    current_ = path;
    animating_ = false;
    waiting_for_texture_ = false;
    progress_ = 1.0;
    transition_ = transition;
    prepare(current_);

    if (transition_ == Transition::None) {
        previous_.clear();
        queue_draw();
        return;
    }
    // Background.qml's per-change rolls (startup: fixed centered disc)
    wipe_direction_ = random_unit() * 4.0;
    disc_cx_ = startup ? 0.5 : random_unit();
    disc_cy_ = startup ? 0.5 : random_unit();
    stripes_count_ = static_cast<int>(std::round(random_unit() * 20 + 4));
    stripes_angle_ = random_unit() * 360.0;

    if (texture_for(current_))
        start_animation();
    else
        waiting_for_texture_ = true; // decoding — the ready signal starts it
    queue_draw();
}

void WallpaperView::start_animation() {
    animating_ = true;
    start_us_ = 0;
    progress_ = 0.0;
    dumped_ = 0;
    add_tick_callback([this](const Glib::RefPtr<Gdk::FrameClock>& clock) {
        if (!animating_)
            return false;
        const gint64 now = clock->get_frame_time();
        if (start_us_ == 0)
            start_us_ = now;
        const double duration_us = Config::get().wallpaper().transition_duration_ms * 1000.0;
        const double t = std::clamp((now - start_us_) / duration_us, 0.0, 1.0);
        progress_ = ease_in_out_cubic(t);
        queue_draw();
        // dev hook: dump the frame as progress passes 25/50/75 %
        if (const char* dir = g_getenv("HS_WALLPAPER_DUMP")) {
            static const double kThresholds[] = {0.25, 0.5, 0.75};
            if (dumped_ < 3 && progress_ >= kThresholds[dumped_]) {
                dump_frame(std::string(dir) + "/frame-" +
                           std::to_string(static_cast<int>(kThresholds[dumped_] * 100)) + ".png");
                ++dumped_;
            }
        }
        if (t < 1.0)
            return true;
        animating_ = false;
        previous_.clear();
        WallpaperTextureCache::get().retain({current_});
        return false;
    });
}

// One texture node placed per fill mode (Noctalia's calculateUV), on black.
void WallpaperView::draw_image(const Glib::RefPtr<Gtk::Snapshot>& snapshot,
                               const Glib::RefPtr<Gdk::Texture>& texture, double w, double h) {
    const graphene_rect_t full = GRAPHENE_RECT_INIT(0, 0, static_cast<float>(w), static_cast<float>(h));
    const GdkRGBA black{0, 0, 0, 1};
    gtk_snapshot_append_color(snapshot->gobj(), &black, &full);
    if (!texture)
        return;
    const double tw = texture->get_width();
    const double th = texture->get_height();
    using F = Config::Wallpaper::FillMode;
    graphene_rect_t rect = full;
    switch (Config::get().wallpaper().fill_mode) {
    case F::Crop: {
        const double f = std::max(w / tw, h / th);
        rect = GRAPHENE_RECT_INIT(static_cast<float>((w - tw * f) / 2), static_cast<float>((h - th * f) / 2),
                                  static_cast<float>(tw * f), static_cast<float>(th * f));
        break;
    }
    case F::Fit: {
        const double f = std::min(w / tw, h / th);
        rect = GRAPHENE_RECT_INIT(static_cast<float>((w - tw * f) / 2), static_cast<float>((h - th * f) / 2),
                                  static_cast<float>(tw * f), static_cast<float>(th * f));
        break;
    }
    case F::Stretch:
        break;
    case F::Center: {
        const double iw = tw / scale_, ih = th / scale_; // 1:1 device pixels
        rect = GRAPHENE_RECT_INIT(static_cast<float>((w - iw) / 2), static_cast<float>((h - ih) / 2),
                                  static_cast<float>(iw), static_cast<float>(ih));
        break;
    }
    case F::Repeat: {
        const graphene_rect_t tile = GRAPHENE_RECT_INIT(0, 0, static_cast<float>(tw / scale_),
                                                        static_cast<float>(th / scale_));
        gtk_snapshot_push_repeat(snapshot->gobj(), &full, &tile);
        gtk_snapshot_append_texture(snapshot->gobj(), texture->gobj(), &tile);
        gtk_snapshot_pop(snapshot->gobj());
        return;
    }
    }
    gtk_snapshot_push_clip(snapshot->gobj(), &full);
    gtk_snapshot_append_scaled_texture(snapshot->gobj(), texture->gobj(), GSK_SCALING_FILTER_TRILINEAR,
                                       &rect);
    gtk_snapshot_pop(snapshot->gobj());
}

void WallpaperView::snapshot_vfunc(const Glib::RefPtr<Gtk::Snapshot>& snapshot) {
    const double w = get_width();
    const double h = get_height();
    if (w <= 0 || h <= 0)
        return;
    auto* gs = snapshot->gobj();
    auto new_texture = texture_for(current_);
    auto old_texture = texture_for(previous_);

    if (!animating_ || progress_ >= 1.0) {
        // hold the previous image until the new one has decoded
        draw_image(snapshot, new_texture ? new_texture : old_texture, w, h);
        return;
    }
    const double p = progress_;
    const double s = Config::get().wallpaper().edge_smoothness;
    const graphene_rect_t full = GRAPHENE_RECT_INIT(0, 0, static_cast<float>(w), static_cast<float>(h));

    if (transition_ == Transition::Fade) {
        gtk_snapshot_push_cross_fade(gs, p);
        draw_image(snapshot, old_texture, w, h);
        gtk_snapshot_pop(gs);
        draw_image(snapshot, new_texture, w, h);
        gtk_snapshot_pop(gs);
        return;
    }

    // old image below, new image through an alpha mask shaped by the transition
    draw_image(snapshot, old_texture, w, h);
    gtk_snapshot_push_mask(gs, GSK_MASK_MODE_ALPHA);
    switch (transition_) {
    case Transition::Wipe: {
        // wp_wipe.frag: soft edge m, progress extended so the band clears the screen
        const double m = 0.001 + 0.499 * s * s;
        const double e = p * (1 + 2 * m) - m;
        const bool horizontal = wipe_direction_ < 1.5;
        // new image occupies the side the shader's mix() gives it
        const bool new_below_edge =
            (wipe_direction_ >= 0.5 && wipe_direction_ < 1.5) || wipe_direction_ >= 2.5;
        const double edge = new_below_edge ? e : 1 - e;
        const double len = horizontal ? w : h;
        const float a = static_cast<float>((edge - m) * len);
        const float b = static_cast<float>((edge + m) * len);
        const graphene_point_t start = horizontal ? GRAPHENE_POINT_INIT(a, 0) : GRAPHENE_POINT_INIT(0, a);
        const graphene_point_t end = horizontal ? GRAPHENE_POINT_INIT(b, 0) : GRAPHENE_POINT_INIT(0, b);
        const GskColorStop stops[] = {stop(0, new_below_edge ? 1.0f : 0.0f),
                                      stop(1, new_below_edge ? 0.0f : 1.0f)};
        gtk_snapshot_append_linear_gradient(gs, &full, &start, &end, stops, 2);
        break;
    }
    case Transition::Disc: {
        // wp_disc.frag in aspect-corrected units (1 = screen height)
        const double A = w / h;
        const double m = (0.001 + 0.499 * s * s) * std::max(1.0, A);
        const double max_dx = std::max(disc_cx_ * A, (1 - disc_cx_) * A);
        const double max_dy = std::max(disc_cy_, 1 - disc_cy_);
        const double max_dist = std::hypot(max_dx, max_dy);
        const double radius = -m + p * (max_dist + 2 * m);
        const double outer = (radius + m) * h;
        if (outer <= 0) {
            const GdkRGBA clear{0, 0, 0, 0};
            gtk_snapshot_append_color(gs, &clear, &full);
            break;
        }
        const graphene_point_t center =
            GRAPHENE_POINT_INIT(static_cast<float>(disc_cx_ * w), static_cast<float>(disc_cy_ * h));
        const float start = static_cast<float>(std::max(0.0, (radius - m) * h / outer));
        const GskColorStop stops[] = {stop(0, 1.0f), stop(1, 0.0f)};
        gtk_snapshot_append_radial_gradient(gs, &full, &center, static_cast<float>(outer),
                                            static_cast<float>(outer), start, 1.0f, stops, 2);
        break;
    }
    case Transition::Stripes: {
        // wp_stripes.frag: bands along (cosA, sinA) in unit-square coordinates,
        // even bands sweep one way, odd bands the other, with a 10% wave lag
        const double m = 0.001 + 0.299 * s * s;
        const double a = stripes_angle_ * kPi / 180.0;
        const double ca = std::cos(a), sa = std::sin(a);
        double min_s = 1e9, max_s = -1e9, min_p = 1e9, max_p = -1e9;
        for (auto [x, y] : {std::pair{0.0, 0.0}, std::pair{1.0, 0.0}, std::pair{0.0, 1.0},
                            std::pair{1.0, 1.0}}) {
            const double sc = x * ca + y * sa;
            const double pc = -x * sa + y * ca;
            min_s = std::min(min_s, sc);
            max_s = std::max(max_s, sc);
            min_p = std::min(min_p, pc);
            max_p = std::max(max_p, pc);
        }
        const int n = std::max(1, stripes_count_);
        const double range = max_p - min_p;
        gtk_snapshot_save(gs);
        gtk_snapshot_scale(gs, static_cast<float>(w), static_cast<float>(h));
        gtk_snapshot_rotate(gs, static_cast<float>(stripes_angle_));
        const int first = static_cast<int>(std::floor(min_s * n)) - 1;
        const int last = static_cast<int>(std::ceil(max_s * n)) + 1;
        for (int i = first; i <= last; ++i) {
            const double delay = std::clamp((i + 0.5) / n, 0.0, 1.0) * 0.1;
            const double sp = std::clamp((p - delay) / 0.9, 0.0, 1.0);
            const bool odd = (i % 2 + 2) % 2 == 1;
            const double edge = odd ? max_p + m - sp * (range + 2 * m) : min_p - m + sp * (range + 2 * m);
            const graphene_rect_t band = GRAPHENE_RECT_INIT(
                static_cast<float>(static_cast<double>(i) / n), static_cast<float>(min_p - 0.01),
                static_cast<float>(1.0 / n), static_cast<float>(range + 0.02));
            const graphene_point_t start = GRAPHENE_POINT_INIT(0, static_cast<float>(edge - m));
            const graphene_point_t end = GRAPHENE_POINT_INIT(0, static_cast<float>(edge + m));
            const GskColorStop stops[] = {stop(0, odd ? 0.0f : 1.0f), stop(1, odd ? 1.0f : 0.0f)};
            gtk_snapshot_append_linear_gradient(gs, &band, &start, &end, stops, 2);
        }
        gtk_snapshot_restore(gs);
        break;
    }
    case Transition::Fade:
    case Transition::None:
        break;
    }
    gtk_snapshot_pop(gs); // mask → source
    draw_image(snapshot, new_texture, w, h);
    gtk_snapshot_pop(gs);
}

// Offscreen render of the view's current frame — the desktop is usually
// covered by windows, so this is how the transitions get checked.
void WallpaperView::dump_frame(const std::string& png_path) {
    GskRenderer* renderer = gsk_gl_renderer_new();
    GError* error = nullptr;
    if (!gsk_renderer_realize_for_display(renderer, gdk_display_get_default(), &error)) {
        g_warning("wallpaper dump: no renderer: %s", error ? error->message : "?");
        g_clear_error(&error);
        g_object_unref(renderer);
        return;
    }
    auto snap = Gtk::Snapshot::create();
    snapshot_vfunc(snap);
    GskRenderNode* node = gtk_snapshot_to_node(snap->gobj());
    if (node == nullptr) {
        g_warning("wallpaper dump: empty frame");
    } else {
        const graphene_rect_t viewport =
            GRAPHENE_RECT_INIT(0, 0, static_cast<float>(get_width()), static_cast<float>(get_height()));
        GdkTexture* texture = gsk_renderer_render_texture(renderer, node, &viewport);
        if (texture != nullptr) {
            gdk_texture_save_to_png(texture, png_path.c_str());
            g_message("wallpaper dump: %s (%dx%d, progress %.2f, transition %d)", png_path.c_str(),
                      get_width(), get_height(), progress_, static_cast<int>(transition_));
            g_object_unref(texture);
        }
        gsk_render_node_unref(node);
    }
    gsk_renderer_unrealize(renderer);
    g_object_unref(renderer);
}

// ---------------------------------------------------------------------------
// window

WallpaperWindow::WallpaperWindow(const Glib::RefPtr<Gdk::Monitor>& monitor) : monitor_(monitor) {
    set_decorated(false);
    add_css_class("wallpaper");

    auto* window = GTK_WINDOW(gobj());
    gtk_layer_init_for_window(window);
    gtk_layer_set_namespace(window, "hypr-shell-wallpaper");
    gtk_layer_set_layer(window, GTK_LAYER_SHELL_LAYER_BACKGROUND);
    gtk_layer_set_keyboard_mode(window, GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
    gtk_layer_set_exclusive_zone(window, -1); // ignore other layers' zones: cover everything
    for (auto edge : {GTK_LAYER_SHELL_EDGE_TOP, GTK_LAYER_SHELL_EDGE_BOTTOM, GTK_LAYER_SHELL_EDGE_LEFT,
                      GTK_LAYER_SHELL_EDGE_RIGHT})
        gtk_layer_set_anchor(window, edge, true);
    if (monitor_)
        gtk_layer_set_monitor(window, monitor_->gobj());

    set_child(view_);
    apply_geometry();
    if (monitor_)
        monitor_->property_geometry().signal_changed().connect(
            sigc::mem_fun(*this, &WallpaperWindow::apply_geometry));
}

WallpaperWindow::~WallpaperWindow() = default;

void WallpaperWindow::apply_geometry() {
    if (!monitor_)
        return;
    Gdk::Rectangle geometry;
    monitor_->get_geometry(geometry);
    view_.set_monitor_size(geometry.get_width(), geometry.get_height(),
                           std::max(1, monitor_->get_scale_factor()));
}

// ---------------------------------------------------------------------------
// manager

WallpaperManager::WallpaperManager(Gtk::Application& app) : app_(app) {
    auto display = Gdk::Display::get_default();
    if (auto monitors = display->get_monitors())
        monitors->signal_items_changed().connect(
            [this](guint, guint, guint) { sync_monitors(); });
    Wallpaper::get().signal_current_changed().connect(
        sigc::mem_fun(*this, &WallpaperManager::on_current_changed));
    Config::get().signal_changed().connect(sigc::mem_fun(*this, &WallpaperManager::apply_config));
    fill_key_ = std::to_string(static_cast<int>(Config::get().wallpaper().fill_mode));
    sync_monitors();
    // Noctalia's startup transition, 100ms after mapping so the compositor has
    // the layer surface
    Glib::signal_timeout().connect_once(
        [this] {
            started_ = true;
            if (!Wallpaper::get().current().empty())
                show_all(pick_transition(), /*startup=*/true);
        },
        100);
    // dev hook: HS_WALLPAPER_DUMP=<dir> writes frame-25/50/75.png as the
    // first transition passes those progress marks (see the tick callback)
    // and frame-final.png 4s after startup; pair with
    // HS_WALLPAPER_TRANSITION=fade|wipe|disc|stripes to force one.
    if (const char* dir = g_getenv("HS_WALLPAPER_DUMP")) {
        const std::string out = dir;
        Glib::signal_timeout().connect_once(
            [this, out] {
                if (!windows_.empty())
                    windows_.front()->view().dump_frame(out + "/frame-final.png");
            },
            4000);
    }
}

WallpaperManager::~WallpaperManager() = default;

void WallpaperManager::sync_monitors() {
    auto monitors = Gdk::Display::get_default()->get_monitors();
    std::vector<Glib::RefPtr<Gdk::Monitor>> present;
    if (monitors)
        for (guint i = 0; i < monitors->get_n_items(); ++i)
            if (auto monitor = std::dynamic_pointer_cast<Gdk::Monitor>(monitors->get_object(i)))
                present.push_back(monitor);
    // drop windows of unplugged monitors
    std::erase_if(windows_, [&](const std::unique_ptr<WallpaperWindow>& window) {
        return std::find(present.begin(), present.end(), window->monitor()) == present.end();
    });
    for (const auto& monitor : present) {
        const bool have = std::any_of(windows_.begin(), windows_.end(),
                                      [&](const auto& window) { return window->monitor() == monitor; });
        if (have)
            continue;
        auto window = std::make_unique<WallpaperWindow>(monitor);
        app_.add_window(*window);
        const auto& current = Wallpaper::get().current();
        // before the startup transition the view stays black so it can
        // animate the first image in (Noctalia's startup transition)
        if (started_ && !current.empty())
            window->view().show_image(current, WallpaperView::Transition::None);
        if (!current.empty())
            window->present();
        windows_.push_back(std::move(window));
    }
}

WallpaperView::Transition WallpaperManager::pick_transition() const {
    const auto& cfg = Config::get().wallpaper();
    if (!cfg.transitions_enabled)
        return WallpaperView::Transition::None;
    // Noctalia picks uniformly among the selected types; the two shader-only
    // ones (pixelate, honeycomb) are not rendered here and are skipped
    std::vector<WallpaperView::Transition> candidates;
    for (const auto& key : cfg.transitions) {
        const auto t = WallpaperView::transition_from_key(key);
        if (t != WallpaperView::Transition::None)
            candidates.push_back(t);
    }
    if (const char* forced = g_getenv("HS_WALLPAPER_TRANSITION")) // dev hook
        return WallpaperView::transition_from_key(forced);
    if (candidates.empty())
        return cfg.transitions.empty() ? WallpaperView::Transition::None : WallpaperView::Transition::Fade;
    return candidates[static_cast<std::size_t>(random_unit() * candidates.size()) % candidates.size()];
}

void WallpaperManager::show_all(WallpaperView::Transition transition, bool startup) {
    const auto& current = Wallpaper::get().current();
    for (auto& window : windows_) {
        window->view().show_image(current, transition, startup);
        if (!current.empty() && !window->is_visible())
            window->present();
    }
}

void WallpaperManager::on_current_changed(const std::string&, bool animate) {
    show_all(animate ? pick_transition() : WallpaperView::Transition::None, false);
}

void WallpaperManager::apply_config() {
    const auto& cfg = Config::get().wallpaper();
    const auto fill_key = std::to_string(static_cast<int>(cfg.fill_mode));
    if (fill_key != fill_key_) {
        fill_key_ = fill_key;
        // the new fill mode needs its own decode; the view holds the old
        // texture until it lands
        for (auto& window : windows_)
            window->view().refresh();
    }
}

} // namespace hyprshell
