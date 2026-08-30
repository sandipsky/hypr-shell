#include "bar/modules/network.hpp"

#include "services/network_manager.hpp"

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

    NetworkManager::get().signal_changed().connect(sigc::mem_fun(*this, &Network::update));
    update();
}

void Network::update() {
    auto& nm = NetworkManager::get();
    set_visible(nm.available());
    if (!nm.available()) {
        return;
    }
    // tooltip mirrors Noctalia's getStatusText(true): "<name> - <rate>"
    std::string tooltip;
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
    if (tooltip.empty()) {
        set_has_tooltip(false);
    } else {
        set_tooltip_text(tooltip);
    }
}

} // namespace hyprshell
