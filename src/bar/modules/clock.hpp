#pragma once

#include "bar/calendar.hpp"

#include "bar/modules/corner_target.hpp"

#include <gtkmm.h>

namespace hyprshell {

class Clock : public Gtk::Label, public CornerTarget {
public:
    void open();
    void activate_corner(bool) override { open(); }
    Clock();
    ~Clock() override;

private:
    void update();
    void schedule_next_minute();
    bool on_minute_timer(Glib::IOCondition);

    Gtk::Popover popover_;
    Calendar* calendar_ = nullptr;
    // CLOCK_REALTIME timerfd armed for the next minute boundary (see schedule_next_minute)
    int minute_fd_ = -1;
    sigc::connection minute_conn_;
};

} // namespace hyprshell
