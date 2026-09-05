#include "bar/modules/clock.hpp"

#include "bar/bar_popover.hpp"

#include "services/config.hpp"

#include <string>

#include <algorithm>
#include <cstdlib>

namespace hyprshell {

Clock::Clock() {
    add_css_class("module");
    add_css_class("clock");
    set_justify(Gtk::Justification::CENTER);
    update();
    schedule_next_minute();
    Config::get().signal_changed().connect(sigc::mem_fun(*this, &Clock::update));

    calendar_ = Gtk::make_managed<Calendar>();
    popover_.set_child(*calendar_);
    popover_.set_parent(*this);
    popover_.set_has_arrow(false);
    popover_.add_css_class("calendar-popover");

    auto click = Gtk::GestureClick::create();
    click->signal_released().connect([this](int, double, double) {
        calendar_->reset_to_today();
        place_bar_popover(popover_);
        popover_.popup();
    });
    add_controller(click);

    // dev hook: HS_OPEN_CALENDAR=1 pops the calendar shortly after startup
    if (const char* hook = g_getenv("HS_OPEN_CALENDAR")) {
        const int delay = std::max(800, std::atoi(hook)); // >1 = delay in ms
        Glib::signal_timeout().connect_once(
            [this] {
                calendar_->reset_to_today();
                place_bar_popover(popover_);
                popover_.popup();
            },
            delay);
    }
}

Clock::~Clock() {
    popover_.unparent();
}

void Clock::update() {
    auto& cfg = Config::get();
    const bool vertical = cfg.bar_vertical();
    const auto& format = vertical ? cfg.clock_format_vertical() : cfg.clock_format_horizontal();

    Glib::ustring text;
    try {
        text = Glib::DateTime::create_now_local().format(format);
    } catch (const Glib::Error&) {
        // half-typed format string from the settings app — fall back
        text = Glib::DateTime::create_now_local().format("%H:%M");
    }
    if (vertical) {
        // Noctalia semantics: the vertical format's space-separated tokens stack
        std::string stacked = text;
        for (auto& c : stacked)
            if (c == ' ')
                c = '\n';
        text = stacked;
    }
    set_label(text);

    // tooltip: the full date by default (`bar.clock.tooltip_format`, strftime;
    // empty disables it); an invalid format shows nothing rather than garbage
    Glib::ustring tooltip;
    if (!cfg.clock_tooltip_format().empty()) {
        try {
            tooltip = Glib::DateTime::create_now_local().format(cfg.clock_tooltip_format());
        } catch (const Glib::Error&) {
        }
    }
    set_tooltip_text(tooltip);
    set_has_tooltip(!tooltip.empty());
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
