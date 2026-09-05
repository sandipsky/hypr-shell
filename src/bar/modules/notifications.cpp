#include "bar/modules/notifications.hpp"

#include "bar/bar_popover.hpp"

#include "services/config.hpp"
#include "services/notifications.hpp"

#include <algorithm>
#include <cstdlib>

namespace hyprshell {

namespace {

// noctalia-tabler-icons glyphs (\u escapes — never literal PUA)
constexpr const char* kBell = "\uEA35";
constexpr const char* kBellOff = "\uECE9";

} // namespace

Notifications::Notifications() : Gtk::Box(Gtk::Orientation::HORIZONTAL, 0) {
    add_css_class("module");
    add_css_class("notifications");
    icon_.add_css_class("icon");

    // unread dot at the icon's top-right corner (Noctalia's badge)
    badge_.add_css_class("notif-badge");
    badge_.set_halign(Gtk::Align::END);
    badge_.set_valign(Gtk::Align::START);
    badge_.set_can_target(false);
    overlay_.set_child(icon_);
    overlay_.add_overlay(badge_);
    append(overlay_);

    // anchored to the overlay, never the module Box (see battery.cpp)
    panel_ = Gtk::make_managed<NotificationPanel>();
    popover_.set_child(*panel_);
    popover_.set_parent(overlay_);
    popover_.set_has_arrow(false);
    popover_.add_css_class("notification-popover");

    auto click = Gtk::GestureClick::create();
    click->signal_released().connect([this](int, double, double) {
        // keep the panel on the free side of the bar
        place_bar_popover(popover_);
        panel_->set_open(true); // marks history as seen — clears the badge
        popover_.popup();
    });
    add_controller(click);
    popover_.signal_closed().connect([this] { panel_->set_open(false); });
    panel_->signal_request_close().connect([this] { popover_.popdown(); });

    // right click toggles do-not-disturb (Noctalia has it in a context menu)
    auto right_click = Gtk::GestureClick::create();
    right_click->set_button(GDK_BUTTON_SECONDARY);
    right_click->signal_released().connect([](int, double, double) {
        auto& service = NotificationService::get();
        service.set_do_not_disturb(!service.do_not_disturb());
    });
    add_controller(right_click);

    // dev hook: HS_OPEN_NOTIFICATIONS=1 pops the panel shortly after startup
    if (const char* hook = g_getenv("HS_OPEN_NOTIFICATIONS")) {
        const int delay = std::max(800, std::atoi(hook)); // >1 = delay in ms
        Glib::signal_timeout().connect_once(
            [this] {
                panel_->set_open(true);
                place_bar_popover(popover_);
                popover_.popup();
            },
            delay);
    }

    NotificationService::get().signal_changed().connect(
        sigc::mem_fun(*this, &Notifications::update));
    Config::get().signal_changed().connect(sigc::mem_fun(*this, &Notifications::update));
    update();
}

Notifications::~Notifications() {
    popover_.unparent();
}

void Notifications::update() {
    auto& service = NotificationService::get();
    auto& cfg = Config::get();

    const int total = static_cast<int>(service.history().size());
    const int unread = service.unread_count();

    icon_.set_text(service.do_not_disturb() ? kBellOff : kBell);
    badge_.set_visible(cfg.notifications_show_badge() && unread > 0);
    // Noctalia's hideWhenZero / hideWhenZeroUnread widget settings
    set_visible(!(cfg.notifications_hide_when_zero() && total == 0) &&
                !(cfg.notifications_hide_when_zero_unread() && unread == 0));

    std::string tooltip = "Notifications";
    if (service.do_not_disturb())
        tooltip += " (do not disturb)";
    else if (unread > 0)
        tooltip += ": " + std::to_string(unread) + " new";
    if (!service.available())
        tooltip += "\nAnother notification daemon is running";
    set_tooltip_text(tooltip);
}

} // namespace hyprshell
