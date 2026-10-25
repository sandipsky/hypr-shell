#include "bar/idle_fade.hpp"

#include "services/config.hpp"
#include "services/idle.hpp"

#include <gtk4-layer-shell.h>

#include <algorithm>

namespace hyprshell {

namespace {

// A fully transparent GTK4 window never commits a real buffer (see the bar
// trigger gotcha) — start just above zero.
constexpr double kStartOpacity = 0.01;

} // namespace

IdleFade::IdleFade() {
    set_decorated(false);
    add_css_class("idle-fade");

    // Layer-shell before mapping: overlay above everything, no keyboard
    // interest (focus stays with the app), covering the whole output.
    auto* window = GTK_WINDOW(gobj());
    gtk_layer_init_for_window(window);
    gtk_layer_set_namespace(window, "hypr-shell-fade");
    gtk_layer_set_layer(window, GTK_LAYER_SHELL_LAYER_OVERLAY);
    gtk_layer_set_keyboard_mode(window, GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
    gtk_layer_set_exclusive_zone(window, -1);
    for (auto edge : {GTK_LAYER_SHELL_EDGE_TOP, GTK_LAYER_SHELL_EDGE_BOTTOM,
                      GTK_LAYER_SHELL_EDGE_LEFT, GTK_LAYER_SHELL_EDGE_RIGHT})
        gtk_layer_set_anchor(window, edge, true);

    fill_.set_can_target(false);
    set_child(fill_);

    Idle::get().signal_fade_changed().connect(sigc::mem_fun(*this, &IdleFade::update));
}

void IdleFade::update() {
    if (Idle::get().fade_pending() == Idle::Stage::None) {
        animating_ = false;
        set_visible(false);
        return;
    }
    if (!get_visible())
        start_fade();
}

void IdleFade::start_fade() {
    set_opacity(kStartOpacity);
    present();
    start_us_ = 0;
    if (animating_)
        return;
    animating_ = true;
    add_tick_callback([this](const Glib::RefPtr<Gdk::FrameClock>& clock) {
        if (!animating_)
            return false;
        const gint64 now = clock->get_frame_time();
        if (start_us_ == 0)
            start_us_ = now;
        const double duration_us =
            std::max(1, Config::get().idle().fade_duration) * 1000000.0;
        const double t = std::clamp((now - start_us_) / duration_us, 0.0, 1.0);
        set_opacity(kStartOpacity + (1.0 - kStartOpacity) * t * t); // Noctalia: InQuad
        if (t < 1.0)
            return true;
        animating_ = false;
        return false;
    });
}

} // namespace hyprshell
