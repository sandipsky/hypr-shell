#pragma once

#include <gtkmm.h>

namespace hyprshell {

// Default-sink volume status icon (tabler glyphs).
class Volume : public Gtk::Box {
public:
    Volume();

private:
    void update();

    Gtk::Label icon_;
};

} // namespace hyprshell
