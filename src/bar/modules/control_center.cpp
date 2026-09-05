#include "bar/modules/control_center.hpp"

#include "bar/bar_popover.hpp"

namespace hyprshell {

namespace {
// tabler "adjustments-horizontal" (sliders) — Noctalia's own control-center
// icon; the gear is taken by the Settings button inside the panel (per user)
constexpr const char* kIcon = "\uEC38";
}

ControlCenter::ControlCenter() : Gtk::Box(Gtk::Orientation::HORIZONTAL, 0) {
    add_css_class("module");
    add_css_class("control-center-module");
    icon_.set_text(kIcon);
    icon_.add_css_class("icon");
    append(icon_);
    set_tooltip_text("Control center");
    set_cursor(Gdk::Cursor::create("pointer"));

    panel_ = Gtk::make_managed<ControlCenterPanel>();
    popover_.set_child(*panel_);
    popover_.set_parent(icon_); // popover-anchor gotcha: never the module Box
    popover_.set_has_arrow(false);
    popover_.add_css_class("control-center-popover");
    popover_.signal_closed().connect([this] { panel_->set_open(false); });
    panel_->signal_request_close().connect([this] { popover_.popdown(); });

    auto click = Gtk::GestureClick::create();
    click->signal_released().connect([this](int, double, double) { toggle(); });
    add_controller(click);

    // dev hook: HS_OPEN_CONTROL_CENTER=1 pops the panel shortly after startup
    if (g_getenv("HS_OPEN_CONTROL_CENTER") != nullptr)
        Glib::signal_timeout().connect_once([this] { toggle(); }, 800);
}

ControlCenter::~ControlCenter() {
    popover_.unparent();
}

void ControlCenter::toggle() {
    if (popover_.get_visible()) {
        popover_.popdown();
        return;
    }
    place_bar_popover(popover_);
    panel_->set_open(true);
    popover_.popup();
}

} // namespace hyprshell
