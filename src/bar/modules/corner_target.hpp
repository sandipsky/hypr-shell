#pragma once

namespace hyprshell {

// Modules whose primary click the bar can trigger from its corners (Windows'
// Start-button rule): when a module is the outermost one — first in the start
// section or last in the end section, any orientation — a click between it and
// the screen corner (the bar's padding, where nothing else listens) counts as
// a click on it. `start` says which corner: true = before the first module,
// false = after the last. Modules with several items (workspaces, taskbar) use
// it to pick their first or last item.
class CornerTarget {
public:
    virtual ~CornerTarget() = default;
    virtual void activate_corner(bool start) = 0;
};

} // namespace hyprshell
