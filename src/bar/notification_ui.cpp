#include "bar/notification_ui.hpp"

#include <giomm/desktopappinfo.h>

namespace hyprshell {

std::string notification_relative_time(gint64 timestamp_ms) {
    const gint64 diff = g_get_real_time() / 1000 - timestamp_ms;
    if (diff < 60'000)
        return "now";
    if (diff < 120'000)
        return "1 minute ago";
    if (diff < 3'600'000)
        return std::to_string(diff / 60'000) + " minutes ago";
    if (diff < 7'200'000)
        return "1 hour ago";
    if (diff < 86'400'000)
        return std::to_string(diff / 3'600'000) + " hours ago";
    if (diff < 172'800'000)
        return "1 day ago";
    return std::to_string(diff / 86'400'000) + " days ago";
}

Gtk::Widget* make_notification_icon(const NotificationService::Notification& n,
                                    int size_px) {
    auto* frame = Gtk::make_managed<Gtk::Box>();
    frame->add_css_class("notif-icon");
    frame->set_size_request(size_px, size_px);
    frame->set_overflow(Gtk::Overflow::HIDDEN); // clip images to the radius
    // the children expand to center themselves in the frame; setting the
    // frame's own flags explicitly stops that expand propagating to the card
    frame->set_hexpand(false);
    frame->set_vexpand(false);

    if (!n.image.empty() && n.image.front() == '/' &&
        Glib::file_test(n.image, Glib::FileTest::EXISTS)) {
        auto* picture = Gtk::make_managed<Gtk::Picture>();
        picture->set_filename(n.image);
        picture->set_content_fit(Gtk::ContentFit::COVER);
        picture->set_size_request(size_px, size_px);
        frame->append(*picture);
        return frame;
    }

    auto* image = Gtk::make_managed<Gtk::Image>();
    image->set_pixel_size(size_px * 7 / 10);
    image->set_hexpand(true);
    image->set_vexpand(true);
    image->set_halign(Gtk::Align::CENTER);
    image->set_valign(Gtk::Align::CENTER);
    if (!n.image.empty() && n.image.find('/') == std::string::npos &&
        Gtk::IconTheme::get_for_display(Gdk::Display::get_default())->has_icon(n.image)) {
        image->set_from_icon_name(n.image);
        frame->append(*image);
        return frame;
    }
    if (!n.desktop_entry.empty()) {
        for (const auto& id : {n.desktop_entry + ".desktop", n.desktop_entry}) {
            if (auto info = Gio::DesktopAppInfo::create(id)) {
                if (auto icon = info->get_icon()) {
                    image->set(icon);
                    frame->append(*image);
                    return frame;
                }
            }
        }
    }

    auto* fallback = Gtk::make_managed<Gtk::Label>("\uEA35"); // tabler bell
    fallback->add_css_class("notif-icon-fallback");
    fallback->set_hexpand(true);
    fallback->set_vexpand(true);
    fallback->set_halign(Gtk::Align::CENTER);
    fallback->set_valign(Gtk::Align::CENTER);
    frame->append(*fallback);
    return frame;
}

} // namespace hyprshell
