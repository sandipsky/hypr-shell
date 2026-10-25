#pragma once

#include <gtkmm.h>

namespace hyprshell {

// Noctalia's NBusyIndicator: a 270° arc (round caps) rotating once per
// 900ms (Style.animationSlow * 2). Drawn with cairo in the widget's CSS
// `color`, so a `.np-spinner { color: … }` rule themes it. The tick callback
// only runs while the widget is mapped and running, so hidden spinners cost
// nothing.
class BusyIndicator : public Gtk::DrawingArea {
public:
    explicit BusyIndicator(int size = 16, double stroke = 2.0);

    void set_running(bool running);
    bool running() const { return running_; }

private:
    void update_tick();
    void draw(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height);

    double stroke_;
    bool running_ = true;
    guint tick_id_ = 0;
};

} // namespace hyprshell
