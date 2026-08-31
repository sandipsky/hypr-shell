#include "services/pulse.hpp"

#include <glibmm.h>

#include <algorithm>
#include <cmath>

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
                                                    PA_SUBSCRIPTION_MASK_SOURCE |
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
        self->input_available_ = false;
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
    // Any sink/source/server event may change the defaults or their volume;
    // state is tiny, so re-query instead of tracking indices.
    static_cast<Pulse*>(self_ptr)->query_server();
}

void Pulse::query_server() {
    if (auto* op = pa_context_get_server_info(context_, &Pulse::on_server_info, this)) {
        pa_operation_unref(op);
    }
}

void Pulse::on_server_info(pa_context* context, const pa_server_info* info, void* self_ptr) {
    if (info == nullptr) {
        return;
    }
    if (info->default_sink_name != nullptr) {
        if (auto* op = pa_context_get_sink_info_by_name(context, info->default_sink_name,
                                                        &Pulse::on_sink_info, self_ptr)) {
            pa_operation_unref(op);
        }
    }
    if (info->default_source_name != nullptr) {
        if (auto* op = pa_context_get_source_info_by_name(context, info->default_source_name,
                                                          &Pulse::on_source_info, self_ptr)) {
            pa_operation_unref(op);
        }
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
    self->sink_name_ = info->name != nullptr ? info->name : "";
    self->sink_desc_ = info->description != nullptr ? info->description : "";
    self->sink_channels_ = info->volume.channels;
    self->changed_.emit();
}

void Pulse::on_source_info(pa_context*, const pa_source_info* info, int eol, void* self_ptr) {
    if (eol != 0 || info == nullptr) {
        return;
    }
    auto* self = static_cast<Pulse*>(self_ptr);
    self->input_available_ = true;
    self->input_volume_ = static_cast<double>(pa_cvolume_avg(&info->volume)) / PA_VOLUME_NORM;
    self->input_muted_ = info->mute != 0;
    self->source_name_ = info->name != nullptr ? info->name : "";
    self->source_desc_ = info->description != nullptr ? info->description : "";
    self->source_channels_ = info->volume.channels;
    self->changed_.emit();
}

void Pulse::set_volume(double volume) {
    if (!available_ || context_ == nullptr || sink_name_.empty()) {
        return;
    }
    volume_ = std::max(0.0, volume);
    changed_.emit();
    pa_cvolume cv;
    pa_cvolume_set(&cv, sink_channels_,
                   static_cast<pa_volume_t>(std::lround(volume_ * PA_VOLUME_NORM)));
    if (auto* op = pa_context_set_sink_volume_by_name(context_, sink_name_.c_str(), &cv,
                                                      nullptr, nullptr)) {
        pa_operation_unref(op);
    }
}

void Pulse::set_muted(bool muted) {
    if (!available_ || context_ == nullptr || sink_name_.empty()) {
        return;
    }
    muted_ = muted;
    changed_.emit();
    if (auto* op = pa_context_set_sink_mute_by_name(context_, sink_name_.c_str(),
                                                    muted ? 1 : 0, nullptr, nullptr)) {
        pa_operation_unref(op);
    }
}

void Pulse::set_input_volume(double volume) {
    if (!input_available_ || context_ == nullptr || source_name_.empty()) {
        return;
    }
    input_volume_ = std::max(0.0, volume);
    changed_.emit();
    pa_cvolume cv;
    pa_cvolume_set(&cv, source_channels_,
                   static_cast<pa_volume_t>(std::lround(input_volume_ * PA_VOLUME_NORM)));
    if (auto* op = pa_context_set_source_volume_by_name(context_, source_name_.c_str(), &cv,
                                                        nullptr, nullptr)) {
        pa_operation_unref(op);
    }
}

void Pulse::set_input_muted(bool muted) {
    if (!input_available_ || context_ == nullptr || source_name_.empty()) {
        return;
    }
    input_muted_ = muted;
    changed_.emit();
    if (auto* op = pa_context_set_source_mute_by_name(context_, source_name_.c_str(),
                                                      muted ? 1 : 0, nullptr, nullptr)) {
        pa_operation_unref(op);
    }
}

} // namespace hyprshell
