#include "bar/battery_panel.hpp"

#include "services/brightness.hpp"
#include "services/config.hpp"
#include "services/hyprland.hpp"
#include "services/power_profiles.hpp"
#include "services/upower.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace hyprshell {

namespace {

// tabler glyphs (noctalia-tabler-icons), \u escapes so the PUA codepoints
// survive every editor/tool (literal PUA glyphs have been silently dropped)
constexpr const char* kIconLeaf = "\uED4F";
constexpr const char* kIconScale = "\uEBC2";
constexpr const char* kIconGauge = "\uEAB1";
constexpr const char* kIconRefresh = "\uEB13";
constexpr const char* kIconSunOff = "\uED63";
constexpr const char* kIconBrightnessLow = "\uFB23";
constexpr const char* kIconBrightnessHigh = "\uFB24";

constexpr const char* kProfileKeys[3] = {"power-saver", "balanced", "performance"};
constexpr const char* kProfileNames[3] = {"Power saver", "Balanced", "Performance"};

Glib::ustring vague_duration(gint64 seconds) {
    const gint64 h = seconds / 3600, m = (seconds % 3600) / 60;
    if (h > 0)
        return Glib::ustring::compose("%1 h %2 m", h, m);
    return Glib::ustring::compose("%1 m", m);
}

Gtk::Label* title_label(const char* text) {
    auto* label = Gtk::make_managed<Gtk::Label>(text);
    label->add_css_class("bp-title");
    label->set_halign(Gtk::Align::START);
    label->set_hexpand(true);
    return label;
}

Gtk::Label* icon_label(const char* glyph) {
    auto* label = Gtk::make_managed<Gtk::Label>(glyph);
    label->add_css_class("bp-icon");
    return label;
}

} // namespace

BatteryPanel::BatteryPanel() : Gtk::Box(Gtk::Orientation::VERTICAL, 9) {
    add_css_class("battery-panel");
    set_size_request(330, -1);

    // -- charge card ---------------------------------------------------------
    charge_card_.add_css_class("bp-card");
    {
        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        row->append(*title_label("Battery"));
        charge_time_.add_css_class("bp-value");
        row->append(charge_time_);
        charge_card_.append(*row);

        auto* bar_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 9);
        charge_bar_.set_hexpand(true);
        charge_bar_.set_valign(Gtk::Align::CENTER);
        charge_bar_.add_css_class("bp-level");
        charge_percent_.add_css_class("bp-percent");
        bar_row->append(charge_bar_);
        bar_row->append(charge_percent_);
        charge_card_.append(*bar_row);
    }
    append(charge_card_);

    // -- power profile card ----------------------------------------------------
    profile_card_.add_css_class("bp-card");
    {
        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        row->append(*title_label("Power profile"));
        profile_name_.add_css_class("bp-value");
        row->append(profile_name_);
        profile_card_.append(*row);

        profile_scale_.set_range(0, 2);
        profile_scale_.set_increments(1, 1);
        profile_scale_.set_round_digits(0); // snap to the three stops
        profile_scale_.set_draw_value(false);
        profile_scale_.add_css_class("bp-slider");
        profile_scale_.signal_value_changed().connect([this] {
            if (updating_)
                return;
            const int idx = std::clamp((int)std::lround(profile_scale_.get_value()), 0, 2);
            PowerProfiles::get().set_profile(kProfileKeys[idx]);
        });
        profile_card_.append(profile_scale_);

        auto* icons = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
        const char* glyphs[3] = {kIconLeaf, kIconScale, kIconGauge};
        for (int i = 0; i < 3; ++i) {
            profile_icons_[i].set_text(glyphs[i]);
            profile_icons_[i].add_css_class("bp-icon");
            profile_icons_[i].add_css_class("bp-profile-icon");
            profile_icons_[i].set_hexpand(i == 1);
            profile_icons_[i].set_halign(i == 0   ? Gtk::Align::START
                                         : i == 1 ? Gtk::Align::CENTER
                                                  : Gtk::Align::END);
            icons->append(profile_icons_[i]);
        }
        profile_card_.append(*icons);
    }
    append(profile_card_);

    // -- brightness card -------------------------------------------------------
    brightness_card_.add_css_class("bp-card");
    {
        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 9);
        brightness_icon_.set_text(kIconBrightnessHigh);
        brightness_icon_.add_css_class("bp-icon");
        row->append(brightness_icon_);
        row->append(*title_label("Brightness"));
        brightness_percent_.add_css_class("bp-value");
        row->append(brightness_percent_);
        brightness_card_.append(*row);

        brightness_scale_.set_range(0, 100);
        brightness_scale_.set_increments(5, 10);
        brightness_scale_.set_draw_value(false);
        brightness_scale_.add_css_class("bp-slider");
        brightness_scale_.signal_value_changed().connect([this] {
            if (updating_)
                return;
            Brightness::get().set_fraction(brightness_scale_.get_value() / 100.0);
        });
        brightness_card_.append(brightness_scale_);
    }
    append(brightness_card_);

    // -- refresh rate card -----------------------------------------------------
    refresh_card_.add_css_class("bp-card");
    {
        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 9);
        row->append(*icon_label(kIconRefresh));
        row->append(*title_label("Refresh rate"));
        refresh_current_.add_css_class("bp-value");
        row->append(refresh_current_);
        refresh_card_.append(*row);

        rate_buttons_.set_homogeneous(true);
        refresh_card_.append(rate_buttons_);
    }
    append(refresh_card_);

    UPower::get().signal_changed().connect(
        sigc::mem_fun(*this, &BatteryPanel::update_battery));
    PowerProfiles::get().signal_changed().connect(
        sigc::mem_fun(*this, &BatteryPanel::update_profile));
    Brightness::get().signal_changed().connect(
        sigc::mem_fun(*this, &BatteryPanel::update_brightness));
    Config::get().signal_changed().connect(
        sigc::mem_fun(*this, &BatteryPanel::update_visibility));

    update_battery();
    update_profile();
    update_brightness();
    update_visibility();
}

