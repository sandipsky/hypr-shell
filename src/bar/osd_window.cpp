#include "bar/osd_window.hpp"

#include "services/brightness.hpp"
#include "services/config.hpp"
#include "services/lock_keys.hpp"
#include "services/pulse.hpp"

#include <gtk4-layer-shell.h>

#include <algorithm>
#include <cmath>

namespace hyprshell {

namespace {

// Noctalia's OSD constants
constexpr unsigned kAutoHideMs = 2000;  // osd.autoHideMs default
constexpr unsigned kShowDelayMs = 30;   // showDelayTimer: let the layout settle
constexpr double kAnimMs = 300.0;       // Style.animationNormal
constexpr double kMinScale = 0.85;
constexpr int kMargin = 9;              // Style.marginM from the screen edge
constexpr int kCardMargin = 13;         // marginM * 1.5 (room for the shadow)
constexpr int kLongHW = 320, kLongHH = 72;  // horizontal window
constexpr int kShortHW = 180;               // horizontal lock-key window
constexpr int kLongVW = 80, kLongVH = 280;  // vertical window
constexpr int kShortVH = 180;               // vertical lock-key window
constexpr int kBarThickness = 8;
constexpr double kEpsilon = 0.005;
// a fully transparent GTK4 window never commits a buffer (bar trigger gotcha)
constexpr double kMinOpacity = 0.01;

// noctalia-tabler-icons glyphs (Noctalia's OSD getIcon())
constexpr const char* kVolumeMuted = "\uF1C3"; // volume-off
constexpr const char* kVolumeZero = "\uEB50";  // volume-3
constexpr const char* kVolumeLow = "\uEB4F";   // volume-2
constexpr const char* kVolumeHigh = "\uEB51";  // volume
constexpr const char* kMic = "\uEAF0";         // microphone
constexpr const char* kMicOff = "\uED16";      // microphone-off
constexpr const char* kSunOff = "\uED63";      // sun-off
constexpr const char* kBrightLow = "\uFB23";   // brightness-down-filled
constexpr const char* kBrightHigh = "\uFB24";  // brightness-up-filled
constexpr const char* kKeyboard = "\uEBD6";    // keyboard

// the shared Noctalia color snapshot
constexpr const char* kStatePrimary = "primary"; // mPrimary #bfc2ff
constexpr const char* kStateError = "error";     // mError #ffb4ab
constexpr const char* kStateDim = "dim";         // mOnSurfaceVariant #c7c5d0
constexpr const char* kStateDefault = "";        // mOnSurface #e5e1e6

double ease_in_out_quad(double t) {
    return t < 0.5 ? 2.0 * t * t : 1.0 - std::pow(-2.0 * t + 2.0, 2) / 2.0;
}

} // namespace

OsdWindow::OsdWindow() {
    set_decorated(false);
    add_css_class("osd");

    // Layer-shell before mapping: overlay layer like Noctalia's default, no
    // keyboard interest; the default exclusive zone (0) keeps it out of the
    // bar's reserved strip so a top OSD sits under a top bar automatically.
    auto* window = GTK_WINDOW(gobj());
    gtk_layer_init_for_window(window);
    gtk_layer_set_namespace(window, "hypr-shell-osd");
    gtk_layer_set_layer(window, GTK_LAYER_SHELL_LAYER_OVERLAY);
    gtk_layer_set_keyboard_mode(window, GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);

    card_.add_css_class("osd-card");
    card_.set_overflow(Gtk::Overflow::VISIBLE);
    icon_.add_css_class("osd-icon");
    text_.add_css_class("osd-text");
    bar_.set_draw_func(sigc::mem_fun(*this, &OsdWindow::draw_bar));
    fixed_.put(card_, 0, 0);
    fixed_.set_can_target(false);
    set_child(fixed_);

    // display-only: an empty input region lets clicks fall through to
    // whatever is underneath (Noctalia's `mask: Region {}`)
    signal_map().connect([this] {
        if (auto surface = get_surface())
            surface->set_input_region(Cairo::Region::create());
    });

    Osd::get().signal_show().connect(sigc::mem_fun(*this, &OsdWindow::on_show_requested));
    Config::get().signal_changed().connect(sigc::mem_fun(*this, &OsdWindow::apply_config));
    apply_config();
}

void OsdWindow::apply_config() {
    const auto& oc = Config::get().osd();
    using L = Config::Osd::Location;

    // any geometry change happens unmapped (a mapped layer surface is not
    // resized; the next show maps it fresh)
    if (get_visible()) {
        show_delay_.disconnect();
        hide_timer_.disconnect();
        fading_ = false;
        set_visible(false);
    }

    auto* window = GTK_WINDOW(gobj());
    const bool top = oc.location == L::Top || oc.location == L::TopLeft ||
                     oc.location == L::TopRight;
    const bool bottom = oc.location == L::Bottom || oc.location == L::BottomLeft ||
                        oc.location == L::BottomRight;
    const bool left = oc.location == L::TopLeft || oc.location == L::BottomLeft ||
                      oc.location == L::Left;
    const bool right = oc.location == L::TopRight || oc.location == L::BottomRight ||
                       oc.location == L::Right;
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_TOP, top);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_BOTTOM, bottom);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_LEFT, left);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_RIGHT, right);
    for (auto edge : {GTK_LAYER_SHELL_EDGE_TOP, GTK_LAYER_SHELL_EDGE_BOTTOM,
                      GTK_LAYER_SHELL_EDGE_LEFT, GTK_LAYER_SHELL_EDGE_RIGHT})
        gtk_layer_set_margin(window, edge, kMargin);

    vertical_ = oc.vertical();
    win_w_ = vertical_ ? kLongVW : kLongHW;
    win_h_ = vertical_ ? kLongVH : kLongHH;
    fixed_.set_size_request(win_w_, win_h_);
    set_default_size(win_w_, win_h_);
    build_content();
}

