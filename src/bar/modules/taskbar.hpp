#pragma once

#include <gtkmm.h>

#include <cstdint>
#include <string>
#include <vector>

namespace hyprshell {

// Taskbar (Noctalia's Taskbar bar widget): one icon per running window,
// filtered to the bar's monitor and the active workspaces, merged with the
// pinned apps (shared store with the launcher) in pinned order. Focused /
// hovered indicator bar under the icon, click focuses or launches, wheel
// cycles focus, drag reorders (pinned order persisted). No right-click
// menu and no capsule background, per user.
// Hide modes: always visible, hidden or transparent when nothing matches.
class Taskbar : public Gtk::Box {
public:
    Taskbar();
    ~Taskbar() override;

private:
    struct Window {
        std::string address; // j/clients "0x…"
        std::string app_id;  // class (initialClass fallback)
        std::string title;
        int workspace_id = -1;
        std::string output;
        bool focused = false;
        int x = 0, y = 0;
    };
    enum class ItemType { Running, PinnedRunning, Pinned };
    struct Item {
        std::string id; // window address, or the pinned id
        ItemType type = ItemType::Running;
        int window = -1; // index into windows_, -1 for pinned-only
        std::string app_id;
        std::string title;
        Gtk::Widget* root = nullptr;       // built by rebuild()
        Gtk::Label* title_label = nullptr; // with show_title
    };

    // data
    void on_event(const std::string& name, const std::string& data);
    void schedule_refresh();
    void refresh(); // j/monitors → j/clients → j/activewindow, serial-guarded
    void update_model();
    std::vector<Item> sort_items(std::vector<Item> items) const;
    std::string item_key(const Item& item) const;
    bool is_pinned_app(const std::string& app_id) const;
    std::string desktop_id_for(const std::string& app_id) const;
    std::string app_name_for(const std::string& app_id) const;
    std::string bar_output();
    int screen_width();

    // ui: a full rebuild only when the item structure or layout changed;
    // otherwise (title / focus changes) the existing widgets are refreshed
    void rebuild(const std::vector<Item>& previous);
    void refresh_item(std::size_t index);
    Gtk::Widget* build_item(std::size_t index, int item_size, int title_width);
    void apply_hide_mode();
    void animate_opacity(double target);
    void set_hovered(const std::string& id, bool hovered);
    bool on_scroll(double dx, double dy);
    void reorder(std::size_t from, std::size_t to);
    void save_pinned_order();
    void launch_pinned(const std::string& app_id);

    Gtk::Box capsule_{Gtk::Orientation::HORIZONTAL, 0};
    Gtk::Box items_box_{Gtk::Orientation::HORIZONTAL, 2};

    std::vector<Window> windows_;
    std::vector<int> active_workspaces_;
    std::string active_address_;
    std::vector<Item> items_;
    std::string layout_key_;                 // vertical / gap / sizes of the built widgets
    std::vector<std::string> session_order_; // transient running-app order
    std::string hovered_id_;
    uint64_t serial_ = 0;
    sigc::connection refresh_timer_;

    double scroll_accum_ = 0.0;
    bool wheel_cooldown_ = false;
    sigc::connection wheel_timer_;

    double opacity_from_ = 1.0;
    double opacity_target_ = 1.0;
    gint64 fade_start_us_ = 0;
    bool fade_running_ = false;
};

} // namespace hyprshell
