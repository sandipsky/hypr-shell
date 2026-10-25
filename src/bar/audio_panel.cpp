#include "bar/audio_panel.hpp"

#include "services/pulse.hpp"

#include <cmath>

namespace hyprshell {

namespace {

// tabler glyphs, \u escapes so the PUA codepoints survive every tool
constexpr const char* kIconVolume = "\uEB51";     // volume (two waves)
constexpr const char* kIconVolumeOff = "\uF1C3";  // volume-off
constexpr const char* kIconMic = "\uEAF0";        // microphone
constexpr const char* kIconMicOff = "\uED16";     // microphone-off

} // namespace

AudioPanel::AudioPanel() : Gtk::Box(Gtk::Orientation::VERTICAL, 9) {
    add_css_class("audio-panel");
    set_size_request(330, -1);

    build_row(output_, "Output");
    build_row(input_, "Input");

    output_.scale.signal_value_changed().connect([this] {
        if (!updating_)
            Pulse::get().set_volume(output_.scale.get_value() / 100.0);
    });
    input_.scale.signal_value_changed().connect([this] {
        if (!updating_)
            Pulse::get().set_input_volume(input_.scale.get_value() / 100.0);
    });
    output_.mute.signal_clicked().connect(
        [] { Pulse::get().set_muted(!Pulse::get().muted()); });
    input_.mute.signal_clicked().connect(
        [] { Pulse::get().set_input_muted(!Pulse::get().input_muted()); });

    Pulse::get().signal_changed().connect(sigc::mem_fun(*this, &AudioPanel::update));
    update();
}

void AudioPanel::build_row(Row& row, const char* kind) {
    row.card.add_css_class("bp-card");

    auto* header = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    auto* kind_label = Gtk::make_managed<Gtk::Label>(kind);
    kind_label->add_css_class("ap-kind");
    header->append(*kind_label);
    row.device.add_css_class("bp-value");
    row.device.set_ellipsize(Pango::EllipsizeMode::END);
    row.device.set_halign(Gtk::Align::START);
    row.device.set_hexpand(true);
    header->append(row.device);
    row.card.append(*header);

    auto* controls = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 9);
    row.scale.set_range(0, 100);
    row.scale.set_increments(1, 5);
    row.scale.set_draw_value(false);
    row.scale.set_hexpand(true);
    row.scale.add_css_class("bp-slider");
    controls->append(row.scale);
    row.percent.add_css_class("bp-percent");
    row.percent.set_width_chars(4);
    row.percent.set_xalign(1.0);
    controls->append(row.percent);
    row.mute.add_css_class("ap-mute-btn");
    row.mute.set_valign(Gtk::Align::CENTER);
    row.mute_icon.add_css_class("bp-icon");
    row.mute.set_child(row.mute_icon);
    controls->append(row.mute);
    row.card.append(*controls);

    append(row.card);
}

void AudioPanel::refresh() {
    update();
}

void AudioPanel::update() {
    auto& pulse = Pulse::get();

    output_.card.set_visible(pulse.available());
    if (pulse.available()) {
        output_.device.set_text(pulse.description().empty()
                                    ? ""
                                    : "- " + pulse.description());
        const int pct = (int)std::lround(pulse.volume() * 100.0);
        updating_ = true;
        output_.scale.set_value(pct);
        updating_ = false;
        output_.percent.set_text(Glib::ustring::compose("%1%%", pct));
        output_.mute_icon.set_text(pulse.muted() ? kIconVolumeOff : kIconVolume);
        output_.scale.set_opacity(pulse.muted() ? 0.5 : 1.0);
    }

    input_.card.set_visible(pulse.input_available());
    if (pulse.input_available()) {
        input_.device.set_text(pulse.input_description().empty()
                                   ? ""
                                   : "- " + pulse.input_description());
        const int pct = (int)std::lround(pulse.input_volume() * 100.0);
        updating_ = true;
        input_.scale.set_value(pct);
        updating_ = false;
        input_.percent.set_text(Glib::ustring::compose("%1%%", pct));
        input_.mute_icon.set_text(pulse.input_muted() ? kIconMicOff : kIconMic);
        input_.scale.set_opacity(pulse.input_muted() ? 0.5 : 1.0);
    }
}

} // namespace hyprshell
