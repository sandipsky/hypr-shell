#include "bar/calendar.hpp"
#include "services/theme.hpp"

#include "services/config.hpp"

#include <cmath>
#include <string>

namespace hyprshell {

namespace {

// noctalia-tabler-icons glyphs
constexpr const char* kChevronLeft = "";  // chevron-left  U+EA60
constexpr const char* kChevronRight = ""; // chevron-right U+EA61
constexpr const char* kCalendarIcon = ""; // calendar      U+EA53

constexpr const char* kWeekdays[] = {"SU", "MO", "TU", "WE", "TH", "FR", "SA"};
constexpr const char* kMonths[] = {
    "JANUARY", "FEBRUARY", "MARCH",     "APRIL",   "MAY",      "JUNE",
    "JULY",    "AUGUST",   "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER",
};

Gtk::Button* make_icon_button(const char* glyph) {
    auto* button = Gtk::make_managed<Gtk::Button>(glyph);
    button->add_css_class("cal-icon-btn");
    button->set_valign(Gtk::Align::CENTER);
    return button;
}

} // namespace

Calendar::Calendar() : Gtk::Box(Gtk::Orientation::VERTICAL, 9) {
    add_css_class("calendar");
    set_size_request(400, -1);

    // -- header card ---------------------------------------------------------
    header_.add_css_class("cal-header");
    day_big_.add_css_class("cal-day-big");
    month_.add_css_class("cal-month");
    year_.add_css_class("cal-year");

    auto* title = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    title->set_valign(Gtk::Align::CENTER);
    month_.set_valign(Gtk::Align::BASELINE_FILL);
    year_.set_valign(Gtk::Align::BASELINE_FILL);
    title->append(month_);
    title->append(year_);

    auto* spacer = Gtk::make_managed<Gtk::Box>();
    spacer->set_hexpand(true);

    ring_.set_content_width(46);
    ring_.set_content_height(46);
    ring_.set_valign(Gtk::Align::CENTER);
    ring_.set_draw_func(sigc::mem_fun(*this, &Calendar::on_ring_draw));
    auto* time_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    time_box->set_valign(Gtk::Align::CENTER);
    time_box->set_halign(Gtk::Align::CENTER);
    time_h_.add_css_class("cal-time");
    time_m_.add_css_class("cal-time");
    time_box->append(time_h_);
    time_box->append(time_m_);
    auto* clock_overlay = Gtk::make_managed<Gtk::Overlay>();
    clock_overlay->set_child(ring_);
    clock_overlay->add_overlay(*time_box);
    clock_overlay->set_valign(Gtk::Align::CENTER);

    header_.append(day_big_);
    header_.append(*title);
    header_.append(*spacer);
    header_.append(*clock_overlay);
    append(header_);

    // -- month card ----------------------------------------------------------
    body_.add_css_class("cal-body");

    auto* nav = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    nav_title_.add_css_class("cal-nav-title");
    nav_title_.set_margin_start(6);
    auto* divider = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);
    divider->add_css_class("cal-divider");
    divider->set_hexpand(true);
    divider->set_valign(Gtk::Align::CENTER);
    auto* prev = make_icon_button(kChevronLeft);
    auto* today = make_icon_button(kCalendarIcon);
    auto* next = make_icon_button(kChevronRight);
    prev->signal_clicked().connect([this] { navigate(-1); });
    today->signal_clicked().connect([this] { reset_to_today(); });
    next->signal_clicked().connect([this] { navigate(+1); });
    nav->append(nav_title_);
    nav->append(*divider);
    nav->append(*prev);
    nav->append(*today);
    nav->append(*next);
    body_.append(*nav);

    grid_.set_column_homogeneous(true);
    grid_.set_row_spacing(2);
    grid_.set_column_spacing(2);
    body_.append(grid_);

    // scroll anywhere on the month card flips months, like Noctalia
    auto scroll = Gtk::EventControllerScroll::create();
    scroll->set_flags(Gtk::EventControllerScroll::Flags::VERTICAL);
    scroll->signal_scroll().connect(
        [this](double, double dy) {
            scroll_accum_ += dy;
            if (scroll_accum_ >= 1.0) {
                scroll_accum_ = 0.0;
                navigate(+1);
            } else if (scroll_accum_ <= -1.0) {
                scroll_accum_ = 0.0;
                navigate(-1);
            }
            return true;
        },
        false);
    body_.add_controller(scroll);

    append(body_);

    // config may change the first day of the week
    Config::get().signal_changed().connect(sigc::mem_fun(*this, &Calendar::rebuild_grid));

    // tick the header clock/ring only while the popover is shown
    signal_map().connect([this] {
        update_header();
        tick_ = Glib::signal_timeout().connect_seconds(
            [this] {
                update_header();
                return true;
            },
            1);
    });
    signal_unmap().connect([this] { tick_.disconnect(); });

