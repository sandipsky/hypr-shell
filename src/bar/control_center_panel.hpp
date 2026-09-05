#pragma once

#include <gtkmm.h>

#include <string>

namespace hyprshell {

// Noctalia's NCircleStat: a 240° arc gauge (150° → 390°) with the value
// centered and an icon in the bottom gap; the ratio eases over 300 ms.
class CircleStat : public Gtk::DrawingArea {
public:
    CircleStat(const char* glyph, const char* suffix);
    void set_ratio(double ratio, const Gdk::RGBA& color); // 0..1

private:
    void draw(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h);
    std::string glyph_, suffix_;
    double ratio_ = 0, animated_ = 0, from_ = 0;
    gint64 start_us_ = 0;
    bool animating_ = false;
    Gdk::RGBA color_;
};

// Blurred album art (or nothing) behind the media card's content, masked to
// the card's 16px radius, with Noctalia's 65% mSurface scrim on top.
class MediaBackground : public Gtk::Widget {
public:
    MediaBackground();
    void set_art_url(const std::string& url); // "" clears

protected:
    void snapshot_vfunc(const Glib::RefPtr<Gtk::Snapshot>& snapshot) override;

private:
    std::string url_;
    Glib::RefPtr<Gdk::Texture> texture_;
    Glib::RefPtr<Gio::Cancellable> cancellable_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

// Control center popover content — Noctalia's ControlCenterPanel: the profile
// row (avatar, name, uptime, settings + power buttons — no close button, per
// user) followed by the four cards the user asked for (audio sliders,
// brightness slider, media player, system monitor), each toggled by
// bar.control_center.show_*; no shortcuts / weather rows. 440px wide, cards
// at Noctalia's fixed heights (60 / 60 / 220 / 84) so the popover never
// needs to resize while open.
class ControlCenterPanel : public Gtk::Box {
public:
    ControlCenterPanel();
    ~ControlCenterPanel() override;

    void set_open(bool open); // popover mapped: stats polling, position refresh
    sigc::signal<void()>& signal_request_close() { return request_close_; }

private:
    void build_profile();
    void update_uptime();
    void build_audio();
    void build_brightness();
    void build_media();
    void build_sysmon();
    void apply_config();
    void update_audio();
    void update_brightness();
    void update_media();
    void update_sysmon();

    bool open_ = false;
    bool updating_ = false;
    sigc::signal<void()> request_close_;

    // profile card
    Gtk::Box profile_card_{Gtk::Orientation::HORIZONTAL, 9};
    Gtk::Box avatar_ring_{Gtk::Orientation::VERTICAL, 0};
    Gtk::Picture avatar_;
    Gtk::Label avatar_fallback_;
    Gtk::Label name_, uptime_;
    Gtk::Button settings_button_, power_button_;
    Gtk::Label settings_icon_, power_icon_;
    sigc::connection uptime_timer_;

    // audio card: output | input columns
    struct AudioColumn {
        Gtk::Box box{Gtk::Orientation::VERTICAL, 2};
        Gtk::Button mute;
        Gtk::Label mute_icon;
        Gtk::Label device;
        Gtk::Scale scale;
    };
    Gtk::Box audio_card_{Gtk::Orientation::VERTICAL, 8}; // output over input (user request)
    AudioColumn output_, input_;

    // brightness card
    Gtk::Box brightness_card_{Gtk::Orientation::VERTICAL, 2};
    Gtk::Label brightness_icon_, brightness_title_, brightness_percent_;
    Gtk::Scale brightness_scale_;
    sigc::connection brightness_debounce_;

    // media card
    Gtk::Overlay media_card_;
    MediaBackground media_background_;
    Gtk::Box media_content_{Gtk::Orientation::VERTICAL, 6};
    Gtk::Button player_button_;
    Gtk::Label player_name_;
    Gtk::Popover player_menu_;
    Gtk::Box player_list_{Gtk::Orientation::VERTICAL, 2};
    Gtk::Label media_empty_;
    Gtk::Box media_active_{Gtk::Orientation::VERTICAL, 6};
    Gtk::Label title_, artist_, album_;
    Gtk::Scale progress_;
    Gtk::Button prev_, play_, next_;
    Gtk::Label prev_icon_, play_icon_, next_icon_;
    bool seeking_ = false;
    sigc::connection seek_debounce_, seek_release_;
    std::string shown_player_;

    // system monitor card
    Gtk::Box sysmon_card_{Gtk::Orientation::HORIZONTAL, 0};
    CircleStat* cpu_stat_ = nullptr;
    CircleStat* temp_stat_ = nullptr;
    CircleStat* mem_stat_ = nullptr;
    CircleStat* disk_stat_ = nullptr;
    bool stats_registered_ = false;
};

} // namespace hyprshell
