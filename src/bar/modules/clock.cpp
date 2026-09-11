#include "bar/modules/clock.hpp"

#include "bar/bar_popover.hpp"

#include "services/config.hpp"

#include <string>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <ctime>

#include <sys/timerfd.h>
#include <unistd.h>

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
    click->signal_released().connect([this](int, double, double) { open(); });
    add_controller(click);

    // dev hook: HS_OPEN_CALENDAR=1 pops the calendar shortly after startup
    if (const char* hook = g_getenv("HS_OPEN_CALENDAR")) {
        const int delay = std::max(800, std::atoi(hook)); // >1 = delay in ms
        Glib::signal_timeout().connect_once(
            [this] {
                calendar_->reset_to_today();
                place_bar_popover(popover_);
                popover_.popup();
                // HS_CALENDAR_NAV=<delta> steps the month 1.5 s later (a
                // click on the arrows cannot be scripted)
                if (const char* nav = g_getenv("HS_CALENDAR_NAV")) {
                    const int delta = std::atoi(nav);
                    Glib::signal_timeout().connect_once(
                        [this, delta] { calendar_->navigate(delta); }, 1500);
                }
            },
            delay);
    }
}

void Clock::open() {
    calendar_->reset_to_today();
    place_bar_popover(popover_);
    popover_.popup();
}

Clock::~Clock() {
    minute_conn_.disconnect();
    if (minute_fd_ >= 0) close(minute_fd_);
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
    // GLib timeouts run on CLOCK_MONOTONIC, which stands still while the machine is
    // suspended: a "60 s until the next minute" timer armed before suspend still had
    // most of that minute left after resume, so the label showed the pre-suspend time
    // for up to a minute (the calendar recomputes on open and was right). A timerfd on
    // CLOCK_REALTIME armed for the ABSOLUTE next minute boundary keeps counting through
    // suspend and fires as soon as the system is back; TFD_TIMER_CANCEL_ON_SET makes a
    // stepped clock (NTP after resume, a manual date change) wake us too, so the label
    // never waits on a boundary that no longer exists.
    if (minute_fd_ < 0) {
        minute_fd_ = timerfd_create(CLOCK_REALTIME, TFD_NONBLOCK | TFD_CLOEXEC);
        if (minute_fd_ < 0) {
            g_warning("clock: timerfd_create failed (%s); falling back to a GLib timeout", g_strerror(errno));
            auto wait = 61 - static_cast<unsigned>(Glib::DateTime::create_now_local().get_second());
            Glib::signal_timeout().connect_seconds([this] {
                update();
                schedule_next_minute();
                return false;
            }, wait);
            return;
        }
        minute_conn_ = Glib::signal_io().connect(sigc::mem_fun(*this, &Clock::on_minute_timer),
                                                 minute_fd_, Glib::IOCondition::IO_IN);
    }

    timespec now{};
    clock_gettime(CLOCK_REALTIME, &now);
    itimerspec spec{};
    spec.it_value.tv_sec = now.tv_sec - now.tv_sec % 60 + 60; // next :00 in wall time
    spec.it_value.tv_nsec = 0;
    if (timerfd_settime(minute_fd_, TFD_TIMER_ABSTIME | TFD_TIMER_CANCEL_ON_SET, &spec, nullptr) < 0)
        g_warning("clock: timerfd_settime failed (%s)", g_strerror(errno));
}

bool Clock::on_minute_timer(Glib::IOCondition) {
    std::uint64_t expirations = 0;
    const ssize_t n = read(minute_fd_, &expirations, sizeof expirations);
    if (n < 0 && errno == EAGAIN) return true; // spurious wake-up, timer still armed
    // n > 0: the minute boundary passed (possibly during suspend);
    // n < 0 && errno == ECANCELED: the clock was set — the timer is disarmed, re-arm it
    update();
    schedule_next_minute();
    return true;
}

} // namespace hyprshell
