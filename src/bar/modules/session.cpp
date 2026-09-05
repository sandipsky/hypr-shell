#include "bar/modules/session.hpp"

#include "bar/bar_popover.hpp"

#include "services/config.hpp"
#include "services/session.hpp"

#include <giomm.h>

namespace hyprshell {

namespace {

constexpr const char* kPower = "\uEB0D"; // tabler power glyph (\u escape — never literal PUA)

} // namespace

Session::Session() : Gtk::Box(Gtk::Orientation::HORIZONTAL, 0) {
    add_css_class("module");
    add_css_class("session");
    icon_.set_text(kPower);
    icon_.add_css_class("icon");
    append(icon_);
    set_tooltip_text("Session");
    set_cursor(Gdk::Cursor::create("pointer"));

    // anchored to the icon label, never the module Box (see battery.cpp)
    list_ = Gtk::make_managed<SessionMenuList>();
    popover_.set_child(*list_);
    popover_.set_parent(icon_);
    popover_.set_has_arrow(false);
    popover_.add_css_class("session-popover");
    list_->signal_activate().connect([this](const SessionAction& action) {
        // close first so the action's own UI (lock screen etc.) can take over
        popover_.popdown();
        const SessionAction* act = &action; // static table, see session_actions.hpp
        Glib::signal_idle().connect_once([act] { run_session_action(*act); });
    });

    auto click = Gtk::GestureClick::create();
    click->signal_released().connect([this](int, double, double) { open(); });
    add_controller(click);
}

Session::~Session() {
    popover_.unparent();
}

void Session::toggle() {
    if (popover_.get_visible()) {
        popover_.popdown();
        return;
    }
    if (!get_mapped()) {
        g_message("session menu: not shown — module disabled or bar hidden");
        return;
    }
    open();
}

void Session::open() {
    if (Config::get().session().mode == Config::Session::Mode::Fullscreen) {
        if (auto app = Gio::Application::get_default())
            app->activate_action("session"); // the App owns the fullscreen window
        return;
    }
    // keep the dropdown on the free side of the bar
    place_bar_popover(popover_);
    popover_.popup();
}

} // namespace hyprshell