void OsdWindow::build_content() {
    // detach the shared leaves, then re-parent them in the new arrangement
    for (Gtk::Widget* w : {static_cast<Gtk::Widget*>(&icon_), static_cast<Gtk::Widget*>(&text_),
                           static_cast<Gtk::Widget*>(&lock_chars_),
                           static_cast<Gtk::Widget*>(&bar_)})
        if (w->get_parent())
            w->unparent();
    if (content_.get_parent())
        card_.remove(content_);

    if (vertical_) {
        card_.add_css_class("vertical");
        text_.add_css_class("vertical");
        content_.set_orientation(Gtk::Orientation::VERTICAL);
        content_.set_spacing(6); // marginS
        // Noctalia's verticalContent: text, bar, icon (bottom)
        text_.set_halign(Gtk::Align::CENTER);
        text_.set_xalign(0.5f);
        text_.set_size_request(-1, 20);
        lock_chars_.set_halign(Gtk::Align::CENTER);
        bar_.set_content_width(kBarThickness);
        bar_.set_content_height(0);
        bar_.set_hexpand(false);
        bar_.set_vexpand(true);
        bar_.set_halign(Gtk::Align::CENTER);
        bar_.set_valign(Gtk::Align::FILL);
        icon_.set_halign(Gtk::Align::CENTER);
        icon_.set_valign(Gtk::Align::END);
        content_.append(text_);
        content_.append(lock_chars_);
        content_.append(bar_);
        content_.append(icon_);
    } else {
        card_.remove_css_class("vertical");
        text_.remove_css_class("vertical");
        content_.set_orientation(Gtk::Orientation::HORIZONTAL);
        content_.set_spacing(9); // marginM
        // Noctalia's horizontalContent: icon, bar (or lock text), percentage
        icon_.set_halign(Gtk::Align::START);
        icon_.set_valign(Gtk::Align::CENTER);
        text_.set_valign(Gtk::Align::CENTER);
        text_.set_size_request(-1, -1);
        bar_.set_content_height(kBarThickness);
        bar_.set_content_width(0);
        bar_.set_hexpand(true);
        bar_.set_vexpand(false);
        bar_.set_halign(Gtk::Align::FILL);
        bar_.set_valign(Gtk::Align::CENTER);
        content_.append(icon_);
        content_.append(bar_);
        content_.append(text_);
    }
    content_.set_hexpand(true);
    content_.set_vexpand(true);
    card_.append(content_);
}

