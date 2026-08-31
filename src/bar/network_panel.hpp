#pragma once

#include "services/network_manager.hpp"

#include <gtkmm.h>

namespace hyprshell {

// Network popover content, following Noctalia's Wi-Fi panel: header with a
// wifi radio switch, the connected network card (Disconnect), and a scrolled
// "Available networks" list with per-network Connect. Connecting to a new
// secured network reveals an inline password card first.
class NetworkPanel : public Gtk::Box {
public:
    NetworkPanel();

    void refresh(); // rescan when the popover opens

private:
    void rebuild_networks();
    void update_state();
    void update_connected(); // fed by NM DBus (fast) and the scan list
    void ask_password(const std::string& ssid);
    void submit_password();

    bool updating_ = false;
    bool last_enabled_ = true; // rebuild when the radio toggles

    // header
    Gtk::Switch wifi_switch_;

    // connected card
    Gtk::Box connected_section_{Gtk::Orientation::VERTICAL, 6};
    Gtk::Box connected_card_{Gtk::Orientation::HORIZONTAL, 9};
    Gtk::Label connected_icon_;
    Gtk::Label connected_ssid_;
    Gtk::Button disconnect_;

    // shown instead of the list while the radio is off (Noctalia look)
    Gtk::Box disabled_card_{Gtk::Orientation::VERTICAL, 6};

    // available list
    Gtk::Box available_section_{Gtk::Orientation::VERTICAL, 6};
    Gtk::Label scan_status_; // "Scanning…"/"Connecting…" — lives in the section
                             // header row so it never changes the panel height
    Gtk::Label status_;      // persistent: errors, "Wi-Fi is turned off"
    Gtk::ScrolledWindow scroller_;
    Gtk::Box list_{Gtk::Orientation::VERTICAL, 6};

    // password prompt
    Gtk::Box password_card_{Gtk::Orientation::VERTICAL, 6};
    Gtk::Label password_title_;
    Gtk::Entry password_entry_;
    std::string password_ssid_;
};

} // namespace hyprshell
