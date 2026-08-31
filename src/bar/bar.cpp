#include "bar/bar.hpp"

#include "services/config.hpp"
#include "services/hyprland.hpp"

#include <gtk4-layer-shell.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace hyprshell {

namespace {

// Noctalia's auto-hide timings: autoHideDelay / autoShowDelay defaults and
// the ~200ms slide (animationNormal * 2/3).
constexpr unsigned kHideDelayMs = 500;
constexpr unsigned kShowDelayMs = 150;
constexpr double kSlideMs = 200.0;
constexpr int kSlideOvershootPx = 8; // clear the hairline border too

GtkLayerShellEdge bar_edge(Config::BarPosition position) {
    switch (position) {
    case Config::BarPosition::Bottom:
        return GTK_LAYER_SHELL_EDGE_BOTTOM;
    case Config::BarPosition::Left:
        return GTK_LAYER_SHELL_EDGE_LEFT;
    case Config::BarPosition::Right:
        return GTK_LAYER_SHELL_EDGE_RIGHT;
    case Config::BarPosition::Top:
        break;
    }
    return GTK_LAYER_SHELL_EDGE_TOP;
}

void anchor_to_bar_edge(GtkWindow* window, Config::BarPosition position, bool vertical) {
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_TOP,
                         position == Config::BarPosition::Top || vertical);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_BOTTOM,
                         position == Config::BarPosition::Bottom || vertical);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_LEFT,
                         position == Config::BarPosition::Left || !vertical);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_RIGHT,
                         position == Config::BarPosition::Right || !vertical);
}

bool any_popover_mapped(const Gtk::Widget* widget) {
    if (dynamic_cast<const Gtk::Popover*>(widget) && widget->get_mapped())
        return true;
    for (auto* child = widget->get_first_child(); child; child = child->get_next_sibling())
        if (any_popover_mapped(child))
            return true;
    return false;
}

} // namespace

Bar::Bar() {
    add_css_class("bar");
    set_decorated(false);

    // Layer-shell must be configured before the window is mapped.
    auto* window = GTK_WINDOW(gobj());
    gtk_layer_init_for_window(window);
    gtk_layer_set_namespace(window, "hypr-shell");
    gtk_layer_set_layer(window, GTK_LAYER_SHELL_LAYER_TOP);
    gtk_layer_auto_exclusive_zone_enable(window);

    // Invisible 1px strip on the bar's screen edge; hovering it re-reveals an
    // auto-hidden bar (the bar itself is off-screen then and can't be hovered).
    auto* tw = GTK_WINDOW(trigger_.gobj());
    gtk_layer_init_for_window(tw);
    gtk_layer_set_namespace(tw, "hypr-shell-trigger");
    gtk_layer_set_layer(tw, GTK_LAYER_SHELL_LAYER_TOP);
    trigger_.set_decorated(false);
    trigger_.add_css_class("bar-trigger");
    // without this the strip falls back to GtkWindow's 200px default size on
    // the unanchored axis and steals input from a wide slice of the screen
    trigger_.set_default_size(1, 1);
    trigger_fill_.set_size_request(1, 1);
    trigger_.set_child(trigger_fill_);

    auto bar_motion = Gtk::EventControllerMotion::create();
    bar_motion->signal_enter().connect([this](double, double) { set_hovered(true); });
    bar_motion->signal_leave().connect([this] { set_hovered(false); });
    add_controller(bar_motion);
    auto trigger_motion = Gtk::EventControllerMotion::create();
    trigger_motion->signal_enter().connect([this](double, double) { set_hovered(true); });
    trigger_motion->signal_leave().connect([this] { set_hovered(false); });
    trigger_.add_controller(trigger_motion);

    layout_.add_css_class("bar-inner");
    layout_.set_start_widget(start_box_);
    layout_.set_center_widget(center_box_);
    layout_.set_end_widget(end_box_);
    set_child(layout_);

    // Auto-hide needs to know about workspace switches (peek) and whether the
    // active workspace is empty (optionally keeps the bar visible).
    auto& hypr = Hyprland::get();
    hypr.signal_event().connect([this](const std::string& name, const std::string&) {
        if (name == "workspace") {
            auto& cfg = Config::get();
            if (cfg.bar_visibility() == Config::BarVisibility::AutoHide &&
                cfg.bar_show_on_workspace_switch())
                peek();
        }
        if (name == "workspace" || name == "openwindow" || name == "closewindow" ||
            name == "movewindow")
            refresh_workspace_empty();
    });
    refresh_workspace_empty();

    apply_config(); // anchors the top/bottom edge — still before mapping
    Config::get().signal_changed().connect(sigc::mem_fun(*this, &Bar::apply_config));
}

