#pragma once

#include "bar/network_panel.hpp"

#include <gtkmm.h>

namespace hyprshell {

// Wifi / ethernet status icon (tabler glyphs), Noctalia bucket thresholds.
// An active ethernet connection wins the icon even while wifi is up, like
// Noctalia. Click opens the Wi-Fi selector panel.
class Network : public Gtk::Box {
public:
    Network();
    ~Network() override;

private:
    void update();

    Gtk::Label icon_;
    Gtk::Popover popover_;
    NetworkPanel* panel_ = nullptr;
};

} // namespace hyprshell
