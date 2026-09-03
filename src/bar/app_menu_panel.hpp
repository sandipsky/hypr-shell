#pragma once

#include "bar/session_menu.hpp"
#include "services/apps.hpp"

#include <gtkmm.h>

#include <functional>
#include <string>
#include <vector>

namespace hyprshell {

// App menu popover content: a search box (with optional settings and session
// buttons at its right) over a grid of application tiles — Noctalia's
// launcher grid view as a bar click panel instead of a fullscreen overlay.
// Typing filters (fuzzy over name / description / executable), arrows move
// the selection, Enter launches, Esc closes.
class AppMenuPanel : public Gtk::Box {
public:
    AppMenuPanel();
    ~AppMenuPanel() override;

    void set_open(bool open); // popover mapped state — resets search + focus
    void apply_config();      // grid columns, header buttons
    void show_session_menu(); // session button: dropdown, or the fullscreen window

    // Launching an app, opening settings or running a session action wants
    // the popover closed first.
    sigc::signal<void()>& signal_request_close() { return request_close_; }

private:
    void update_results();
    void rebuild_grid();
    void select(int index, bool scroll_into_view);
    void activate_index(int index);
    bool on_key_pressed(guint keyval, guint keycode, Gdk::ModifierType state);
    void run_after_close(std::function<void()> action);
    void focus_default(); // the search box, or the panel itself when it is hidden

    bool open_ = false;
    bool dirty_ = true; // app list / columns / label mode changed while closed
    int columns_ = 5;
    bool multiline_ = false;
    bool show_search_ = true;
    int tile_height_ = 0; // measured in rebuild_grid — every tile shares it

    Gtk::Box header_{Gtk::Orientation::HORIZONTAL, 6};
    Gtk::Entry search_;
    Gtk::Button settings_button_;
    Gtk::Button session_button_;
    Gtk::Popover session_popover_; // nested dropdown (session.mode = dropdown)
    SessionMenuList* session_list_ = nullptr;

    Gtk::Stack content_stack_; // grid scroller or the empty label, fixed height per open
    Gtk::ScrolledWindow scroller_;
    Gtk::Grid grid_;
    Gtk::Label empty_label_;

    std::vector<Apps::Entry> results_;
    std::vector<Gtk::Widget*> tiles_;
    int selected_ = -1;

    // Noctalia's ignoreMouseHover (see LauncherWindow): hovering only selects
    // once the mouse really moved after the panel opened.
    bool mouse_active_ = false;
    bool mouse_primed_ = false;
    double mouse_x_ = 0.0, mouse_y_ = 0.0;

    sigc::signal<void()> request_close_;
};

} // namespace hyprshell
