#pragma once

#include <pulse/glib-mainloop.h>
#include <pulse/pulseaudio.h>
#include <sigc++/sigc++.h>

#include <string>

namespace hyprshell {

// Default sink + source volume/mute via the PulseAudio client API (served by
// pipewire-pulse here). The pa_glib_mainloop dispatches every callback on the
// GTK main loop, so no locking is needed anywhere.
class Pulse {
public:
    static Pulse& get();

    Pulse(const Pulse&) = delete;
    Pulse& operator=(const Pulse&) = delete;

    // default sink (output)
    bool available() const { return available_; }
    double volume() const { return volume_; } // 0.0 .. 1.0+ (overdrive possible)
    bool muted() const { return muted_; }
    const std::string& description() const { return sink_desc_; }

    // default source (input)
    bool input_available() const { return input_available_; }
    double input_volume() const { return input_volume_; }
    bool input_muted() const { return input_muted_; }
    const std::string& input_description() const { return source_desc_; }

    // Writers update the local state optimistically (and emit) so sliders
    // don't bounce; the server's subscribe event then confirms.
    void set_volume(double volume);
    void set_muted(bool muted);
    void set_input_volume(double volume);
    void set_input_muted(bool muted);

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
    static void on_source_info(pa_context* context, const pa_source_info* info, int eol,
                               void* self);

    pa_glib_mainloop* mainloop_ = nullptr;
    pa_context* context_ = nullptr;

    bool available_ = false;
    double volume_ = 0.0;
    bool muted_ = false;
    std::string sink_name_;
    std::string sink_desc_;
    uint8_t sink_channels_ = 2;

    bool input_available_ = false;
    double input_volume_ = 0.0;
    bool input_muted_ = false;
    std::string source_name_;
    std::string source_desc_;
    uint8_t source_channels_ = 2;

    sigc::signal<void()> changed_;
};

} // namespace hyprshell
