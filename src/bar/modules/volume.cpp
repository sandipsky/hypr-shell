#include "bar/modules/volume.hpp"

#include "services/pulse.hpp"

#include <cmath>

namespace hyprshell {

namespace {

// noctalia-tabler-icons glyphs; thresholds match Noctalia's AudioService.
constexpr const char* kMuted = "";  // volume-off U+F1C3
constexpr const char* kZero = "";   // volume-3   U+EB50 (speaker, no waves)
constexpr const char* kLow = "";    // volume-2   U+EB4F (one wave)
constexpr const char* kHigh = "";   // volume     U+EB51 (two waves)

} // namespace

Volume::Volume() : Gtk::Box(Gtk::Orientation::HORIZONTAL, 0) {
    add_css_class("module");
    add_css_class("volume");
    icon_.add_css_class("icon");
    append(icon_);

    Pulse::get().signal_changed().connect(sigc::mem_fun(*this, &Volume::update));
    update();
}

void Volume::update() {
    auto& pulse = Pulse::get();
    set_visible(pulse.available());
    if (!pulse.available()) {
        return;
    }
    if (pulse.muted()) {
        icon_.set_text(kMuted);
    } else if (pulse.volume() < 0.005) {
        icon_.set_text(kZero);
    } else if (pulse.volume() <= 0.5) {
        icon_.set_text(kLow);
    } else {
        icon_.set_text(kHigh);
    }
    auto pct = static_cast<int>(std::lround(pulse.volume() * 100.0));
    set_tooltip_text("Volume: " + std::to_string(pct) + "%");
}

} // namespace hyprshell
