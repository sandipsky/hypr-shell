#pragma once

#include "services/notifications.hpp"

#include <gtkmm.h>

#include <string>
#include <vector>

namespace hyprshell {

// Notification toast stack — Noctalia's Notification.qml as a layer-shell
// window: up to five cards anchored per notifications.location, per-urgency
// countdown drawn as the 2px progress bar (shrinking from both ends), hover
// pauses the countdown, click activates (default action / focus sender),
// right-click and the close button dismiss (optionally deleting the history
// entry, notifications.clear_dismissed). "compact" density: small single-line
// cards without header or close button.
class NotificationPopups : public Gtk::Window {
public:
    NotificationPopups();

private:
    void apply_config(); // layer + anchors per notifications.*
    void rebuild();
    void add_card(const NotificationService::Popup& popup);
    void redraw_progress();

    Gtk::Box stack_{Gtk::Orientation::VERTICAL, 9};
    std::vector<std::pair<std::string, Gtk::DrawingArea*>> progress_bars_;
    Glib::RefPtr<Gtk::CssProvider> opacity_provider_; // background_opacity rule
};

} // namespace hyprshell
