#include "bar/bar.hpp"

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
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_TOP, TRUE);
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
}

} // namespace hyprshell
