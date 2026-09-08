#pragma once

#include "bar/notification_panel.hpp"

#include "bar/modules/corner_target.hpp"

#include <gtkmm.h>

namespace hyprshell {

// Notification bell (tabler glyphs): bell, bell-off while do-not-disturb, plus
// a small unread badge, like Noctalia's NotificationHistory bar widget. Click
// opens the history panel; right click toggles do-not-disturb.
class Notifications : public Gtk::Box, public CornerTarget {
public:
    void open();
    void activate_corner(bool) override { open(); }
    Notifications();
    ~Notifications() override;

private:
    void update();

    Gtk::Overlay overlay_; // icon + badge; also the popover anchor
    Gtk::Label icon_;
    Gtk::Box badge_;
    Gtk::Popover popover_;
    NotificationPanel* panel_ = nullptr;
};

} // namespace hyprshell