void BatteryPanel::refresh() {
    Brightness::get().refresh();
    query_monitor();
    update_battery();
    update_profile();
    update_brightness();
    update_visibility();
}

void BatteryPanel::update_battery() {
    auto& upower = UPower::get();
    const int percent = (int)std::lround(upower.percentage());
    charge_bar_.set_fraction(upower.percentage() / 100.0);
    charge_percent_.set_text(Glib::ustring::compose("%1%%", percent));

    Glib::ustring time;
    if (upower.plugged_idle())
        time = "Plugged in";
    else if (upower.plugged() && upower.time_to_full() > 0)
        time = vague_duration(upower.time_to_full()) + " until full";
    else if (!upower.plugged() && upower.time_to_empty() > 0)
        time = vague_duration(upower.time_to_empty()) + " remaining";
    charge_time_.set_text(time);
    charge_time_.set_visible(!time.empty());
}

void BatteryPanel::update_profile() {
    auto& profiles = PowerProfiles::get();
    int idx = 1;
    for (int i = 0; i < 3; ++i)
        if (profiles.profile() == kProfileKeys[i])
            idx = i;
    updating_ = true;
    profile_scale_.set_value(idx);
    updating_ = false;
    profile_name_.set_text(kProfileNames[idx]);
    for (int i = 0; i < 3; ++i) {
        if (i == idx)
            profile_icons_[i].add_css_class("active");
        else
            profile_icons_[i].remove_css_class("active");
    }
    update_visibility();
}

void BatteryPanel::update_brightness() {
    auto& brightness = Brightness::get();
    const int percent = (int)std::lround(brightness.fraction() * 100.0);
    updating_ = true;
    brightness_scale_.set_value(percent);
    updating_ = false;
    brightness_percent_.set_text(Glib::ustring::compose("%1%%", percent));
    brightness_icon_.set_text(percent <= 0   ? kIconSunOff
                              : percent <= 50 ? kIconBrightnessLow
                                              : kIconBrightnessHigh);
}

void BatteryPanel::update_visibility() {
    auto& cfg = Config::get();
    charge_card_.set_visible(UPower::get().available());
    profile_card_.set_visible(cfg.battery_show_power_profiles() &&
                              PowerProfiles::get().available());
    brightness_card_.set_visible(cfg.battery_show_brightness() &&
                                 Brightness::get().available());
    refresh_card_.set_visible(cfg.battery_show_refresh_rate() &&
                              monitor_.rates.size() > 1);
}

void BatteryPanel::query_monitor() {
    auto& hypr = Hyprland::get();
    if (!hypr.available())
        return;
    const unsigned serial = ++monitor_serial_;
    hypr.request("j/monitors", [this, serial](const std::string& reply) {
        if (serial != monitor_serial_)
            return;
        try {
            const auto monitors = nlohmann::json::parse(reply);
            if (!monitors.is_array() || monitors.empty())
                return;
            // the focused monitor, falling back to the first
            const auto* mon = &monitors[0];
            for (const auto& m : monitors)
                if (m.value("focused", false))
                    mon = &m;
            monitor_.name = mon->value("name", "");
            monitor_.width = mon->value("width", 0);
            monitor_.height = mon->value("height", 0);
            monitor_.x = mon->value("x", 0);
            monitor_.y = mon->value("y", 0);
            monitor_.scale = mon->value("scale", 1.0);
            monitor_.transform = mon->value("transform", 0);
            monitor_.current_rate =
                (int)std::lround(mon->value("refreshRate", 0.0));
            // distinct rates available at the current resolution,
            // mode strings look like "1920x1080@144.00Hz"
            monitor_.rates.clear();
            const std::string prefix = std::to_string(monitor_.width) + "x" +
                                       std::to_string(monitor_.height) + "@";
            for (const auto& mode : mon->value("availableModes",
                                               std::vector<std::string>{})) {
                if (mode.rfind(prefix, 0) != 0)
                    continue;
                const int rate =
                    (int)std::lround(std::stod(mode.substr(prefix.size())));
                if (std::find(monitor_.rates.begin(), monitor_.rates.end(), rate) ==
                    monitor_.rates.end())
                    monitor_.rates.push_back(rate);
            }
            std::sort(monitor_.rates.begin(), monitor_.rates.end());
        } catch (const std::exception& e) {
            g_warning("battery panel: monitors query failed: %s", e.what());
            return;
        }
        rebuild_rate_buttons();
        update_visibility();
    });
}

void BatteryPanel::rebuild_rate_buttons() {
    while (auto* child = rate_buttons_.get_first_child())
        rate_buttons_.remove(*child);
    refresh_current_.set_text(
        Glib::ustring::compose("%1 Hz", monitor_.current_rate));
    for (const int rate : monitor_.rates) {
        auto* button = Gtk::make_managed<Gtk::Button>(
            Glib::ustring::compose("%1 Hz", rate));
        button->add_css_class("bp-rate-btn");
        if (rate == monitor_.current_rate)
            button->add_css_class("active");
        button->signal_clicked().connect([this, rate] {
            if (rate == monitor_.current_rate)
                return;
            Hyprland::get().set_monitor_mode(
                monitor_.name, monitor_.width, monitor_.height, rate, monitor_.scale,
                monitor_.transform, monitor_.x, monitor_.y, [this](bool ok) {
                    if (ok)
                        query_monitor(); // re-read the applied rate
                });
        });
        rate_buttons_.append(*button);
    }
}

} // namespace hyprshell
