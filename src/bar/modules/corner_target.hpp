#pragma once

#include <gtkmm/widget.h>

#include <cmath>
#include <vector>

namespace hyprshell {

// Modules whose click the bar can trigger from its padding, where nothing
// else listens. Two rules (Windows' taskbar behaviour):
//  - beside: a click across the bar's thickness within the module's span along
//    the bar (the strip above / below a horizontal bar's module) counts as a
//    click on that module — `activate_beside(x, y)`, coordinates relative to
//    the module; multi-item modules pick the item under the point.
//  - corner: when a module is the outermost one — first in the start section
//    or last in the end section (the centre section's first / last when that
//    side is empty), any orientation — a click between it and the screen
//    corner counts as a click on it. `start` says which corner: true = before
//    the first module, false = after the last. Modules with several items
//    (workspaces, taskbar) use it to pick their first or last item.
class CornerTarget {
public:
    virtual ~CornerTarget() = default;
    virtual void activate_corner(bool start) = 0;
    virtual void activate_beside(double /*x*/, double /*y*/) { activate_corner(true); }
    // The same two places for a right click: a module with a secondary action
    // (app menu → session menu, volume → mute, bell → do-not-disturb) mirrors
    // it here; the default does nothing.
    virtual void secondary_corner(bool /*start*/) {}
    virtual void secondary_beside(double /*x*/, double /*y*/) { secondary_corner(true); }
};

// Index of the child whose extent along the bar contains the point (relative
// to `parent`), else the nearest child; -1 with no children.
inline int pick_child_along(Gtk::Widget& parent, const std::vector<Gtk::Widget*>& children,
                            double x, double y, bool vertical) {
    int best = -1;
    double best_distance = 0.0;
    for (std::size_t i = 0; i < children.size(); ++i) {
        const auto bounds = children[i]->compute_bounds(parent);
        if (!bounds)
            continue;
        const double lo = vertical ? bounds->get_y() : bounds->get_x();
        const double hi = lo + (vertical ? bounds->get_height() : bounds->get_width());
        const double p = vertical ? y : x;
        if (p >= lo && p <= hi)
            return static_cast<int>(i);
        const double distance = p < lo ? lo - p : p - hi;
        if (best < 0 || distance < best_distance) {
            best = static_cast<int>(i);
            best_distance = distance;
        }
    }
    return best;
}

} // namespace hyprshell
