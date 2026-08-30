#include "services/pulse.hpp"

#include <glibmm.h>

namespace hyprshell {

Pulse& Pulse::get() {
    static Pulse instance;
    return instance;
}

Pulse::Pulse() {
    mainloop_ = pa_glib_mainloop_new(nullptr);
    connect_context();
}

void Pulse::connect_context() {
    context_ = pa_context_new(pa_glib_mainloop_get_api(mainloop_), "hypr-shell");
    pa_context_set_state_callback(context_, &Pulse::on_context_state, this);
    // NOFAIL keeps the context in CONNECTING while the server is missing and
    // reconnects when it appears, so a pipewire restart heals on its own.
    pa_context_connect(context_, nullptr,
                       static_cast<pa_context_flags_t>(PA_CONTEXT_NOAUTOSPAWN | PA_CONTEXT_NOFAIL),
                       nullptr);
}

void Pulse::on_context_state(pa_context* context, void* self_ptr) {
    auto* self = static_cast<Pulse*>(self_ptr);
    switch (pa_context_get_state(context)) {
    case PA_CONTEXT_READY:
        pa_context_set_subscribe_callback(context, &Pulse::on_subscribe_event, self);
        if (auto* op = pa_context_subscribe(
                context,
                static_cast<pa_subscription_mask_t>(PA_SUBSCRIPTION_MASK_SINK |
                                                    PA_SUBSCRIPTION_MASK_SERVER),
                nullptr, nullptr)) {
            pa_operation_unref(op);
        }
        self->query_server();
        break;
    case PA_CONTEXT_FAILED:
    case PA_CONTEXT_TERMINATED:
        // NOFAIL makes this rare; retry from scratch after a pause.
        self->available_ = false;
        self->changed_.emit();
        pa_context_unref(self->context_);
        self->context_ = nullptr;
        Glib::signal_timeout().connect_seconds_once([self] { self->connect_context(); }, 5);
        break;
    default:
        break;
    }
}

void Pulse::on_subscribe_event(pa_context*, pa_subscription_event_type_t, uint32_t, void* self_ptr) {
    // Any sink or server event may change the default sink or its volume;
    // state is tiny, so re-query instead of tracking indices.
    static_cast<Pulse*>(self_ptr)->query_server();
}

void Pulse::query_server() {
    if (auto* op = pa_context_get_server_info(context_, &Pulse::on_server_info, this)) {
        pa_operation_unref(op);
    }
}

void Pulse::on_server_info(pa_context* context, const pa_server_info* info, void* self_ptr) {
    if (info == nullptr || info->default_sink_name == nullptr) {
        return;
    }
    if (auto* op = pa_context_get_sink_info_by_name(context, info->default_sink_name,
                                                    &Pulse::on_sink_info, self_ptr)) {
        pa_operation_unref(op);
    }
}

void Pulse::on_sink_info(pa_context*, const pa_sink_info* info, int eol, void* self_ptr) {
    if (eol != 0 || info == nullptr) {
        return;
    }
    auto* self = static_cast<Pulse*>(self_ptr);
    self->available_ = true;
    self->volume_ = static_cast<double>(pa_cvolume_avg(&info->volume)) / PA_VOLUME_NORM;
    self->muted_ = info->mute != 0;
    self->changed_.emit();
}

} // namespace hyprshell
