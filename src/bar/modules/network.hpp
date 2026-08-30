#pragma once

#include <gtkmm.h>

namespace hyprshell {

// Wifi / ethernet status icon (tabler glyphs), Noctalia bucket thresholds.
class Network : public Gtk::Box {
public:
    Network();

private:
    void update();

    Gtk::Label icon_;
};

} // namespace hyprshell
