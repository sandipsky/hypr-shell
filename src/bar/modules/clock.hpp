#pragma once

#include <gtkmm.h>

namespace hyprshell {

class Clock : public Gtk::Label {
public:
    Clock();

private:
    void update();
    void schedule_next_minute();
};

} // namespace hyprshell
