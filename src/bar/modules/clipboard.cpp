#include "bar/modules/clipboard.hpp"

#include "services/clipboard.hpp"
#include "services/config.hpp"

#include <giomm.h>

namespace hyprshell {

namespace {

constexpr const char* kClipboard = "\uEA6F"; // tabler clipboard glyph

} // namespace

ClipboardModule::ClipboardModule() : Gtk::Box(Gtk::Orientation::HORIZONTAL, 0) {
    add_css_class("module");
    add_css_class("clipboard");
    icon_.set_text(kClipboard);
    icon_.add_css_class("icon");
    append(icon_);
    set_tooltip_text("Clipboard history");
    set_cursor(Gdk::Cursor::create("pointer"));

    auto click = Gtk::GestureClick::create();
    click->signal_released().connect([](int, double, double) {
        if (auto app = Gio::Application::get_default())
            app->activate_action("clipboard");
    });
    add_controller(click);

    Config::get().signal_changed().connect(sigc::mem_fun(*this, &ClipboardModule::update));
    update();
}

void ClipboardModule::update() {
    set_visible(Clipboard::get().enabled());
}

} // namespace hyprshell
