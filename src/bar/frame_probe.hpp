#pragma once

#include <gtkmm.h>

namespace hyprshell {

// Dev hook: with HS_FRAME_DEBUG=1, log the milliseconds from this call to the
// first frame painted on `widget`'s surface — the cost of a first open (icon
// rasterization, CSS validation, the surface's first GL draw). Call it right
// before popup() / present(); `what` names the window in the log line.
inline void log_first_frame(Gtk::Widget& widget, const char* what) {
    if (g_getenv("HS_FRAME_DEBUG") == nullptr)
        return;
    struct Probe {
        gint64 t0;
        const char* what;
        gulong id;
    };
    const gint64 t0 = g_get_monotonic_time();
    widget.add_tick_callback([t0, what](const Glib::RefPtr<Gdk::FrameClock>& clock) {
        auto* probe = new Probe{t0, what, 0};
        probe->id = g_signal_connect_data(
            clock->gobj(), "after-paint",
            G_CALLBACK(+[](GdkFrameClock* c, gpointer data) {
                auto* p = static_cast<Probe*>(data);
                g_message("%s: first frame painted %.1f ms after open", p->what,
                          (g_get_monotonic_time() - p->t0) / 1000.0);
                g_signal_handler_disconnect(c, p->id);
            }),
            probe, +[](gpointer data, GClosure*) { delete static_cast<Probe*>(data); },
            GConnectFlags(0));
        return false;
    });
}

} // namespace hyprshell
