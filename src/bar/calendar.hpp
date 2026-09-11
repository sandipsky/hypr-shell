#pragma once

#include <gtkmm.h>

#include <string>

namespace hyprshell {

// Calendar popover content, a 1:1 port of Noctalia's calendar panel:
// a header card (big day number, MONTH year, digital clock in a seconds-
// progress ring) over a month card (nav row, weekday header, 7-column grid
// with dimmed adjacent months and a highlighted today). The month card
// shows either the Gregorian (AD) or the Bikram Sambat (BS) calendar — an
// AD / BS switch in the nav row flips it; `bar.clock.calendar` is the default.
class Calendar : public Gtk::Box {
public:
    Calendar();

    // jump back to the current month and refresh header + grid
    void reset_to_today();
    // step the shown month (also a dev-hook entry point)
    void navigate(int delta_months);

private:
    enum class System { Gregorian, Bikram };

    // what rebuild_grid() needs to lay out a month in either system
    struct MonthInfo {
        std::string title;   // "SEPTEMBER 2026" / "BHADRA 2083"
        int days_in_month = 0;
        int days_in_prev = 0;
        int first_weekday = 0; // 0 = Sunday
        int today = 0;         // day of month, 0 when today is not in this month
    };

    void update_header();
    void rebuild_grid();
    void set_system(System system);
    void adopt_config_system();
    bool month_valid(int year, int month) const;
    MonthInfo month_info() const;
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
    Gtk::Box system_box_{Gtk::Orientation::HORIZONTAL, 0};
    Gtk::ToggleButton ad_button_{"AD"};
    Gtk::ToggleButton bs_button_{"BS"};
    Gtk::Grid grid_;

    System system_ = System::Gregorian;
    std::string config_system_; // last seen bar.clock.calendar (edge tracking)
    bool syncing_buttons_ = false;

    // the shown month, in the active system's year/month numbering
    int shown_year_ = 2000;
    int shown_month_ = 1; // 1..12
    double ring_fraction_ = 0.0;
    double scroll_accum_ = 0.0;
    sigc::connection tick_;
};

} // namespace hyprshell
