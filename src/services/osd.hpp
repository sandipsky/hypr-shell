#pragma once

#include "services/lock_keys.hpp"

#include <glibmm.h>
#include <sigc++/sigc++.h>

#include <string>

namespace hyprshell {

// On-screen-display trigger logic — the non-visual half of Noctalia's OSD.qml.
// Watches Pulse (output + input volume/mute), Brightness and LockKeys and
// emits signal_show(type) whenever the OSD window should appear or refresh.
// Rules copied from Noctalia: nothing during the first 2s after startup, no
// volume OSD while the audio panel is open, no brightness OSD while the
// panel holding the brightness slider is open, no microphone OSD for 300ms
// after the default sink changed (device switches re-announce input state).
class Osd {
public:
    // order matches Noctalia's OSD.Type enum
    enum class Type { Volume, InputVolume, Brightness, LockKey };

    static Osd& get();

    Osd(const Osd&) = delete;
    Osd& operator=(const Osd&) = delete;

    // last shown type and, for LockKey, Noctalia's status text ("CAPS ON")
    Type type() const { return type_; }
    const std::string& lock_key_text() const { return lock_key_text_; }
    bool lock_key_on() const { return lock_key_on_; }

    // panels that already visualize the value suppress the matching OSD
    void set_audio_panel_open(bool open) { audio_panel_open_ = open; }
    void set_brightness_panel_open(bool open) { brightness_panel_open_ = open; }

    // Show unconditionally (dev hook / tests); for LockKey the text is
    // synthesized from the current Caps Lock state.
    void show(Type type);

    static Type type_from_key(const std::string& key); // "volume" | "input" | "brightness" | "lock"

    sigc::signal<void(Type)>& signal_show() { return show_; }

private:
    Osd();

    void apply_config();
    void on_pulse_changed();
    void on_brightness_changed();
    void on_lock_key_changed(LockKeys::Key key, bool on);
    void request(Type type); // show() with Noctalia's gating

    Type type_ = Type::Volume;
    std::string lock_key_text_;
    bool lock_key_on_ = false;

    bool startup_complete_ = false;
    bool audio_panel_open_ = false;
    bool brightness_panel_open_ = false;
    bool suppress_input_ = false;
    sigc::connection input_suppression_;

    // last observed Pulse state, to tell what changed
    bool pulse_synced_ = false;
    double volume_ = 0.0;
    bool muted_ = false;
    std::string sink_desc_;
    bool input_available_ = false;
    double input_volume_ = 0.0;
    bool input_muted_ = false;

    sigc::signal<void(Type)> show_;
};

} // namespace hyprshell
