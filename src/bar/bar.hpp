#pragma once

#include "bar/modules/active_window.hpp"
#include "bar/modules/battery.hpp"
#include "bar/modules/bluetooth.hpp"
#include "bar/modules/clock.hpp"
#include "bar/modules/network.hpp"
#include "bar/modules/volume.hpp"
#include "bar/modules/workspaces.hpp"

#include <gtkmm.h>

namespace hyprshell {

class Bar : public Gtk::ApplicationWindow {
public:
    Bar();
    ~Bar() override;

private:
    void apply_config();
    Gtk::Widget* module_widget(const std::string& name);

    // auto-hide (Noctalia's displayMode = auto_hide semantics)
    void set_hovered(bool hovered);
    void schedule_show();
    void schedule_hide();
    void set_hidden(bool hidden); // starts the slide animation
    void apply_slide();           // layer margin from hide_progress_
    void peek();                  // show briefly, then auto-hide again
    bool popover_open() const;    // e.g. the calendar — blocks hiding
    bool must_stay_visible() const;
    void refresh_workspace_empty();

    Gtk::CenterBox layout_;
    Gtk::Box start_box_{Gtk::Orientation::HORIZONTAL, 0};
    Gtk::Box center_box_{Gtk::Orientation::HORIZONTAL, 0};
    Gtk::Box end_box_{Gtk::Orientation::HORIZONTAL, 0};
    Workspaces workspaces_;
    ActiveWindow active_window_;
    Network network_;
    Bluetooth bluetooth_;
    Volume volume_;
    Battery battery_;
    Clock clock_;

    Gtk::Window trigger_; // 1px hover strip that reveals the hidden bar
    Gtk::Box trigger_fill_;
    bool hovered_ = false;
    bool hidden_ = false;
    bool workspace_empty_ = false;
    unsigned ws_serial_ = 0;
    double hide_progress_ = 0.0; // 0 = shown … 1 = slid off-screen
    double anim_from_ = 0.0;
    gint64 anim_start_us_ = 0;
    bool anim_running_ = false;
    sigc::connection show_timer_;
    sigc::connection hide_timer_;
};

} // namespace hyprshell