void OsdWindow::set_state_class(Gtk::Widget& widget, const char* state) {
    for (const char* cls : {kStatePrimary, kStateError, kStateDim})
        widget.remove_css_class(cls);
    if (*state)
        widget.add_css_class(state);
}

void OsdWindow::refresh() {
    auto& pulse = Pulse::get();
    auto& osd = Osd::get();
    const bool lock = type_ == Osd::Type::LockKey;

    // value, icon and colors — Noctalia's getCurrentValue/getIcon/
    // getProgressColor/getIconColor
    double value = 0.0;
    const char* glyph = kKeyboard;
    const char* bar_state = kStatePrimary;
    const char* icon_state = kStateDefault;
    switch (type_) {
    case Osd::Type::Volume:
        value = pulse.muted() ? 0.0 : pulse.volume();
        glyph = pulse.muted()                 ? kVolumeMuted
                : pulse.volume() < kEpsilon   ? kVolumeZero
                : pulse.volume() <= 0.5       ? kVolumeLow
                                              : kVolumeHigh;
        if (pulse.muted())
            bar_state = icon_state = kStateError;
        break;
    case Osd::Type::InputVolume:
        value = pulse.input_muted() ? 0.0 : pulse.input_volume();
        glyph = pulse.input_muted() ? kMicOff : kMic;
        if (pulse.input_muted())
            bar_state = icon_state = kStateError;
        break;
    case Osd::Type::Brightness: {
        const double b = Brightness::get().fraction();
        value = b;
        glyph = b < kEpsilon ? kSunOff : b <= 0.5 ? kBrightLow : kBrightHigh;
        break;
    }
    case Osd::Type::LockKey:
        value = 1.0;
        bar_state = icon_state = osd.lock_key_on() ? kStatePrimary : kStateDim;
        break;
    }
    icon_.set_text(glyph);
    set_state_class(icon_, icon_state);
    // the vertical bar mode draws the icon one size smaller (fontSizeL)
    if (vertical_ && !lock)
        icon_.add_css_class("small");
    else
        icon_.remove_css_class("small");

    const int pct = static_cast<int>(std::lround(std::min(1.0, value) * 100.0));
    const std::string percent = std::to_string(pct) + "%";
    while (auto* child = lock_chars_.get_first_child())
        lock_chars_.remove(*child);
    bar_.set_visible(!lock);
    if (lock) {
        text_.remove_css_class("osd-percent");
        text_.add_css_class("osd-lock-text");
        set_state_class(text_, bar_state);
        if (vertical_) {
            // one character per row, like Noctalia's Repeater
            text_.set_visible(false);
            lock_chars_.set_visible(true);
            for (const char c : osd.lock_key_text()) {
                auto* label = Gtk::make_managed<Gtk::Label>(std::string(1, c));
                label->add_css_class("osd-text");
                label->set_size_request(-1, 17); // fontSizeS * 1.3
                set_state_class(*label, bar_state);
                lock_chars_.append(*label);
            }
        } else {
            lock_chars_.set_visible(false);
            text_.set_visible(true);
            text_.set_text(osd.lock_key_text());
            text_.set_hexpand(true);
            text_.set_xalign(0.5f);
            text_.set_width_chars(-1);
        }
    } else {
        lock_chars_.set_visible(false);
        text_.set_visible(true);
        text_.remove_css_class("osd-lock-text");
        text_.add_css_class("osd-percent");
        set_state_class(text_, kStateDefault);
        text_.set_text(percent);
        text_.set_hexpand(false);
        text_.set_width_chars(4); // "150%", Noctalia's percentageMetrics
        text_.set_xalign(vertical_ ? 0.5f : 1.0f);
    }

    // card size: fixed for the bar modes, content-sized (with Noctalia's
    // minimum) for lock keys — measured in-tree, which works before mapping
    card_.set_size_request(-1, -1);
    if (vertical_) {
        card_w_ = kLongVW - 2 * kCardMargin;
        card_h_ = kLongVH - 2 * kCardMargin;
        if (lock) {
            const auto m = card_.measure(Gtk::Orientation::VERTICAL, card_w_);
            card_h_ = std::clamp(m.sizes.natural, kShortVH - 2 * kCardMargin, card_h_);
        }
    } else {
        card_w_ = kLongHW - 2 * kCardMargin;
        card_h_ = kLongHH - 2 * kCardMargin;
        if (lock) {
            const auto m = card_.measure(Gtk::Orientation::HORIZONTAL, card_h_);
            card_w_ = std::clamp(m.sizes.natural, kShortHW - 2 * kCardMargin, card_w_);
        }
    }
    card_.set_size_request(card_w_, card_h_);

    // progress bar target (value + color), animated 300ms InOutQuad
    const Rgb color = bar_state == kStateError ? Rgb{1.0, 0.706, 0.671}
                      : bar_state == kStateDim ? Rgb{0.780, 0.773, 0.816}
                                               : Rgb{0.749, 0.761, 1.0};
    const double target = std::min(1.0, value);
    if (!bar_synced_ || !get_visible()) {
        bar_value_ = bar_to_ = target;
        bar_color_ = bar_color_to_ = color;
        bar_synced_ = true;
        bar_animating_ = false;
        bar_.queue_draw();
    } else if (target != bar_to_ || color.r != bar_color_to_.r || color.g != bar_color_to_.g ||
               color.b != bar_color_to_.b) {
        bar_from_ = bar_value_;
        bar_to_ = target;
        bar_color_from_ = bar_color_;
        bar_color_to_ = color;
        bar_start_us_ = 0;
        if (!bar_animating_) {
            bar_animating_ = true;
            bar_.add_tick_callback([this](const Glib::RefPtr<Gdk::FrameClock>& clock) {
                if (!bar_animating_)
                    return false;
                const gint64 now = clock->get_frame_time();
                if (bar_start_us_ == 0)
                    bar_start_us_ = now;
                const double t =
                    std::clamp((now - bar_start_us_) / (kAnimMs * 1000.0), 0.0, 1.0);
                const double e = ease_in_out_quad(t);
                bar_value_ = bar_from_ + (bar_to_ - bar_from_) * e;
                bar_color_ = {bar_color_from_.r + (bar_color_to_.r - bar_color_from_.r) * e,
                              bar_color_from_.g + (bar_color_to_.g - bar_color_from_.g) * e,
                              bar_color_from_.b + (bar_color_to_.b - bar_color_from_.b) * e};
                bar_.queue_draw();
                if (t < 1.0)
                    return true;
                bar_animating_ = false;
                return false;
            });
        }
    }
}

