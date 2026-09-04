#pragma once

#include "bar/control_center_panel.hpp"

#include <gtkmm.h>

namespace hyprshell {

// Control center bar button (Noctalia's ControlCenter widget: the noctalia
// glyph in a round button). Click opens the control center panel.
class ControlCenter : public Gtk::Box {
public:
    ControlCenter();
    ~ControlCenter() override;

    void toggle();

private:
    Gtk::Label icon_;
    Gtk::Popover popover_;
    ControlCenterPanel* panel_ = nullptr;
};

} // namespace hyprshell
