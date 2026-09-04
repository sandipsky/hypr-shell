#pragma once

#include "bar/lock_background.hpp"
#include "services/session_actions.hpp"

#include <gtkmm.h>

#include <map>
#include <string>
#include <vector>

namespace hyprshell {

class LockScreen;

// One monitor's lock surface — Noctalia's LockScreenBackground +
// LockScreenPanel: the wallpaper (blurred, shadow gradient), a two-stage
// Windows-11-style panel ("cover" = big clock + date; any key or click reveals
// "login" = avatar, name, password field; Escape returns), the info / error /
// countdown pills 200px above the bottom edge, battery + power button in the
// bottom-right corner and the power button's session menu (suspend, logout,
// [hibernate], reboot, shutdown) with Noctalia's 10s countdown. Typing goes to
// a hidden Gtk::Text (password mode) whose text is mirrored by the controller
// so every monitor shows the same dots.
class LockSurface : public Gtk::Window {
public:
    LockSurface(LockScreen& screen, const Glib::RefPtr<Gdk::Monitor>& monitor, bool preview);
    ~LockSurface() override;

private:
    enum class Stage { Cover, Login };

    void build_cover();
    void build_login();
    void build_pills();
    void build_bottom_right();
    void build_session_menu();

    void set_stage(Stage stage);
    void on_stage_animation_tick();
    // Noctalia's `Behavior on opacity`: fade a widget to `to` over `ms`
    void fade(Gtk::Widget& widget, double to, int ms);
    void on_fade_tick();
    void start_entrance();
    bool on_key_pressed(guint keyval, guint keycode, Gdk::ModifierType state);
    bool on_child_position(Gtk::Widget* widget, Gdk::Rectangle& allocation);
    void refresh();              // controller state → widgets
    void update_password_visuals();
    void update_pills();
    void update_clock();
    void update_battery();
    void apply_config();
    void focus_input();

    // session menu + countdown (Noctalia's startTimer / cancelTimer / executeAction)
    void start_timer(const SessionAction& action);
    void cancel_timer();
    void execute_action(const SessionAction& action);
    void set_session_menu_open(bool open);

    LockScreen& screen_;
    Glib::RefPtr<Gdk::Monitor> monitor_;
    bool preview_ = false;

    Gtk::Overlay root_;
    LockBackground background_;
    Gtk::Box gradient_;
    Gtk::Text input_; // hidden password input

    // cover stage
    Gtk::Box cover_box_{Gtk::Orientation::VERTICAL, 4};
    Gtk::Label time_label_;
    Gtk::Label date_label_;

    // login stage
    Gtk::Box login_box_{Gtk::Orientation::VERTICAL, 13};
    Gtk::Box avatar_ring_;
    Gtk::Picture avatar_;
    Gtk::Label avatar_fallback_;
    Gtk::Label name_label_;
    Gtk::Box password_field_{Gtk::Orientation::HORIZONTAL, 0};
    Gtk::Box caret_left_;
    Gtk::ScrolledWindow visual_host_;
    Gtk::Box visual_box_{Gtk::Orientation::HORIZONTAL, 0};
    Gtk::Label dots_label_;
    Gtk::Label plain_label_;
    Gtk::Box caret_right_;
    Gtk::Button eye_button_;
    Gtk::Label eye_glyph_;
    bool password_visible_ = false;
    bool blink_off_ = false;
    sigc::connection blink_timer_;
    bool syncing_ = false;

    // pills
    Gtk::Box info_pill_{Gtk::Orientation::HORIZONTAL, 9};
    Gtk::Label info_text_;
    Gtk::Box error_pill_{Gtk::Orientation::HORIZONTAL, 9};
    Gtk::Label error_text_;
    Gtk::Box countdown_pill_{Gtk::Orientation::HORIZONTAL, 9};
    Gtk::Label countdown_text_;
    Gtk::Button countdown_cancel_;

    // bottom right
    Gtk::Box bottom_right_{Gtk::Orientation::HORIZONTAL, 13};
    Gtk::Box battery_box_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::Overlay battery_overlay_;
    Gtk::Label battery_fill_;
    Gtk::Label battery_frame_;
    Gtk::Label battery_percent_;
    Gtk::Button power_button_;

    // session menu
    Gtk::Box session_menu_{Gtk::Orientation::VERTICAL, 2};
    bool session_menu_open_ = false;
    const SessionAction* pending_action_ = nullptr;
    bool timer_active_ = false;
    int time_remaining_ms_ = 0;
    sigc::connection countdown_timer_;

    Stage stage_ = Stage::Cover;
    struct Fade {
        double from, to;
        int ms;
        gint64 start_us;
    };
    std::map<Gtk::Widget*, Fade> fades_;
    bool fading_ = false;
    // clock slide-in on appear (Noctalia's Behavior on y from the unsized parent)
    double entrance_ = 0.0;
    gint64 entrance_start_us_ = 0;
    bool entering_ = false;
    double progress_ = 0.0; // 0 = cover shown, 1 = login shown
    double anim_from_ = 0.0;
    double anim_to_ = 0.0;
    gint64 anim_start_us_ = 0;
    bool animating_ = false;
    sigc::connection clock_timer_;
    std::vector<sigc::connection> service_connections_;
};

} // namespace hyprshell
