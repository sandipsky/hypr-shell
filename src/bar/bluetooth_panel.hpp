#pragma once

#include "bar/busy_indicator.hpp"

#include <gtkmm.h>

namespace hyprshell {

// Bluetooth popover content: header with a power switch (Noctalia's panel
// header, minus its settings/close/auto-connect buttons — user request;
// auto-connect lives in hypr-shell-settings), then Connected / Paired /
// Available device lists. Unlike Noctalia (which pairs in its settings
// window), discovery runs while this popover is open and unpaired devices
// can be paired right here. A header refresh button forgets cached unpaired
// devices and restarts discovery; it shows a spinner while discovering (no
// "Scanning…" text, user request).
class BluetoothPanel : public Gtk::Box {
public:
    BluetoothPanel();

    void set_open(bool open); // popover mapped state — drives discovery

private:
    void rebuild();
    void update_busy(); // header spinner from Bluez::scanning()

    bool updating_ = false;
    bool open_ = false;

    // header
    Gtk::Label header_icon_;
    Gtk::Button refresh_btn_; // restart discovery; spinner while discovering
    Gtk::Stack refresh_stack_;
    Gtk::Label refresh_icon_;
    BusyIndicator refresh_spinner_;
    Gtk::Switch power_switch_;

    // shown instead of the lists while the adapter is off (Noctalia look)
    Gtk::Box disabled_card_{Gtk::Orientation::VERTICAL, 6};

    Gtk::ScrolledWindow scroller_;
    Gtk::Box list_{Gtk::Orientation::VERTICAL, 6};
    Gtk::Label status_; // pair/connect errors, pinned under the list
};

} // namespace hyprshell
