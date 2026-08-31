#pragma once

#include "bar/battery_panel.hpp"

#include <gtkmm.h>

namespace hyprshell {

// Win11-style battery: Segoe Fluent glyphs, two stacked labels. The bottom
// label is the decile fill glyph (Battery1..Battery10); when plugged in, it is
// tinted green and the BatteryCharging0 frame (outline + bolt, transparent
// interior) is drawn over it so only the level bars show the tint. Ported from
// the user's Noctalia fork (NBatteryWin11.qml).
class Battery : public Gtk::Box {
public:
    Battery();
    ~Battery() override;

private:
    void update();

    Gtk::Overlay overlay_;
    Gtk::Label fill_;
    Gtk::Label frame_;
    Gtk::Popover popover_; // click opens the battery panel
    BatteryPanel* panel_ = nullptr;
};

} // namespace hyprshell
