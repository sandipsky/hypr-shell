#pragma once

#include <gtkmm/popover.h>

#include "services/config.hpp"

namespace hyprshell {

// Gap between the bar and a module's popover, in px. Bar popovers have no
// arrow, so without an offset they sit flush against the bar edge.
inline constexpr int kBarPopoverGap = 6;

// Place a module popover on the free side of the bar (below a top bar, above
// a bottom bar, ...) and push it kBarPopoverGap away from the bar. Call it
// right before popup(): the bar position may have changed since the last open.
inline void place_bar_popover(Gtk::Popover& popover) {
    switch (Config::get().bar_position()) {
    case Config::BarPosition::Top:
        popover.set_position(Gtk::PositionType::BOTTOM);
        popover.set_offset(0, kBarPopoverGap);
        break;
    case Config::BarPosition::Bottom:
        popover.set_position(Gtk::PositionType::TOP);
        popover.set_offset(0, -kBarPopoverGap);
        break;
    case Config::BarPosition::Left:
        popover.set_position(Gtk::PositionType::RIGHT);
        popover.set_offset(kBarPopoverGap, 0);
        break;
    case Config::BarPosition::Right:
        popover.set_position(Gtk::PositionType::LEFT);
        popover.set_offset(-kBarPopoverGap, 0);
        break;
    }
}

} // namespace hyprshell
