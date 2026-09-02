#include "bar/modules/launcher.hpp"

#include <giomm.h>

namespace hyprshell {

namespace {

constexpr const char* kSearch = "\uEB1C"; // tabler search glyph

} // namespace

Launcher::Launcher() : Gtk::Box(Gtk::Orientation::HORIZONTAL, 0) {
    add_css_class("module");
    add_css_class("launcher");
    icon_.set_text(kSearch);
    icon_.add_css_class("icon");
    append(icon_);
    set_tooltip_text("Open launcher");
    set_cursor(Gdk::Cursor::create("pointer"));

    auto click = Gtk::GestureClick::create();
    click->signal_released().connect([](int, double, double) {
        if (auto app = Gio::Application::get_default())
            app->activate_action("launcher");
    });
    add_controller(click);
}

} // namespace hyprshell
