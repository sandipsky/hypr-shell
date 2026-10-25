#pragma once

#include "services/keybinds.hpp"

#include <gtkmm.h>

#include <string>
#include <vector>

namespace hyprshell {

// Keyboard shortcuts overlay: Hyprland's key bindings in the launcher's
// design — a fullscreen dimmed overlay with a centred panel holding a title,
// a filter entry and the bindings grouped under the config's own section
// comments, each as keycaps + what it does (services/keybinds.hpp). Opened
// from the desktop menu, the app's "keybindings" action or
// `hypr-shell --keybindings`; Escape or a click outside closes it.
class KeybindingsWindow : public Gtk::ApplicationWindow {
public:
    KeybindingsWindow();

    void toggle();
    void open();
    void close_window();

private:
    struct RowWidget {
        Gtk::Widget* widget = nullptr;
        std::size_t bind = 0; // index into binds_
    };
    struct GroupWidget {
        Gtk::Widget* header = nullptr;
        std::vector<RowWidget> rows;
    };

    void refresh();
    void rebuild_rows();
    void apply_filter();
    Gtk::Widget* make_row(const Keybind& bind);
    bool on_key_pressed(guint keyval, guint keycode, Gdk::ModifierType state);
    void scroll_by(double delta);

    Gtk::Overlay overlay_;
    Gtk::Box backdrop_;
    Gtk::Box panel_{Gtk::Orientation::VERTICAL, 12};
    Gtk::Label title_;
    Gtk::Label subtitle_;
    Gtk::Entry search_;
    Gtk::ScrolledWindow scroller_;
    Gtk::Box list_{Gtk::Orientation::VERTICAL, 0};
    Gtk::Label status_;
    Gtk::Separator divider_{Gtk::Orientation::HORIZONTAL};
    Gtk::Label count_label_;

    std::vector<Keybind> binds_;
    std::vector<GroupWidget> groups_;
    unsigned fetch_serial_ = 0; // a stale fetch (closed + reopened quickly) is ignored
    bool loading_ = false;
};

} // namespace hyprshell
