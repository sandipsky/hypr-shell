#pragma once

#include "services/notifications.hpp"

#include <gtkmm.h>

#include <string>

namespace hyprshell {

// Notification history popover content — Noctalia's NotificationHistoryPanel
// with the user's changes: no close button, no All/Today/Yesterday/Earlier
// tabs (everything in one list), and the header holds a "Clear All" button
// (trash icon + text; the do-not-disturb toggle lives in hypr-shell-settings
// and on the bell's right click). Cards keep Noctalia's layout: app
// image/icon, urgency dot, app name, relative time, summary, body, action
// buttons, expand + delete.
class NotificationPanel : public Gtk::Box {
public:
    NotificationPanel();

    void set_open(bool open); // popover mapped state — drives rebuilds + timers

    // Clear All / activating a notification wants the popover closed.
    sigc::signal<void()>& signal_request_close() { return request_close_; }

private:
    void rebuild();
    void add_card(const NotificationService::Notification& n);

    bool open_ = false;
    std::string expanded_id_;

    // shown instead of the list while the history is empty (Noctalia look)
    Gtk::Box empty_card_{Gtk::Orientation::VERTICAL, 6};

    Gtk::ScrolledWindow scroller_;
    Gtk::Box list_{Gtk::Orientation::VERTICAL, 9};

    sigc::connection refresh_timer_; // relative times tick while open
    sigc::signal<void()> request_close_;
};

} // namespace hyprshell
