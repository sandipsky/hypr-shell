#pragma once

#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <gdkmm/graphene_rect.h>
#include <gtkmm/popover.h>
#include <gtkmm/root.h>

#include "services/config.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace hyprshell {

// Gap between the bar and a module's popover, in px. Bar popovers have no
// arrow, so without an offset they sit flush against the bar edge.
inline constexpr int kBarPopoverGap = 6;

// Place a module popover on the free side of the bar (below a top bar, above
// a bottom bar, ...) and push it kBarPopoverGap away from the bar. Call it
// right before popup(): the bar position may have changed since the last open.
//
// Along the bar the popover is normally centred on its anchor. When that
// would cross the bar window's edge (= the monitor edge; the bar spans it),
// the popover is instead hung off the anchor's near edge via its own
// halign/valign — GTK maps START/END to the corner gravities — so it stays
// on screen regardless of the compositor's slide constraint, which Hyprland
// does not always apply to layer-shell popups (the 440px control-center
// popover was seen cut off at the screen's right edge).
inline void place_bar_popover(Gtk::Popover& popover) {
    const auto position = Config::get().bar_position();
    const bool vertical =
        position == Config::BarPosition::Left || position == Config::BarPosition::Right;

    auto* anchor = popover.get_parent();
    auto* window = anchor ? dynamic_cast<Gtk::Widget*>(anchor->get_root()) : nullptr;
    const auto bounds = anchor && window ? anchor->compute_bounds(*window)
                                         : std::optional<Gdk::Graphene::Rect>();
    auto* child = popover.get_child();

    // GTK hangs the popover off the ANCHOR's edge, so the gap must include the
    // distance from the anchor to the bar's outer edge: a 19px icon in a 35px
    // bar sits 8px in from the inner edge on a horizontal bar but 9px on a
    // vertical one, and a bare 6px offset left the popover overlapping the bar
    // (user report: "no spacing on vertical bars").
    double to_edge = 0;
    if (bounds && window) {
        switch (position) {
        case Config::BarPosition::Top:
            to_edge = window->get_height() - (bounds->get_y() + bounds->get_height());
            break;
        case Config::BarPosition::Bottom:
            to_edge = bounds->get_y();
            break;
        case Config::BarPosition::Left:
            to_edge = window->get_width() - (bounds->get_x() + bounds->get_width());
            break;
        case Config::BarPosition::Right:
            to_edge = bounds->get_x();
            break;
        }
        to_edge = std::max(0.0, to_edge);
    }
    const int offset = kBarPopoverGap + static_cast<int>(std::lround(to_edge));
    switch (position) {
    case Config::BarPosition::Top:
        popover.set_position(Gtk::PositionType::BOTTOM);
        popover.set_offset(0, offset);
        break;
    case Config::BarPosition::Bottom:
        popover.set_position(Gtk::PositionType::TOP);
        popover.set_offset(0, -offset);
        break;
    case Config::BarPosition::Left:
        popover.set_position(Gtk::PositionType::RIGHT);
        popover.set_offset(offset, 0);
        break;
    case Config::BarPosition::Right:
        popover.set_position(Gtk::PositionType::LEFT);
        popover.set_offset(-offset, 0);
        break;
    }

    // Alignment along the bar: centre unless that runs off the window.
    auto align = Gtk::Align::CENTER;
    if (bounds && child) {
        // A hidden popover measures 0x0, so measure its child (the panel): a
        // widget's measure works before mapping. Add the popover contents'
        // CSS padding (12px per side at most in our themes) since the padding
        // must stay on screen too; the shadow may hang off, it's excluded.
        int minimum = 0, natural = 0, min_baseline = 0, nat_baseline = 0;
        child->measure(vertical ? Gtk::Orientation::VERTICAL : Gtk::Orientation::HORIZONTAL,
                       -1, minimum, natural, min_baseline, nat_baseline);
        const double half = natural / 2.0 + 12;
        const double extent = vertical ? window->get_height() : window->get_width();
        const double centre = vertical ? bounds->get_y() + bounds->get_height() / 2.0
                                       : bounds->get_x() + bounds->get_width() / 2.0;
        if (centre + half > extent)
            align = Gtk::Align::END; // popover's far edge = anchor's far edge
        else if (centre - half < 0)
            align = Gtk::Align::START;
    }
    if (vertical) {
        popover.set_halign(Gtk::Align::FILL);
        popover.set_valign(align);
    } else {
        popover.set_valign(Gtk::Align::FILL);
        popover.set_halign(align);
    }

    // dev hook: HS_POPOVER_DEBUG=1 logs what GTK measured and where the
    // compositor finally put the popup (positions relative to the bar surface)
    if (g_getenv("HS_POPOVER_DEBUG") != nullptr && bounds && window && child) {
        int minimum = 0, natural = 0, mb = 0, nb = 0;
        child->measure(Gtk::Orientation::HORIZONTAL, -1, minimum, natural, mb, nb);
        int hmin = 0, hnat = 0;
        child->measure(Gtk::Orientation::VERTICAL, -1, hmin, hnat, mb, nb);
        g_message("popover: anchor %.0f,%.0f %.0fx%.0f in window %dx%d, natural %dx%d, align %d",
                  bounds->get_x(), bounds->get_y(), bounds->get_width(), bounds->get_height(),
                  window->get_width(), window->get_height(), natural, hnat,
                  static_cast<int>(align));
        Glib::signal_timeout().connect_once(
            [&popover] {
                GdkSurface* raw = gtk_native_get_surface(GTK_NATIVE(popover.gobj()));
                if (raw == nullptr || !GDK_IS_POPUP(raw)) {
                    g_message("popover: no popup surface (%p, mapped %d)", static_cast<void*>(raw),
                              popover.get_mapped());
                    return;
                }
                auto* popup = GDK_POPUP(raw);
                auto surface = Glib::wrap(raw, true);
                g_message("popover: placed at %d,%d size %dx%d (rect anchor %d, surface anchor %d)",
                          gdk_popup_get_position_x(popup), gdk_popup_get_position_y(popup),
                          surface->get_width(), surface->get_height(),
                          static_cast<int>(gdk_popup_get_rect_anchor(popup)),
                          static_cast<int>(gdk_popup_get_surface_anchor(popup)));
            },
            600);
    }
}

} // namespace hyprshell