Bar::~Bar() {
    show_timer_.disconnect();
    hide_timer_.disconnect();
}

void Bar::apply_config() {
    auto& cfg = Config::get();
    auto* window = GTK_WINDOW(gobj());

    // The auto exclusive zone follows anchor changes, so flipping edges at
    // runtime keeps windows from underlapping the bar. A horizontal bar spans
    // left..right on its edge; a vertical one spans top..bottom.
    const auto position = cfg.bar_position();
    const bool vertical = cfg.bar_vertical();
    anchor_to_bar_edge(window, position, vertical);
    anchor_to_bar_edge(GTK_WINDOW(trigger_.gobj()), position, vertical);

    for (const char* name : {"bottom", "left", "right"})
        remove_css_class(name);
    switch (position) {
    case Config::BarPosition::Bottom:
        add_css_class("bottom");
        break;
    case Config::BarPosition::Left:
        add_css_class("left");
        break;
    case Config::BarPosition::Right:
        add_css_class("right");
        break;
    case Config::BarPosition::Top:
        break;
    }

    const auto orientation =
        vertical ? Gtk::Orientation::VERTICAL : Gtk::Orientation::HORIZONTAL;
    layout_.set_orientation(orientation);
    start_box_.set_orientation(orientation);
    center_box_.set_orientation(orientation);
    end_box_.set_orientation(orientation);

    // Rebuild the three sections from bar.layout. A disabled module is simply
    // not parented — modules stay free to set_visible() for their own service
    // availability without fighting the config.
    Gtk::Box* boxes[] = {&start_box_, &center_box_, &end_box_};
    for (auto* box : boxes)
        while (auto* child = box->get_first_child())
            box->remove(*child);
    for (std::size_t i = 0; i < 3; ++i)
        for (const auto& name : cfg.bar_layout(static_cast<Config::BarSection>(i)))
            if (cfg.module_enabled(name))
                if (auto* widget = module_widget(name)) {
                    // a vertical bar's width comes from its widest module —
                    // center the rest instead of leaving them edge-aligned
                    widget->set_halign(vertical ? Gtk::Align::CENTER : Gtk::Align::FILL);
                    boxes[i]->append(*widget);
                }

    // Visibility (Noctalia's displayMode). Auto-hide overlays windows instead
    // of reserving space, and starts a hide cycle unless the pointer is on the
    // bar — so enabling it from settings hides after the usual delay.
    const auto visibility = cfg.bar_visibility();
    const bool auto_hide = visibility == Config::BarVisibility::AutoHide;
    if (auto_hide)
        gtk_layer_set_exclusive_zone(window, 0); // also disables the auto zone
    else
        gtk_layer_auto_exclusive_zone_enable(window);
    trigger_.set_visible(auto_hide);
    set_visible(visibility != Config::BarVisibility::Hidden);
    if (auto_hide) {
        if (!hovered_ && !hidden_)
            schedule_hide();
    } else {
        show_timer_.disconnect();
        hide_timer_.disconnect();
        set_hidden(false);
    }
}

// -- auto-hide ---------------------------------------------------------------

void Bar::set_hovered(bool hovered) {
    hovered_ = hovered;
    if (Config::get().bar_visibility() != Config::BarVisibility::AutoHide)
        return;
    if (hovered) {
        hide_timer_.disconnect();
        if (hidden_)
            schedule_show();
    } else {
        show_timer_.disconnect();
        schedule_hide();
    }
}

void Bar::schedule_show() {
    show_timer_.disconnect();
    show_timer_ = Glib::signal_timeout().connect(
        [this] {
            if (hovered_)
                set_hidden(false);
            return false;
        },
        kShowDelayMs);
}

