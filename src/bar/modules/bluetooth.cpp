#include "bar/modules/bluetooth.hpp"

#include "bar/bar_popover.hpp"

#include "services/bluez.hpp"
#include "services/config.hpp"

#include <algorithm>
#include <cstdlib>

namespace hyprshell {

namespace {

// noctalia-tabler-icons glyphs (\u escapes — never literal PUA)
constexpr const char* kBluetooth = "\uEA37";
constexpr const char* kBluetoothOff = "\uECEB";
constexpr const char* kBluetoothConnected = "\uECEA";

} // namespace

Bluetooth::Bluetooth() : Gtk::Box(Gtk::Orientation::HORIZONTAL, 0) {
    add_css_class("module");
    add_css_class("bluetooth");
    icon_.add_css_class("icon");
    append(icon_);

    // click opens the bluetooth panel; anchored to the icon label because a
    // Gtk::Box parent allocates an open popover inline (see battery module)
    panel_ = Gtk::make_managed<BluetoothPanel>();
    popover_.set_child(*panel_);
    popover_.set_parent(icon_);
    popover_.set_has_arrow(false);
    popover_.add_css_class("bluetooth-popover");
    set_cursor(Gdk::Cursor::create("pointer"));

    auto click = Gtk::GestureClick::create();
    click->signal_released().connect([this](int, double, double) {
        // keep the panel on the free side of the bar
        place_bar_popover(popover_);
        panel_->set_open(true); // starts discovery while the popover shows
        popover_.popup();
    });
    add_controller(click);
    popover_.signal_closed().connect([this] { panel_->set_open(false); });

    // dev hook: HS_OPEN_BLUETOOTH=1 pops the panel 800ms after startup; a value above 1
    // is the delay in ms (the main loop can stall briefly during startup and a
    // popup issued meanwhile is dismissed at once)
    if (const char* hook = g_getenv("HS_OPEN_BLUETOOTH")) {
        const int delay = std::max(800, std::atoi(hook));
        Glib::signal_timeout().connect_once(
            [this] {
                panel_->set_open(true);
                place_bar_popover(popover_);
                popover_.popup();
            },
            delay);
    }

    Bluez::get().signal_changed().connect(sigc::mem_fun(*this, &Bluetooth::update));
    update();
}

Bluetooth::~Bluetooth() {
    popover_.unparent();
}

void Bluetooth::update() {
    auto& bt = Bluez::get();
    set_visible(bt.available());
    if (!bt.available()) {
        return;
    }
    if (!bt.enabled()) {
        icon_.set_text(kBluetoothOff);
        set_tooltip_text("Bluetooth off");
        return;
    }
    // tooltip mirrors Noctalia's pill text: first connected device (+ N more)
    std::string first;
    int connected = 0;
    for (const auto& dev : bt.devices()) {
        if (!dev.connected || dev.blocked)
            continue;
        if (connected++ == 0)
            first = dev.name;
    }
    icon_.set_text(connected > 0 ? kBluetoothConnected : kBluetooth);
    if (connected > 1)
        first += " + " + std::to_string(connected - 1);
    set_tooltip_text(connected > 0 ? first : "Bluetooth");
}

} // namespace hyprshell
