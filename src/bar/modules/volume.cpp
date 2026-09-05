#include "bar/modules/volume.hpp"

#include "bar/bar_popover.hpp"

#include "services/config.hpp"
#include "services/osd.hpp"
#include "services/pulse.hpp"

#include <cmath>

#include <algorithm>
#include <cstdlib>

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

    // anchored to the icon label, never the module Box (see battery.cpp)
    panel_ = Gtk::make_managed<AudioPanel>();
    popover_.set_child(*panel_);
    popover_.set_parent(icon_);
    popover_.set_has_arrow(false);
    popover_.add_css_class("audio-popover");
    // the panel shows the levels itself — no OSD while it is open (Noctalia)
    popover_.signal_map().connect([] { Osd::get().set_audio_panel_open(true); });
    popover_.signal_unmap().connect([] { Osd::get().set_audio_panel_open(false); });

    auto click = Gtk::GestureClick::create();
    click->signal_released().connect([this](int, double, double) {
        // keep the panel on the free side of the bar
        place_bar_popover(popover_);
        panel_->refresh();
        popover_.popup();
    });
    add_controller(click);

    // right click toggles output mute
    auto right_click = Gtk::GestureClick::create();
    right_click->set_button(GDK_BUTTON_SECONDARY);
    right_click->signal_released().connect(
        [](int, double, double) { Pulse::get().set_muted(!Pulse::get().muted()); });
    add_controller(right_click);

    // dev hook: HS_OPEN_AUDIO=1 pops the panel shortly after startup
    if (const char* hook = g_getenv("HS_OPEN_AUDIO")) {
        const int delay = std::max(800, std::atoi(hook)); // >1 = delay in ms
        Glib::signal_timeout().connect_once(
            [this] {
                panel_->refresh();
                place_bar_popover(popover_);
                popover_.popup();
            },
            delay);
    }

    Pulse::get().signal_changed().connect(sigc::mem_fun(*this, &Volume::update));
    update();
}

Volume::~Volume() {
    popover_.unparent();
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
