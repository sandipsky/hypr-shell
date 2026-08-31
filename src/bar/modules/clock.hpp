#pragma once

#include "bar/calendar.hpp"

#include <gtkmm.h>

namespace hyprshell {

class Clock : public Gtk::Label {
public:
    Clock();
    ~Clock() override;

private:
    void update();
    void schedule_next_minute();

    Gtk::Popover popover_;
    Calendar* calendar_ = nullptr;
};

} // namespace hyprshell
