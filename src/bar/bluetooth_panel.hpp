#pragma once

#include <gtkmm.h>

namespace hyprshell {

// Bluetooth popover content: header with a power switch (Noctalia's panel
// header, minus its settings/close/auto-connect buttons — user request;
// auto-connect lives in hypr-shell-settings), then Connected / Paired /
// Available device lists. Unlike Noctalia (which pairs in its settings
// window), discovery runs while this popover is open and unpaired devices
// can be paired right here.
class BluetoothPanel : public Gtk::Box {
public:
    BluetoothPanel();

    void set_open(bool open); // popover mapped state — drives discovery

private:
    void rebuild();

    bool updating_ = false;
    bool open_ = false;

    // header
    Gtk::Label header_icon_;
    Gtk::Switch power_switch_;

    // shown instead of the lists while the adapter is off (Noctalia look)
    Gtk::Box disabled_card_{Gtk::Orientation::VERTICAL, 6};

    Gtk::ScrolledWindow scroller_;
    Gtk::Box list_{Gtk::Orientation::VERTICAL, 6};
    Gtk::Label status_; // pair/connect errors, pinned under the list
};

} // namespace hyprshell
