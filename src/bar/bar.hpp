#pragma once

#include "bar/modules/active_window.hpp"
#include "bar/modules/battery.hpp"
#include "bar/modules/clock.hpp"
#include "bar/modules/network.hpp"
#include "bar/modules/volume.hpp"
#include "bar/modules/workspaces.hpp"

#include <gtkmm.h>

namespace hyprshell {

class Bar : public Gtk::ApplicationWindow {
public:
    Bar();

private:
    void apply_config();

    Gtk::CenterBox layout_;
    Workspaces workspaces_;
    ActiveWindow active_window_;
    Gtk::Box end_box_{Gtk::Orientation::HORIZONTAL, 0};
    Network network_;
    Volume volume_;
    Battery battery_;
    Clock clock_;
};

} // namespace hyprshell
