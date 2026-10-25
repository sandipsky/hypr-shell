#pragma once

#include <gtkmm.h>

namespace hyprshell {

// Fade-to-black grace overlay shown before each idle action (Noctalia's
// IdleFadeOverlay): a fullscreen overlay layer window whose opacity eases in
// from ~0 to 1 over idle.fade_duration seconds; hidden the moment the idle
// service reports the fade cancelled or finished. One window, on the
// compositor's default output (per-monitor windows come with phase 1's
// per-monitor work).
class IdleFade : public Gtk::Window {
public:
    IdleFade();

private:
    void update();
    void start_fade();

    Gtk::Box fill_;
    bool animating_ = false;
    gint64 start_us_ = 0;
};

} // namespace hyprshell
