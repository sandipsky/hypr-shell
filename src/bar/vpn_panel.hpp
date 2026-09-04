#pragma once

#include <gtkmm.h>

#include <string>

namespace hyprshell {

// VPN popover content — Noctalia's VPNPanel: header (shield icon, "VPN",
// import button — its refresh / close buttons were dropped per user; the
// list refreshes on open and NM changes), then one card per profile (toggle with
// name + state, delete button with an inline confirm row) or the empty state
// with an import button. Fixed 440x500 like Noctalia (popover-resize gotcha).
class VpnPanel : public Gtk::Box {
public:
    VpnPanel();

    void set_open(bool open); // popover mapped state: refresh on open
    void open_import_dialog() { pick_import_file(); } // dev hook entry

private:
    void rebuild();
    void pick_import_file();
    Gtk::Widget* make_profile_card(const std::string& uuid);

    bool open_ = false;
    std::string confirming_uuid_; // card showing its delete confirmation

    Gtk::Label header_icon_;
    Gtk::ScrolledWindow scroller_;
    Gtk::Box list_{Gtk::Orientation::VERTICAL, 9};
};

} // namespace hyprshell
