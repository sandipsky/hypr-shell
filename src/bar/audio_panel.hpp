#pragma once

#include <gtkmm.h>

namespace hyprshell {

// Volume popover content, following Noctalia's audio panel: an Output and an
// Input card, each with "<Kind> - <device description>", a level slider, the
// percentage, and a round mute-toggle button whose icon tracks the state.
class AudioPanel : public Gtk::Box {
public:
    AudioPanel();

    void refresh(); // sync sliders when the popover opens

private:
    // one card's widgets; built identically for output and input
    struct Row {
        Gtk::Box card{Gtk::Orientation::VERTICAL, 6};
        Gtk::Label device;
        Gtk::Scale scale;
        Gtk::Label percent;
        Gtk::Button mute;
        Gtk::Label mute_icon;
    };

    void build_row(Row& row, const char* kind);
    void update();

    bool updating_ = false; // programmatic slider updates — don't write back

    Row output_;
    Row input_;
};

} // namespace hyprshell
