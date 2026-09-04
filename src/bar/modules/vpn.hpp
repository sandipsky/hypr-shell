#pragma once

#include "bar/vpn_panel.hpp"

#include <gtkmm.h>

namespace hyprshell {

// VPN bar pill (Noctalia's VPN widget on BarPill): shield / shield-lock icon
// with the first active profile's name (+ N more) as pill text — revealed on
// hover, always, or never per bar.vpn.display_mode; icon-only on vertical
// bars. Click opens the VPN panel (no right-click menu, per user).
class Vpn : public Gtk::Box {
public:
    Vpn();
    ~Vpn() override;

private:
    void update();
    void apply_config();
    void open_panel();
    void set_revealed(bool revealed);

    Gtk::Label icon_;
    Gtk::Revealer revealer_;
    Gtk::Label text_;
    Gtk::Popover popover_;
    VpnPanel* panel_ = nullptr;
    sigc::connection show_timer_;
    bool hovered_ = false;
    std::string icon_color_class_, text_color_class_;
};

} // namespace hyprshell
