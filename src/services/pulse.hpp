#pragma once

#include <pulse/glib-mainloop.h>
#include <pulse/pulseaudio.h>
#include <sigc++/sigc++.h>

#include <string>

namespace hyprshell {

// Default-sink volume/mute via the PulseAudio client API (served by
// pipewire-pulse here). The pa_glib_mainloop dispatches every callback on the
// GTK main loop, so no locking is needed anywhere.
class Pulse {
public:
    static Pulse& get();

    Pulse(const Pulse&) = delete;
    Pulse& operator=(const Pulse&) = delete;

    bool available() const { return available_; }
    double volume() const { return volume_; } // 0.0 .. 1.0+ (overdrive possible)
    bool muted() const { return muted_; }

    sigc::signal<void()>& signal_changed() { return changed_; }

private:
    Pulse();

    void connect_context();
    void query_server();

    static void on_context_state(pa_context* context, void* self);
    static void on_subscribe_event(pa_context* context, pa_subscription_event_type_t type,
                                   uint32_t index, void* self);
    static void on_server_info(pa_context* context, const pa_server_info* info, void* self);
    static void on_sink_info(pa_context* context, const pa_sink_info* info, int eol, void* self);

    pa_glib_mainloop* mainloop_ = nullptr;
    pa_context* context_ = nullptr;

    bool available_ = false;
    double volume_ = 0.0;
    bool muted_ = false;
    sigc::signal<void()> changed_;
};

} // namespace hyprshell
