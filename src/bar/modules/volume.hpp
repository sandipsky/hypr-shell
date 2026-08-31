#pragma once

#include "bar/audio_panel.hpp"

#include <gtkmm.h>

namespace hyprshell {

// Default-sink volume status icon (tabler glyphs). Left click opens the audio
// panel (output/input levels), right click toggles output mute.
class Volume : public Gtk::Box {
public:
    Volume();
    ~Volume() override;

private:
    void update();

    Gtk::Label icon_;
    Gtk::Popover popover_;
    AudioPanel* panel_ = nullptr;
};

} // namespace hyprshell
