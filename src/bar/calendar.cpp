#include "bar/calendar.hpp"
#include "services/theme.hpp"

#include "services/config.hpp"
#include "services/nepali_date.hpp"

#include <cmath>
#include <optional>
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

    // AD / BS switch: two grouped toggles styled as one pill
    system_box_.add_css_class("cal-system");
    system_box_.set_valign(Gtk::Align::CENTER);
    for (auto* button : {&ad_button_, &bs_button_}) {
        button->add_css_class("cal-system-btn");
        button->set_valign(Gtk::Align::CENTER);
        system_box_.append(*button);
    }
    bs_button_.set_group(ad_button_);
    ad_button_.set_active(true);
    ad_button_.signal_toggled().connect([this] {
        if (!syncing_buttons_ && ad_button_.get_active())
            set_system(System::Gregorian);
    });
    bs_button_.signal_toggled().connect([this] {
        if (!syncing_buttons_ && bs_button_.get_active())
            set_system(System::Bikram);
    });

    nav->append(nav_title_);
    nav->append(*divider);
    nav->append(system_box_);
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

    // config may change the first day of the week or the default calendar system
    Config::get().signal_changed().connect([this] {
        adopt_config_system();
        rebuild_grid();
    });
    adopt_config_system();

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

// Today as a BS date, or nullopt once the table runs out (BS 2091+).
static std::optional<nepali::Date> bikram_today() {
    auto now = Glib::DateTime::create_now_local();
    return nepali::from_gregorian(now.get_year(), now.get_month(), now.get_day_of_month());
}

void Calendar::reset_to_today() {
    auto now = Glib::DateTime::create_now_local();
    if (system_ == System::Bikram) {
        if (auto bs = bikram_today()) {
            shown_year_ = bs->year;
            shown_month_ = bs->month;
        } else {
            set_system(System::Gregorian); // recurses with the Gregorian branch
            return;
        }
    } else {
        shown_year_ = now.get_year();
        shown_month_ = now.get_month();
    }
    update_header();
    rebuild_grid();
}

// The config value is adopted only when it changes (like DND), so a switch
// made in the popover survives unrelated config reloads.
void Calendar::adopt_config_system() {
    const std::string& wanted = Config::get().clock_calendar();
    if (wanted == config_system_)
        return;
    config_system_ = wanted;
    set_system(wanted == "bs" ? System::Bikram : System::Gregorian);
}

void Calendar::set_system(System system) {
    // the table ends in 2034 AD; after that only the Gregorian view is available
    const auto today_bs = bikram_today();
    system_box_.set_visible(today_bs.has_value());
    if (system == System::Bikram && !today_bs)
        system = System::Gregorian;

    if (system != system_) {
        // keep the shown month: convert its 1st into the other system
        if (system == System::Bikram) {
            auto bs = nepali::from_gregorian(shown_year_, shown_month_, 1);
            if (!bs)
                bs = today_bs;
            shown_year_ = bs->year;
            shown_month_ = bs->month;
        } else {
            auto g = nepali::to_gregorian({shown_year_, shown_month_, 1});
            auto now = Glib::DateTime::create_now_local();
            shown_year_ = g ? static_cast<int>(g->get_year()) : now.get_year();
            shown_month_ = g ? static_cast<int>(g->get_month()) : now.get_month();
        }
        system_ = system;
    }

    syncing_buttons_ = true;
    (system_ == System::Bikram ? bs_button_ : ad_button_).set_active(true);
    syncing_buttons_ = false;
    update_header();
    rebuild_grid();
}

bool Calendar::month_valid(int year, int month) const {
    if (system_ == System::Bikram)
        return nepali::days_in_month(year, month) > 0;
    return Glib::Date::valid_year(static_cast<Glib::Date::Year>(year));
}

void Calendar::update_header() {
    auto now = Glib::DateTime::create_now_local();
    std::optional<nepali::Date> bs;
    if (system_ == System::Bikram)
        bs = bikram_today();
    if (bs) {
        day_big_.set_text(std::to_string(bs->day));
        month_.set_text(nepali::kMonths[bs->month - 1]);
        year_.set_text(std::to_string(bs->year));
    } else {
        day_big_.set_text(std::to_string(now.get_day_of_month()));
        month_.set_text(kMonths[now.get_month() - 1]);
        year_.set_text(std::to_string(now.get_year()));
    }
    time_h_.set_text(now.format("%H"));
    time_m_.set_text(now.format("%M"));
    ring_fraction_ = now.get_second() / 60.0;
    ring_.queue_draw();
}

void Calendar::navigate(int delta_months) {
    int year = shown_year_;
    int month = shown_month_ + delta_months;
    while (month < 1) {
        month += 12;
        --year;
    }
    while (month > 12) {
        month -= 12;
        ++year;
    }
    if (!month_valid(year, month))
        return; // the BS table ends here
    shown_year_ = year;
    shown_month_ = month;
    rebuild_grid();
}

Calendar::MonthInfo Calendar::month_info() const {
    MonthInfo info;
    const int prev_month = shown_month_ == 1 ? 12 : shown_month_ - 1;
    const int prev_year = shown_month_ == 1 ? shown_year_ - 1 : shown_year_;
    auto now = Glib::DateTime::create_now_local();

    if (system_ == System::Bikram) {
        info.title = std::string(nepali::kMonths[shown_month_ - 1]) + " " +
                     std::to_string(shown_year_);
        info.days_in_month = nepali::days_in_month(shown_year_, shown_month_);
        // the month before 1 Baishakh 2000 is outside the table: pad with 32,
        // the largest BS month, so the dimmed lead-in days still count down
        info.days_in_prev = nepali::days_in_month(prev_year, prev_month);
        if (info.days_in_prev == 0)
            info.days_in_prev = 32;
        if (auto first = nepali::to_gregorian({shown_year_, shown_month_, 1}))
            info.first_weekday = static_cast<int>(first->get_weekday()) % 7; // Mon=1..Sun=7
        if (auto today = bikram_today();
            today && today->year == shown_year_ && today->month == shown_month_)
            info.today = today->day;
        return info;
    }

    info.title = std::string(kMonths[shown_month_ - 1]) + " " + std::to_string(shown_year_);
    auto first = Glib::DateTime::create_local(shown_year_, shown_month_, 1, 0, 0, 0);
    info.first_weekday = first.get_day_of_week() % 7; // GLib: Mon=1..Sun=7
    info.days_in_month = static_cast<int>(Glib::Date::get_days_in_month(
        static_cast<Glib::Date::Month>(shown_month_), shown_year_));
    info.days_in_prev = static_cast<int>(Glib::Date::get_days_in_month(
        static_cast<Glib::Date::Month>(prev_month), prev_year));
    if (now.get_year() == shown_year_ && now.get_month() == shown_month_)
        info.today = now.get_day_of_month();
    return info;
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

    const MonthInfo info = month_info();
    nav_title_.set_text(info.title);

    const int days_before = (info.first_weekday - first_day + 7) % 7;
    const int days_in_month = info.days_in_month;
    const int days_in_prev = info.days_in_prev;
    // Always six week rows (Noctalia's MonthGrid does the same): a mapped
    // popover on Hyprland cannot grow, so a 5-row -> 6-row month change would
    // dismiss it instead (seen live: September -> August 2026 closed the popup).
    constexpr int kRows = 6;
    const int days_after = kRows * 7 - days_before - days_in_month;

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
        add_cell(index, day, false, day == info.today);
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
