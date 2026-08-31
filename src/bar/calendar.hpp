#pragma once

#include <gtkmm.h>

namespace hyprshell {

// Calendar popover content, a 1:1 port of Noctalia's calendar panel:
// a header card (big day number, MONTH year, digital clock in a seconds-
// progress ring) over a month card (nav row, weekday header, 7-column grid
// with dimmed adjacent months and a highlighted today).
class Calendar : public Gtk::Box {
public:
    Calendar();

    // jump back to the current month and refresh header + grid
    void reset_to_today();

private:
    void update_header();
    void rebuild_grid();
    void navigate(int delta_months);
    void on_ring_draw(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height);

    // header card
    Gtk::Box header_{Gtk::Orientation::HORIZONTAL, 6};
    Gtk::Label day_big_;
    Gtk::Label month_;
    Gtk::Label year_;
    Gtk::DrawingArea ring_;
    Gtk::Label time_h_;
    Gtk::Label time_m_;

    // month card
    Gtk::Box body_{Gtk::Orientation::VERTICAL, 6};
    Gtk::Label nav_title_;
    Gtk::Grid grid_;

    int shown_year_ = 2000;
    int shown_month_ = 1; // 1..12
    double ring_fraction_ = 0.0;
    double scroll_accum_ = 0.0;
    sigc::connection tick_;
};

} // namespace hyprshell