void OsdWindow::on_show_requested(Osd::Type type) {
    type_ = type;
    refresh();
    place_card();
    hide_timer_.disconnect();
    show_delay_.disconnect();

    if (!get_visible()) {
        opacity_ = kMinOpacity;
        scale_ = kMinScale;
        set_opacity(opacity_);
        place_card();
        present();
        // Noctalia waits 30ms after activation so the layout has settled
        show_delay_ = Glib::signal_timeout().connect(
            [this] {
                start_fade(true);
                return false;
            },
            kShowDelayMs);
        return;
    }
    start_fade(true); // re-shown mid-fade: reverse from the current values
}

void OsdWindow::start_fade(bool in) {
    fade_in_ = in;
    fade_from_opacity_ = opacity_;
    fade_from_scale_ = scale_;
    fade_start_us_ = 0;
    hide_timer_.disconnect();
    if (fading_)
        return;
    fading_ = true;
    add_tick_callback([this](const Glib::RefPtr<Gdk::FrameClock>& clock) {
        if (!fading_)
            return false;
        const gint64 now = clock->get_frame_time();
        if (fade_start_us_ == 0)
            fade_start_us_ = now;
        const double t = std::clamp((now - fade_start_us_) / (kAnimMs * 1000.0), 0.0, 1.0);
        const double e = ease_in_out_quad(t);
        const double to_opacity = fade_in_ ? 1.0 : kMinOpacity;
        const double to_scale = fade_in_ ? 1.0 : kMinScale;
        opacity_ = fade_from_opacity_ + (to_opacity - fade_from_opacity_) * e;
        scale_ = fade_from_scale_ + (to_scale - fade_from_scale_) * e;
        set_opacity(opacity_);
        place_card();
        if (t < 1.0)
            return true;
        fading_ = false;
        if (fade_in_) {
            hide_timer_ = Glib::signal_timeout().connect(
                [this] {
                    start_fade(false);
                    return false;
                },
                kAutoHideMs);
        } else {
            set_visible(false);
        }
        return false;
    });
}

