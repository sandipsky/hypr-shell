#include "bar/modules/launcher.hpp"

#include <giomm.h>

namespace hyprshell {

namespace {

constexpr const char* kSearch = "\uEB1C"; // tabler search glyph

} // namespace

Launcher::Launcher() : Gtk::Box(Gtk::Orientation::HORIZONTAL, 0) {
    add_css_class("module");
    add_css_class("launcher");
    add_css_class("icon-only"); // round hover pill (bar.css)
    icon_.set_text(kSearch);
    icon_.add_css_class("icon");
    append(icon_);
    set_tooltip_text("Open launcher");

    auto click = Gtk::GestureClick::create();
    click->signal_released().connect([this](int, double, double) { open(); });
    add_controller(click);
}

void Launcher::open() {
    if (auto app = Gio::Application::get_default())
        app->activate_action("launcher");
}

} // namespace hyprshell
