#include "bar/bar.hpp"

#include "services/config.hpp"
#include "services/hyprland.hpp"

#include <gtk4-layer-shell.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <optional>

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
    bar_motion->signal_enter().connect([this](double x, double y) {
        g_debug("bar: pointer enter at %.0f,%.0f", x, y);
        set_hovered(true);
    });
    bar_motion->signal_leave().connect([this] {
        g_debug("bar: pointer leave");
        set_hovered(false);
    });
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

    // Padding hit targets (user request, Windows' taskbar rule): the bar's
    // padding listens to nothing, so a primary click there is handed to a
    // module. Beside: a click within a module's span along the bar (the strip
    // above / below it on a horizontal bar) counts as a click on that module.
    // Corner: a click between the outermost module — the first visible one
    // in the start section, the last visible one in the end section, or the
    // centre section's first / last when that side is empty, on any bar
    // orientation — and the screen corner counts as a click on it, so the
    // pointer can be thrown into either corner without aiming. Modules opt
    // in through CornerTarget; the bubble phase leaves their own gestures
    // first. A right click reaches the module's secondary action the same way.
    auto corner_click = Gtk::GestureClick::create();
    corner_click->set_button(0);
    corner_click->set_propagation_phase(Gtk::PropagationPhase::BUBBLE);
    auto* corner_gesture = corner_click.get(); // raw: a captured RefPtr would cycle
    corner_click->signal_pressed().connect([this, corner_gesture](int, double x, double y) {
        if (hidden_)
            return;
        const guint button = corner_gesture->get_current_button();
        if (button != GDK_BUTTON_PRIMARY && button != GDK_BUTTON_SECONDARY)
            return;
        const bool secondary = button == GDK_BUTTON_SECONDARY;
        const bool vertical = Config::get().bar_vertical();
        struct Hit {
            CornerTarget* target;
            double x1, y1, x2, y2;
        };
        auto hit_for = [this](Gtk::Widget* widget) -> std::optional<Hit> {
            auto* target = dynamic_cast<CornerTarget*>(widget);
            if (!target)
                return std::nullopt;
            const auto bounds = widget->compute_bounds(*this);
            if (!bounds)
                return std::nullopt;
            return Hit{target, bounds->get_x(), bounds->get_y(),
                       bounds->get_x() + bounds->get_width(),
                       bounds->get_y() + bounds->get_height()};
        };

        // beside: the module whose span along the bar contains the click
        for (Gtk::Widget* box : {&start_box_, &center_box_, &end_box_}) {
            for (auto* w = box->get_first_child(); w; w = w->get_next_sibling()) {
                if (!w->get_visible())
                    continue;
                const auto hit = hit_for(w);
                if (!hit)
                    continue;
                if (x >= hit->x1 && x <= hit->x2 && y >= hit->y1 && y <= hit->y2)
                    return; // the module handles its own clicks
                const bool along = vertical ? (y >= hit->y1 && y <= hit->y2)
                                            : (x >= hit->x1 && x <= hit->x2);
                if (along) {
                    if (secondary)
                        hit->target->secondary_beside(x - hit->x1, y - hit->y1);
                    else
                        hit->target->activate_beside(x - hit->x1, y - hit->y1);
                    return;
                }
            }
        }

        // corner: the start corner belongs to the first module, the end corner to the last
        for (const bool start : {true, false}) {
            auto outermost = [start](Gtk::Widget& box) {
                Gtk::Widget* w = start ? box.get_first_child() : box.get_last_child();
                while (w && !w->get_visible())
                    w = start ? w->get_next_sibling() : w->get_prev_sibling();
                return w;
            };
            Gtk::Widget* widget = outermost(start ? start_box_ : end_box_);
            if (!widget) // empty section: the centre module is the outermost one
                widget = outermost(center_box_);
            const auto hit = hit_for(widget);
            if (!hit)
                continue;
            // Only the position along the bar decides: everything from the
            // module's far edge to the screen edge, across the bar's whole
            // thickness (the module is centred in the bar, so a click at the
            // corner pixel lies outside its own rows/columns).
            const bool in_corner = vertical ? (start ? y <= hit->y2 : y >= hit->y1)
                                            : (start ? x <= hit->x2 : x >= hit->x1);
            if (in_corner) {
                if (secondary)
                    hit->target->secondary_corner(start);
                else
                    hit->target->activate_corner(start);
                return;
            }
        }
    });
    add_controller(corner_click);

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
    Config::get().signal_changed().connect([this] {
        apply_config();
        refresh_workspace_empty();
    });
}

