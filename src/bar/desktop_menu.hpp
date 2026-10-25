#pragma once

#include "services/desktop_menu_items.hpp"

#include <giomm.h>
#include <gtkmm.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace hyprshell {

// Right-click menu on the desktop (the wallpaper window): Apps ▸ / Settings ▸
// / Next desktop background / Session ▸ / Keybindings, per `desktop_menu.*`
// (order, per-item toggles, icons). A Gtk::Popover hung off the click point;
// the submenus are nested popovers opening to the right on hover or click.
// Hyprland draws layer-surface popups above every window, so the menu shows
// on top even though the wallpaper sits at the very bottom of the stack.
class DesktopMenu {
public:
    // `anchor` is the wallpaper window's content (a Gtk::Overlay); popovers
    // parent to it and the click point is in its coordinates.
    explicit DesktopMenu(Gtk::Widget& anchor);
    ~DesktopMenu();

    void open(double x, double y);
    void close();
    bool is_open() const { return popover_.get_visible(); }

    // dev hook (HS_DESKTOP_MENU_SUBMENU): open a submenu row by item key
    void open_submenu(const std::string& key);

private:
    struct Row {
        std::string key;
        Gtk::Button* button = nullptr;
        Gtk::Popover* submenu = nullptr; // null for plain actions
    };

    void rebuild();
    Gtk::Button* make_row(const std::string& label, Gtk::Widget* icon, bool has_submenu,
                          bool destructive);
    Gtk::Widget* make_glyph(const char* glyph);
    Gtk::Widget* make_page_icon(const char* icon); // settings page icon: theme name or "glyph:"
    void build_apps_submenu();
    void build_settings_submenu();
    void build_session_submenu();
    void show_submenu(Row& row);
    void hide_submenus();
    void on_row_hover(Row* row);
    void run_after_close(std::function<void()> action);
    void refresh_app_icons();
    void setup_submenu(Gtk::Popover& popover, Gtk::Widget& child);

    Gtk::Widget& anchor_;
    Gtk::Popover popover_;
    Gtk::Overlay content_;
    Gtk::Box list_{Gtk::Orientation::VERTICAL, 2};
    Gtk::Popover apps_popover_, settings_popover_, session_popover_;
    Gtk::ScrolledWindow apps_scroller_;
    Gtk::Box apps_list_{Gtk::Orientation::VERTICAL, 2};
    Gtk::Box settings_list_{Gtk::Orientation::VERTICAL, 2};
    Gtk::Box session_list_{Gtk::Orientation::VERTICAL, 2};
    std::vector<Row> rows_;
    Row* open_row_ = nullptr;
    Row* pending_row_ = nullptr;
    sigc::connection hover_timer_;
    std::vector<std::pair<Gtk::Image*, Glib::RefPtr<Gio::Icon>>> pending_icons_;
    sigc::connection icons_connection_;
};

} // namespace hyprshell
