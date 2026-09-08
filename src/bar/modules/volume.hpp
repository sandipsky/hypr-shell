#pragma once

#include "bar/audio_panel.hpp"

#include <gtkmm.h>

namespace hyprshell {

// Default-sink volume status icon (tabler glyphs). Left click opens the audio
// panel (output/input levels), right click toggles output mute, the wheel
// steps the volume by bar.volume.scroll_step percent.
class Volume : public Gtk::Box {
public:
    Volume();
    ~Volume() override;

private:
    void update();
    bool on_scroll(double dx, double dy);

    double scroll_accum_ = 0.0; // smooth-scroll deltas, one step per whole unit

    Gtk::Label icon_;
    Gtk::Popover popover_;
    AudioPanel* panel_ = nullptr;
};

} // namespace hyprshell
