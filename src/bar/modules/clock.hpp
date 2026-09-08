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

    Gtk::Popover popover_;
    Calendar* calendar_ = nullptr;
};

} // namespace hyprshell
