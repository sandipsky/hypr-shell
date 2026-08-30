#include "bar/modules/clock.hpp"

namespace hyprshell {

Clock::Clock() {
    add_css_class("module");
    add_css_class("clock");
    update();
    schedule_next_minute();
}

void Clock::update() {
    set_label(Glib::DateTime::create_now_local().format("%a %e %b  %H:%M"));
}

void Clock::schedule_next_minute() {
    // fire ~1s past the minute boundary so the minute has always rolled over
    auto wait = 61 - static_cast<unsigned>(Glib::DateTime::create_now_local().get_second());
    Glib::signal_timeout().connect_seconds([this] {
        update();
        schedule_next_minute();
        return false;
    }, wait);
}

} // namespace hyprshell