void Bar::schedule_hide() {
    hide_timer_.disconnect();
    hide_timer_ = Glib::signal_timeout().connect(
        [this] {
            if (Config::get().bar_visibility() != Config::BarVisibility::AutoHide ||
                hovered_)
                return false;
            // an open popover (calendar) or an empty workspace keeps the bar
            // up — keep checking until that changes
            if (popover_open() || must_stay_visible())
                return true;
            set_hidden(true);
            return false;
        },
        kHideDelayMs);
}

void Bar::peek() {
    set_hidden(false);
    if (!hovered_)
        schedule_hide();
}

bool Bar::popover_open() const {
    return any_popover_mapped(this);
}

bool Bar::must_stay_visible() const {
    return Config::get().bar_show_when_workspace_empty() && workspace_empty_;
}

void Bar::set_hidden(bool hidden) {
    if (hidden_ == hidden)
        return;
    hidden_ = hidden;
    anim_from_ = hide_progress_;
    anim_start_us_ = 0;
    if (anim_running_)
        return; // the running tick picks up the new direction
    anim_running_ = true;
    add_tick_callback([this](const Glib::RefPtr<Gdk::FrameClock>& clock) {
        const gint64 now = clock->get_frame_time();
        if (anim_start_us_ == 0)
            anim_start_us_ = now;
        double t = std::clamp((now - anim_start_us_) / (kSlideMs * 1000.0), 0.0, 1.0);
        // Noctalia: ease-in (quad) when hiding, ease-out (cubic) when revealing
        const double eased = hidden_ ? t * t : 1.0 - std::pow(1.0 - t, 3);
        hide_progress_ = anim_from_ + ((hidden_ ? 1.0 : 0.0) - anim_from_) * eased;
        apply_slide();
        if (t < 1.0)
            return true;
        anim_running_ = false;
        return false;
    });
}

void Bar::apply_slide() {
    // Slide off-screen by giving the anchored edge a negative margin. The
    // window stays mapped (no unmap/remap latency on reveal) but is entirely
    // outside the output, so it receives no input while hidden.
    auto& cfg = Config::get();
    const int size = cfg.bar_vertical() ? get_width() : get_height();
    const int offset =
        -static_cast<int>(std::lround(hide_progress_ * (size + kSlideOvershootPx)));
    auto* window = GTK_WINDOW(gobj());
    const auto edge = bar_edge(cfg.bar_position());
    for (auto e : {GTK_LAYER_SHELL_EDGE_TOP, GTK_LAYER_SHELL_EDGE_BOTTOM,
                   GTK_LAYER_SHELL_EDGE_LEFT, GTK_LAYER_SHELL_EDGE_RIGHT})
        gtk_layer_set_margin(window, e, e == edge ? offset : 0);
}

void Bar::refresh_workspace_empty() {
    auto& hypr = Hyprland::get();
    if (!hypr.available())
        return;
    const unsigned serial = ++ws_serial_;
    hypr.request("j/activeworkspace", [this, serial](const std::string& reply) {
        if (serial != ws_serial_)
            return;
        bool empty = false;
        try {
            empty = nlohmann::json::parse(reply).value("windows", 0) == 0;
        } catch (const std::exception&) {
            return;
        }
        if (empty == workspace_empty_)
            return;
        workspace_empty_ = empty;
        auto& cfg = Config::get();
        if (cfg.bar_visibility() != Config::BarVisibility::AutoHide ||
            !cfg.bar_show_when_workspace_empty())
            return;
        if (empty)
            set_hidden(false); // hide_timer keeps polling and waits this out
        else if (!hovered_)
            schedule_hide(); // workspace occupied again — restart the cycle
    });
}

Gtk::Widget* Bar::module_widget(const std::string& name) {
    if (name == "workspaces")
        return &workspaces_;
    if (name == "active_window")
        return &active_window_;
    if (name == "network")
        return &network_;
    if (name == "volume")
        return &volume_;
    if (name == "battery")
        return &battery_;
    if (name == "clock")
        return &clock_;
    return nullptr;
}

} // namespace hyprshell
