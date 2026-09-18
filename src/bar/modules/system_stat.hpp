#pragma once

#include <gtkmm.h>

namespace hyprshell {

// One of the three system-stat bar modules (`cpu`, `memory`, `disk`): a
// tabler glyph plus an optional percentage label (`bar.<key>.show_text`),
// placed before or after the icon (`text_position`) — on a vertical bar the
// box turns vertical and "before" means above. Hovering shows the detail
// tooltip: every core's usage (+ temperature) for the CPU, used / total for
// memory and the disk. Not clickable, but it shows the bar's hover pill like
// the clickable modules (user request). Polling happens only while the module
// is mapped: `SystemStats` counts it as a consumer between map and unmap.
class SystemStatModule : public Gtk::Box {
public:
    enum class Kind { Cpu, Memory, Disk };

    explicit SystemStatModule(Kind kind);
    ~SystemStatModule() override;

private:
    void apply_config();
    void update();

    Kind kind_;
    Gtk::Label icon_;
    Gtk::Label text_;
    bool registered_ = false;
};

} // namespace hyprshell
