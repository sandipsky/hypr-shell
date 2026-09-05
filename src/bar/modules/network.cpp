#include "bar/modules/network.hpp"

#include "bar/bar_popover.hpp"

#include "services/config.hpp"
#include "services/network_manager.hpp"

#include <algorithm>
#include <cstdlib>

namespace hyprshell {

namespace {

// noctalia-tabler-icons glyphs
constexpr const char* kWifiOff = "";         // wifi-off      U+ECFA
constexpr const char* kWifiExclamation = ""; // wifi-... U+EBFD (limited connectivity)
constexpr const char* kWifiQuestion = "";    // wifi-question U+EBFE (portal/unknown)
// signal buckets: >=80, >=60, >=35, >=15, below
constexpr const char* kWifiLevels[] = {"", "", "", "", ""};
constexpr const char* kEthernet = "";            // ethernet     U+ECCC
constexpr const char* kEthernetExclamation = ""; // ethernet-exclamation U+ECCE
constexpr const char* kEthernetQuestion = "";    // ethernet-question    U+ECCF

const char* wifi_icon(const NetworkManager& nm) {
    using Connectivity = NetworkManager::Connectivity;
    switch (nm.connectivity()) {
    case Connectivity::limited:
        return kWifiExclamation;
    case Connectivity::portal:
    case Connectivity::unknown:
        return kWifiQuestion;
    default:
        break;
    }
    auto s = nm.strength();
    return kWifiLevels[s >= 80 ? 0 : s >= 60 ? 1 : s >= 35 ? 2 : s >= 15 ? 3 : 4];
}

const char* ethernet_icon(const NetworkManager& nm) {
    using Connectivity = NetworkManager::Connectivity;
    switch (nm.connectivity()) {
    case Connectivity::limited:
        return kEthernetExclamation;
    case Connectivity::portal:
    case Connectivity::unknown:
        return kEthernetQuestion;
    default:
        return kEthernet;
    }
}

} // namespace

Network::Network() : Gtk::Box(Gtk::Orientation::HORIZONTAL, 0) {
    add_css_class("module");
    add_css_class("network");
    icon_.add_css_class("icon");
    append(icon_);

    // click opens the Wi-Fi selector; anchored to the icon label because a
    // Gtk::Box parent allocates an open popover inline (see battery module)
    panel_ = Gtk::make_managed<NetworkPanel>();
    popover_.set_child(*panel_);
    popover_.set_parent(icon_);
    popover_.set_has_arrow(false);
    popover_.add_css_class("network-popover");
    set_cursor(Gdk::Cursor::create("pointer"));

    auto click = Gtk::GestureClick::create();
    click->signal_released().connect([this](int, double, double) {
        // keep the panel on the free side of the bar
        place_bar_popover(popover_);
        panel_->refresh();
        popover_.popup();
    });
    add_controller(click);

    // dev hook: HS_OPEN_NETWORK=1 pops the panel 800ms after startup; a value above 1
    // is the delay in ms (the main loop can stall briefly during startup and a
    // popup issued meanwhile is dismissed at once)
    if (const char* hook = g_getenv("HS_OPEN_NETWORK")) {
        const int delay = std::max(800, std::atoi(hook));
        Glib::signal_timeout().connect_once(
            [this] {
                panel_->refresh();
                place_bar_popover(popover_);
                popover_.popup();
            },
            delay);
    }

    NetworkManager::get().signal_changed().connect(sigc::mem_fun(*this, &Network::update));
    update();
}

Network::~Network() {
    popover_.unparent();
}

void Network::update() {
    auto& nm = NetworkManager::get();
    set_visible(nm.available());
    if (!nm.available()) {
        return;
    }
    // tooltip mirrors Noctalia's getStatusText(true): "<name> - <rate>"
    std::string tooltip;
    if (nm.ethernet_connected()) {
        // Noctalia: an active ethernet connection wins the icon even while
        // wifi is connected too
        icon_.set_text(ethernet_icon(nm));
        tooltip = nm.kind() == NetworkManager::Kind::ethernet ? nm.connection_id()
                                                              : "Ethernet";
    } else {
        switch (nm.kind()) {
        case NetworkManager::Kind::wifi: {
            icon_.set_text(wifi_icon(nm));
            tooltip = nm.ssid().empty() ? nm.connection_id() : nm.ssid();
            if (nm.max_bitrate() > 0) {
                tooltip += " - " + std::to_string(nm.max_bitrate() / 1000) + " Mbit/s";
            }
            break;
        }
        case NetworkManager::Kind::ethernet:
            icon_.set_text(ethernet_icon(nm));
            tooltip = nm.connection_id();
            break;
        case NetworkManager::Kind::none:
            icon_.set_text(nm.wifi_enabled() ? kWifiLevels[4] : kWifiOff);
            break;
        }
    }
    if (tooltip.empty()) {
        set_has_tooltip(false);
    } else {
        set_tooltip_text(tooltip);
    }
}

} // namespace hyprshell
