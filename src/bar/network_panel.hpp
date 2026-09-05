#pragma once

#include "bar/busy_indicator.hpp"
#include "services/network_manager.hpp"

#include <gtkmm.h>

namespace hyprshell {

// Network popover content, following Noctalia's Wi-Fi panel: header with a
// wifi radio switch, the connected network card (Disconnect), and a scrolled
// "Available networks" list with per-network Connect. Connecting to a new
// secured network reveals an inline password card first. A refresh button in
// the header rescans; it turns into a spinner while a scan runs, and the row
// being connected (or the Disconnect button) shows a spinner too — no
// "Scanning…"/"Connecting…" text (user request).
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
    void start_connect(const std::string& ssid, const std::string& password = "");
    void update_busy(); // header spinner + disconnect spinner from state

    bool updating_ = false;
    bool last_enabled_ = true; // rebuild when the radio toggles

    std::string connecting_ssid_; // row showing a spinner instead of Connect
    bool disconnecting_ = false;

    // header
    Gtk::Button refresh_btn_;      // rescan; shows the spinner while scanning
    Gtk::Stack refresh_stack_;
    Gtk::Label refresh_icon_;
    BusyIndicator refresh_spinner_;
    Gtk::Switch wifi_switch_;

    // connected card
    Gtk::Box connected_section_{Gtk::Orientation::VERTICAL, 6};
    Gtk::Box connected_card_{Gtk::Orientation::HORIZONTAL, 9};
    Gtk::Label connected_icon_;
    Gtk::Label connected_ssid_;
    Gtk::Stack disconnect_stack_; // Disconnect button | spinner while busy
    Gtk::Button disconnect_;
    BusyIndicator disconnect_spinner_;

    // shown instead of the list while the radio is off (Noctalia look)
    Gtk::Box disabled_card_{Gtk::Orientation::VERTICAL, 6};

    // available list
    Gtk::Box available_section_{Gtk::Orientation::VERTICAL, 6};
    Gtk::Label status_; // persistent: errors, "Wi-Fi is turned off"
    Gtk::ScrolledWindow scroller_;
    Gtk::Box list_{Gtk::Orientation::VERTICAL, 6};

    // password prompt
    Gtk::Box password_card_{Gtk::Orientation::VERTICAL, 6};
    Gtk::Label password_title_;
    Gtk::Entry password_entry_;
    std::string password_ssid_;
};

} // namespace hyprshell
