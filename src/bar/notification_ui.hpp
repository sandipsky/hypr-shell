#pragma once

#include "services/notifications.hpp"

#include <gtkmm.h>

#include <string>

namespace hyprshell {

// Shared between the history panel and the popup toasts.

// Noctalia's Time.formatRelativeTime ("now", "5 minutes ago", ...).
std::string notification_relative_time(gint64 timestamp_ms);

// Rounded notification icon (size_px square): image file (image-data cache or
// sender path) > themed icon > desktop-entry icon > bell glyph fallback.
// Returns a managed widget.
Gtk::Widget* make_notification_icon(const NotificationService::Notification& n,
                                    int size_px);

} // namespace hyprshell
