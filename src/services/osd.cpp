#include "services/osd.hpp"

#include "services/brightness.hpp"
#include "services/config.hpp"
#include "services/pulse.hpp"

#include <cmath>

namespace hyprshell {

namespace {

constexpr unsigned kStartupGraceMs = 2000;    // Noctalia's startupTimer
constexpr unsigned kInputSuppressionMs = 300; // Noctalia's inputSuppressionTimer

} // namespace

Osd& Osd::get() {
    static Osd instance;
    return instance;
}

Osd::Osd() {
    Pulse::get().signal_changed().connect(sigc::mem_fun(*this, &Osd::on_pulse_changed));
    Brightness::get().signal_changed().connect(
        sigc::mem_fun(*this, &Osd::on_brightness_changed));
    LockKeys::get().signal_changed().connect(sigc::mem_fun(*this, &Osd::on_lock_key_changed));
    Config::get().signal_changed().connect(sigc::mem_fun(*this, &Osd::apply_config));
    apply_config();
    Glib::signal_timeout().connect_once([this] { startup_complete_ = true; }, kStartupGraceMs);
}

void Osd::apply_config() {
    LockKeys::get().set_enabled(Config::get().osd().enabled);
}

Osd::Type Osd::type_from_key(const std::string& key) {
    if (key == "input")
        return Type::InputVolume;
    if (key == "brightness")
        return Type::Brightness;
    if (key == "lock")
        return Type::LockKey;
    return Type::Volume;
}

void Osd::show(Type type) {
    if (type == Type::LockKey && lock_key_text_.empty()) {
        lock_key_on_ = LockKeys::get().caps_lock();
        lock_key_text_ = std::string("CAPS ") + (lock_key_on_ ? "ON" : "OFF");
    }
    type_ = type;
    show_.emit(type);
}

void Osd::request(Type type) {
    if (!startup_complete_ || !Config::get().osd().enabled)
        return;
    if ((type == Type::Volume || type == Type::InputVolume) && audio_panel_open_)
        return;
    if (type == Type::Brightness && brightness_panel_open_)
        return;
    if (type == Type::InputVolume && suppress_input_)
        return;
    show(type);
}

void Osd::on_pulse_changed() {
    auto& pulse = Pulse::get();
    if (!pulse.available()) {
        pulse_synced_ = false;
        return;
    }
    const bool volume_changed = std::fabs(pulse.volume() - volume_) > 1e-6 ||
                                pulse.muted() != muted_;
    const bool sink_changed = pulse.description() != sink_desc_;
    const bool input_changed = pulse.input_available() != input_available_ ||
                               std::fabs(pulse.input_volume() - input_volume_) > 1e-6 ||
                               pulse.input_muted() != input_muted_;
    const bool synced = pulse_synced_;

    volume_ = pulse.volume();
    muted_ = pulse.muted();
    sink_desc_ = pulse.description();
    input_available_ = pulse.input_available();
    input_volume_ = pulse.input_volume();
    input_muted_ = pulse.input_muted();
    pulse_synced_ = true;
    if (!synced)
        return; // first observation only records the state

    if (sink_changed) {
        // a device switch re-announces the input side too — not a user change
        suppress_input_ = true;
        input_suppression_.disconnect();
        input_suppression_ = Glib::signal_timeout().connect(
            [this] {
                suppress_input_ = false;
                return false;
            },
            kInputSuppressionMs);
    }
    if (volume_changed)
        request(Type::Volume);
    if (input_changed && pulse.input_available())
        request(Type::InputVolume);
}

void Osd::on_brightness_changed() {
    request(Type::Brightness);
}

void Osd::on_lock_key_changed(LockKeys::Key key, bool on) {
    lock_key_on_ = on;
    lock_key_text_ = std::string(LockKeys::key_name(key)) + (on ? " ON" : " OFF");
    request(Type::LockKey);
}

} // namespace hyprshell