void OsdWindow::place_card() {
    const auto& oc = Config::get().osd();
    using L = Config::Osd::Location;

    // the card hugs the anchored edges of the (fixed-size) window, like
    // Noctalia's window itself would, and scales about its own centre
    const bool anchored_left = oc.location == L::TopLeft || oc.location == L::BottomLeft ||
                               oc.location == L::Left;
    const bool anchored_right = oc.location == L::TopRight || oc.location == L::BottomRight ||
                                oc.location == L::Right;
    const bool anchored_top = oc.location == L::Top || oc.location == L::TopLeft ||
                              oc.location == L::TopRight;
    const bool anchored_bottom = oc.location == L::Bottom || oc.location == L::BottomLeft ||
                                 oc.location == L::BottomRight;
    const double x = anchored_left    ? kCardMargin
                     : anchored_right ? win_w_ - kCardMargin - card_w_
                                      : (win_w_ - card_w_) / 2.0;
    const double y = anchored_top      ? kCardMargin
                     : anchored_bottom ? win_h_ - kCardMargin - card_h_
                                       : (win_h_ - card_h_) / 2.0;
    graphene_point_t origin = {static_cast<float>(x + card_w_ * (1.0 - scale_) / 2.0),
                               static_cast<float>(y + card_h_ * (1.0 - scale_) / 2.0)};
    GskTransform* transform = gsk_transform_scale(gsk_transform_translate(nullptr, &origin),
                                                  static_cast<float>(scale_),
                                                  static_cast<float>(scale_));
    gtk_fixed_set_child_transform(GTK_FIXED(fixed_.gobj()), GTK_WIDGET(card_.gobj()), transform);
    gsk_transform_unref(transform);
}

void OsdWindow::draw_bar(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height) {
    const double radius = std::min(4.0, std::min(width, height) / 2.0);
    auto rounded = [&](double x, double y, double w, double h) {
        cr->begin_new_sub_path();
        cr->arc(x + w - radius, y + radius, radius, -M_PI / 2, 0);
        cr->arc(x + w - radius, y + h - radius, radius, 0, M_PI / 2);
        cr->arc(x + radius, y + h - radius, radius, M_PI / 2, M_PI);
        cr->arc(x + radius, y + radius, radius, M_PI, 3 * M_PI / 2);
        cr->close_path();
    };
    // track: mSurfaceVariant
    rounded(0, 0, width, height);
    cr->set_source_rgb(0.125, 0.122, 0.137); // #201f23
    cr->fill_preserve();
    cr->clip();
    // fill grows from the left, or from the bottom in the vertical column
    cr->set_source_rgb(bar_color_.r, bar_color_.g, bar_color_.b);
    const double v = std::clamp(bar_value_, 0.0, 1.0);
    if (vertical_)
        cr->rectangle(0, height * (1.0 - v), width, height * v);
    else
        cr->rectangle(0, 0, width * v, height);
    cr->fill();
}

} // namespace hyprshell
