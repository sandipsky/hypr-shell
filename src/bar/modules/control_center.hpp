#pragma once

#include "bar/control_center_panel.hpp"

#include "bar/modules/corner_target.hpp"

#include <gtkmm.h>

namespace hyprshell {

// Control center bar button (Noctalia's ControlCenter widget: the noctalia
// glyph in a round button). Click opens the control center panel.
class ControlCenter : public Gtk::Box, public CornerTarget {
public:
    void activate_corner(bool) override { toggle(); }
    ControlCenter();
    ~ControlCenter() override;

    void toggle();

private:
    Gtk::Label icon_;
    Gtk::Popover popover_;
    ControlCenterPanel* panel_ = nullptr;
};

} // namespace hyprshell
