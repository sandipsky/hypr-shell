#pragma once

#include "bar/bluetooth_panel.hpp"

#include "bar/modules/corner_target.hpp"

#include <gtkmm.h>

namespace hyprshell {

// Bluetooth status icon (tabler glyphs): off / on / device connected, like
// Noctalia's bar widget. Click opens the bluetooth panel; hidden when BlueZ
// (or an adapter) is missing.
class Bluetooth : public Gtk::Box, public CornerTarget {
public:
    void open();
    void activate_corner(bool) override { open(); }
    Bluetooth();
    ~Bluetooth() override;

private:
    void update();

    Gtk::Label icon_;
    Gtk::Popover popover_;
    BluetoothPanel* panel_ = nullptr;
};

} // namespace hyprshell
