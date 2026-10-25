#pragma once

#include <gtkmm.h>

#include <string>
#include <vector>

namespace hyprshell {

// Battery popover content, following Noctalia's battery panel: a charge card
// (level bar + time remaining), a power-profile slider with the three
// power-profiles-daemon profiles, a brightness slider (logind backlight), and
// a refresh-rate switcher (Hyprland monitor modes at the current resolution).
// Each card hides when its backend is unavailable or disabled in bar.battery.
class BatteryPanel : public Gtk::Box {
public:
    BatteryPanel();

    // called every time the popover opens: re-read brightness (sysfs has no
    // change events) and re-query the monitor's modes
    void refresh();

private:
    void update_battery();
    void update_profile();
    void update_brightness();
    void update_visibility();
    void query_monitor();
    void rebuild_rate_buttons();

    bool updating_ = false; // programmatic widget updates — don't write back

    // charge card
    Gtk::Box charge_card_{Gtk::Orientation::VERTICAL, 6};
    Gtk::Label charge_time_;
    Gtk::ProgressBar charge_bar_;
    Gtk::Label charge_percent_;

    // power profile card
    Gtk::Box profile_card_{Gtk::Orientation::VERTICAL, 3};
    Gtk::Label profile_name_;
    Gtk::Scale profile_scale_;
    Gtk::Label profile_icons_[3]; // leaf / scale / gauge

    // brightness card
    Gtk::Box brightness_card_{Gtk::Orientation::VERTICAL, 3};
    Gtk::Label brightness_icon_;
    Gtk::Label brightness_percent_;
    Gtk::Scale brightness_scale_;

    // refresh rate card
    Gtk::Box refresh_card_{Gtk::Orientation::VERTICAL, 6};
    Gtk::Label refresh_current_;
    Gtk::Box rate_buttons_{Gtk::Orientation::HORIZONTAL, 6};
    struct MonitorInfo {
        std::string name;
        int width = 0, height = 0, x = 0, y = 0, transform = 0;
        double scale = 1.0;
        int current_rate = 0;
        std::vector<int> rates;
    } monitor_;
    unsigned monitor_serial_ = 0;
};

} // namespace hyprshell
