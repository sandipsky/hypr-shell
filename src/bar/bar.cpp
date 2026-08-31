#include "bar/bar.hpp"

#include "services/config.hpp"

#include <gtk4-layer-shell.h>

namespace hyprshell {

Bar::Bar() {
    add_css_class("bar");
    set_decorated(false);

    // Layer-shell must be configured before the window is mapped.
    auto* window = GTK_WINDOW(gobj());
    gtk_layer_init_for_window(window);
    gtk_layer_set_namespace(window, "hypr-shell");
    gtk_layer_set_layer(window, GTK_LAYER_SHELL_LAYER_TOP);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
    gtk_layer_auto_exclusive_zone_enable(window);

    layout_.add_css_class("bar-inner");
    layout_.set_start_widget(workspaces_);
    layout_.set_center_widget(active_window_);
    end_box_.append(network_);
    end_box_.append(volume_);
    end_box_.append(battery_);
    end_box_.append(clock_);
    layout_.set_end_widget(end_box_);
    set_child(layout_);

    apply_config(); // anchors the top/bottom edge — still before mapping
    Config::get().signal_changed().connect(sigc::mem_fun(*this, &Bar::apply_config));
}

void Bar::apply_config() {
    auto& cfg = Config::get();
    auto* window = GTK_WINDOW(gobj());

    // The auto exclusive zone follows anchor changes, so flipping edges at
    // runtime keeps windows from underlapping the bar.
    const bool top = cfg.bar_position() == Config::BarPosition::Top;
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_TOP, top);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_BOTTOM, !top);
    if (top)
        remove_css_class("bottom");
    else
        add_css_class("bottom");

    layout_.set_size_request(-1, cfg.bar_height() > 0 ? cfg.bar_height() : -1);

    workspaces_.set_visible(cfg.module_enabled("workspaces"));
    active_window_.set_visible(cfg.module_enabled("active_window"));
    network_.set_visible(cfg.module_enabled("network"));
    volume_.set_visible(cfg.module_enabled("volume"));
    battery_.set_visible(cfg.module_enabled("battery"));
    clock_.set_visible(cfg.module_enabled("clock"));
}

} // namespace hyprshell