    reset_to_today();
}

void Calendar::reset_to_today() {
    auto now = Glib::DateTime::create_now_local();
    shown_year_ = now.get_year();
    shown_month_ = now.get_month();
    update_header();
    rebuild_grid();
}

void Calendar::update_header() {
    auto now = Glib::DateTime::create_now_local();
    day_big_.set_text(std::to_string(now.get_day_of_month()));
    month_.set_text(kMonths[now.get_month() - 1]);
    year_.set_text(std::to_string(now.get_year()));
    time_h_.set_text(now.format("%H"));
    time_m_.set_text(now.format("%M"));
    ring_fraction_ = now.get_second() / 60.0;
    ring_.queue_draw();
}

void Calendar::navigate(int delta_months) {
    shown_month_ += delta_months;
    while (shown_month_ < 1) {
        shown_month_ += 12;
        --shown_year_;
    }
    while (shown_month_ > 12) {
        shown_month_ -= 12;
        ++shown_year_;
    }
    rebuild_grid();
}

void Calendar::rebuild_grid() {
    while (auto* child = grid_.get_first_child()) {
        grid_.remove(*child);
    }

    const int first_day = Config::get().clock_first_day_of_week(); // 0 = Sunday
    for (int i = 0; i < 7; ++i) {
        auto* label = Gtk::make_managed<Gtk::Label>(kWeekdays[(first_day + i) % 7]);
        label->add_css_class("cal-weekday");
        label->set_hexpand(true);
        grid_.attach(*label, i, 0);
    }

    nav_title_.set_text(std::string(kMonths[shown_month_ - 1]) + " " +
                        std::to_string(shown_year_));

    // day-of-week of the 1st, as an index where 0 == Sunday
    auto first = Glib::DateTime::create_local(shown_year_, shown_month_, 1, 0, 0, 0);
    const int dow = first.get_day_of_week() % 7; // GLib: Mon=1..Sun=7
    const int days_before = (dow - first_day + 7) % 7;
    const int days_in_month = static_cast<int>(Glib::Date::get_days_in_month(
        static_cast<Glib::Date::Month>(shown_month_), shown_year_));
    const int prev_month = shown_month_ == 1 ? 12 : shown_month_ - 1;
    const int prev_year = shown_month_ == 1 ? shown_year_ - 1 : shown_year_;
    const int days_in_prev = static_cast<int>(Glib::Date::get_days_in_month(
        static_cast<Glib::Date::Month>(prev_month), prev_year));
    const int cells = days_before + days_in_month;
    const int days_after = (7 - cells % 7) % 7;

    auto now = Glib::DateTime::create_now_local();
    const bool this_month = now.get_year() == shown_year_ && now.get_month() == shown_month_;

    auto add_cell = [this](int index, int day, bool dim, bool today) {
        auto* label = Gtk::make_managed<Gtk::Label>(std::to_string(day));
        label->add_css_class("cal-day");
        if (dim) {
            label->add_css_class("dim");
        }
        if (today) {
            label->add_css_class("today");
        }
        label->set_size_request(30, 30);
        label->set_halign(Gtk::Align::CENTER);
        label->set_hexpand(true);
        grid_.attach(*label, index % 7, 1 + index / 7);
    };

    int index = 0;
    for (int i = 0; i < days_before; ++i, ++index) {
        add_cell(index, days_in_prev - days_before + 1 + i, true, false);
    }
    for (int day = 1; day <= days_in_month; ++day, ++index) {
        add_cell(index, day, false, this_month && day == now.get_day_of_month());
    }
    for (int day = 1; day <= days_after; ++day, ++index) {
        add_cell(index, day, true, false);
    }
}

void Calendar::on_ring_draw(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height) {
    const double cx = width / 2.0;
    const double cy = height / 2.0;
    const double radius = std::min(width, height) / 2.0 - 2.0;
    cr->set_line_width(2.0);
    cr->set_line_cap(Cairo::Context::LineCap::ROUND);

    // faint full track (mOnPrimary on the primary-coloured header)
    const Gdk::RGBA ring = Theme::get().rgba("mOnPrimary");
    cr->set_source_rgba(ring.get_red(), ring.get_green(), ring.get_blue(), 0.25);
    cr->arc(cx, cy, radius, 0, 2 * G_PI);
    cr->stroke();

    // seconds progress, from 12 o'clock
    cr->set_source_rgb(ring.get_red(), ring.get_green(), ring.get_blue());
    cr->arc(cx, cy, radius, -G_PI / 2, -G_PI / 2 + ring_fraction_ * 2 * G_PI);
    cr->stroke();
}

} // namespace hyprshell