Bar::~Bar() {
    show_timer_.disconnect();
    hide_timer_.disconnect();
}

void Bar::toggle_app_menu() {
    // an auto-hidden bar slides back first; the open popover then keeps it up
    if (hidden_)
        peek();
    app_menu_.toggle();
}

void Bar::toggle_session_menu() {
    if (hidden_)
        peek();
    session_.toggle();
}

void Bar::toggle_control_center() {
    if (hidden_)
        peek();
    control_center_.toggle();
}

// gtk4-layer-shell fakes the compositor's configure to GTK with the
// FULLSCREEN state, so GDK pins the surface to that size: content that grows
// pushes it larger through the min-size constraint, content that shrinks
// (a smaller bar.density) leaves the bar at the old thickness with the
// workspace pills stretched. The library re-evaluates the size when the
// window's default size changes (notify::default-*), taking a 0 axis as the
// natural size — and any other value literally, so -1 ("unset") produced an
// invalid configure that dropped the compositor's span (the bar shrank to
// its natural length). Hence: after the style pass has run (an idle below
// GTK's layout priority, since it measures synchronously), set the
// thickness to the measured natural value and back to 0 — a real change
// each time, and the span axis stays 0 (compositor-configured).
void Bar::refresh_thickness() {
    Glib::signal_idle().connect_once(
        [this] {
            if (!get_mapped())
                return;
            const bool vertical = Config::get().bar_vertical();
            int minimum = 0, natural = 0, baseline_min = 0, baseline_nat = 0;
            measure(vertical ? Gtk::Orientation::HORIZONTAL : Gtk::Orientation::VERTICAL, -1,
                    minimum, natural, baseline_min, baseline_nat);
            if (natural <= 0)
                return;
            if (vertical) {
                set_default_size(natural, 0);
                set_default_size(0, 0);
            } else {
                set_default_size(0, natural);
                set_default_size(0, 0);
            }
        },
        Glib::PRIORITY_DEFAULT_IDLE);
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

    for (const char* name : {"bottom", "left", "right", "density-comfortable"})
        remove_css_class(name);
    // bar.density: bar.css redefines its size variables under this class
    if (cfg.bar_density() == Config::BarDensity::Comfortable)
        add_css_class("density-comfortable");
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

    // A thinner density must shrink the surface, which GTK never does on its
    // own (see refresh_thickness()). An orientation change re-anchors instead.
    if (get_mapped() && vertical == last_vertical_)
        refresh_thickness();
    last_vertical_ = vertical;

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
    auto& cfg = Config::get();
    if (!hypr.available() || cfg.bar_visibility() != Config::BarVisibility::AutoHide ||
        !cfg.bar_show_when_workspace_empty())
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
        if (empty)
            set_hidden(false); // hide_timer keeps polling and waits this out
        else if (!hovered_)
            schedule_hide(); // workspace occupied again — restart the cycle
    });
}

Gtk::Widget* Bar::module_widget(const std::string& name) {
    if (name == "launcher")
        return &launcher_;
    if (name == "app_menu")
        return &app_menu_;
    if (name == "workspaces")
        return &workspaces_;
    if (name == "taskbar")
        return &taskbar_;
    if (name == "active_window")
        return &active_window_;
    if (name == "network")
        return &network_;
    if (name == "bluetooth")
        return &bluetooth_;
    if (name == "control_center")
        return &control_center_;
    if (name == "cpu")
        return &cpu_;
    if (name == "memory")
        return &memory_;
    if (name == "disk")
        return &disk_;
    if (name == "volume")
        return &volume_;
    if (name == "battery")
        return &battery_;
    if (name == "clipboard")
        return &clipboard_;
    if (name == "notifications")
        return &notifications_;
    if (name == "clock")
        return &clock_;
    if (name == "session")
        return &session_;
    return nullptr;
}

} // namespace hyprshell
