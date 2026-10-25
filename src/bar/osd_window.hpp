#pragma once

#include "services/osd.hpp"

#include <gtkmm.h>

#include <vector>

namespace hyprshell {

// On-screen display — Noctalia's OSD.qml as a click-through layer window:
// a rounded card with icon, progress bar and percentage (or "CAPS ON"-style
// text for lock keys), anchored per osd.location, fading + scaling in over
// 300ms, auto-hidden 2s after the last change. Horizontal at the top/bottom
// anchors, a vertical column at the left/right ones. The window keeps a
// fixed size per orientation (320x72 / 80x280, Noctalia's) and the card is
// positioned inside it with a Gtk::Fixed child transform, so the layer
// surface never has to resize while mapped. One window on the compositor's
// default output (per-monitor windows come with phase 1).
class OsdWindow : public Gtk::Window {
public:
    OsdWindow();

private:
    void apply_config();  // anchors, margins, orientation (hides while it rebuilds)
    void build_content(); // widget tree for the current orientation
    void on_show_requested(Osd::Type type);
    void refresh();       // icon/text/colors/bar target from the services
    void start_fade(bool in);
    void place_card();    // Fixed child transform from scale_ and the anchor side
    void set_state_class(Gtk::Widget& widget, const char* state);
    void draw_bar(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height);

    Gtk::Fixed fixed_;
    Gtk::Box card_{Gtk::Orientation::VERTICAL, 0};
    Gtk::Box content_{Gtk::Orientation::HORIZONTAL, 9};
    Gtk::Label icon_;
    Gtk::Label text_;                                  // percentage / lock text
    Gtk::Box lock_chars_{Gtk::Orientation::VERTICAL, 0}; // stacked lock text (vertical)
    Gtk::DrawingArea bar_;

    Osd::Type type_ = Osd::Type::Volume;
    bool vertical_ = false;
    int win_w_ = 0, win_h_ = 0;   // layer surface size
    int card_w_ = 0, card_h_ = 0; // card size inside it

    // fade + scale (Noctalia: opacity 0→1, scale 0.85→1, 300ms InOutQuad)
    double opacity_ = 0.0, scale_ = 0.85;
    double fade_from_opacity_ = 0.0, fade_from_scale_ = 0.85;
    bool fade_in_ = false;
    bool fading_ = false;
    gint64 fade_start_us_ = 0;
    sigc::connection show_delay_;
    sigc::connection hide_timer_;

    // progress bar value + color animation
    struct Rgb {
        double r, g, b;
    };
    double bar_value_ = 0.0, bar_from_ = 0.0, bar_to_ = 0.0;
    Rgb bar_color_{0, 0, 0}, bar_color_from_{0, 0, 0}, bar_color_to_{0, 0, 0};
    bool bar_animating_ = false;
    gint64 bar_start_us_ = 0;
    bool bar_synced_ = false;
};

} // namespace hyprshell
