#include "bar/network_panel.hpp"

namespace hyprshell {

namespace {

// tabler glyphs, \u escapes so the PUA codepoints survive every tool
constexpr const char* kIconWifi = "\uEB52";
constexpr const char* kIconWifiOff = "\uECFA";
constexpr const char* kIconLock = "\uEAE2";
// signal buckets: >=80, >=60, >=35, >=15, below (Noctalia thresholds)
constexpr const char* kSignalLevels[] = {"\uEB52", "\uEBFC", "\uEBA5", "\uEBA4", "\uEBA3"};

const char* signal_glyph(int signal) {
    return kSignalLevels[signal >= 80 ? 0 : signal >= 60 ? 1 : signal >= 35 ? 2
                         : signal >= 15 ? 3 : 4];
}

} // namespace

NetworkPanel::NetworkPanel() : Gtk::Box(Gtk::Orientation::VERTICAL, 9) {
    add_css_class("network-panel");
    // Fixed size, like Noctalia's panel: the popover surface never resizes
    // once mapped on Hyprland, so swapping content (list <-> disabled card,
    // connected card appearing) must not change the panel's height.
    set_size_request(330, 440);

    // -- header: [wifi] Wi-Fi ............ [switch] [x] -----------------------
    auto* header = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 9);
    auto* header_icon = Gtk::make_managed<Gtk::Label>(kIconWifi);
    header_icon->add_css_class("bp-icon");
    header->append(*header_icon);
    auto* title = Gtk::make_managed<Gtk::Label>("Wi-Fi");
    title->add_css_class("np-title");
    title->set_halign(Gtk::Align::START);
    title->set_hexpand(true);
    header->append(*title);
    wifi_switch_.set_valign(Gtk::Align::CENTER);
    wifi_switch_.property_active().signal_changed().connect([this] {
        if (updating_)
            return;
        NetworkManager::get().set_wifi_enabled(wifi_switch_.get_active());
        if (wifi_switch_.get_active())
            // give the radio a moment to come up before the first scan
            Glib::signal_timeout().connect_seconds_once(
                [] { NetworkManager::get().scan(); }, 1);
    });
    header->append(wifi_switch_);
    append(*header);

    // -- connected card --------------------------------------------------------
    auto* connected_title = Gtk::make_managed<Gtk::Label>("Connected");
    connected_title->add_css_class("np-section");
    connected_title->set_halign(Gtk::Align::START);
    connected_section_.append(*connected_title);

    connected_card_.add_css_class("np-connected");
    connected_icon_.add_css_class("np-connected-icon");
    connected_card_.append(connected_icon_);
    auto* connected_text = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    connected_text->set_hexpand(true);
    connected_text->set_valign(Gtk::Align::CENTER);
    connected_ssid_.add_css_class("np-connected-ssid");
    connected_ssid_.set_halign(Gtk::Align::START);
    connected_ssid_.set_ellipsize(Pango::EllipsizeMode::END);
    connected_text->append(connected_ssid_);
    auto* connected_sub = Gtk::make_managed<Gtk::Label>("Connected");
    connected_sub->add_css_class("np-connected-sub");
    connected_sub->set_halign(Gtk::Align::START);
    connected_text->append(*connected_sub);
    connected_card_.append(*connected_text);
    disconnect_.set_label("Disconnect");
    disconnect_.add_css_class("np-disconnect");
    disconnect_.set_valign(Gtk::Align::CENTER);
    disconnect_.signal_clicked().connect([this] {
        NetworkManager::get().wifi_disconnect(connected_ssid_.get_text());
    });
    connected_card_.append(disconnect_);
    connected_section_.append(connected_card_);
    append(connected_section_);

    // -- password prompt (hidden until a secured new network is chosen) --------
    password_card_.add_css_class("bp-card");
    password_title_.add_css_class("bp-title");
    password_title_.set_halign(Gtk::Align::START);
    password_card_.append(password_title_);
    password_entry_.set_visibility(false);
    password_entry_.set_placeholder_text("Password");
    password_entry_.set_input_purpose(Gtk::InputPurpose::PASSWORD);
    password_entry_.signal_activate().connect(
        sigc::mem_fun(*this, &NetworkPanel::submit_password));
    password_card_.append(password_entry_);
    auto* pw_buttons = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    pw_buttons->set_homogeneous(true);
    auto* pw_cancel = Gtk::make_managed<Gtk::Button>("Cancel");
    pw_cancel->add_css_class("bp-rate-btn");
    pw_cancel->signal_clicked().connect([this] { password_card_.set_visible(false); });
    pw_buttons->append(*pw_cancel);
    auto* pw_connect = Gtk::make_managed<Gtk::Button>("Connect");
    pw_connect->add_css_class("bp-rate-btn");
    pw_connect->add_css_class("active");
    pw_connect->signal_clicked().connect(
        sigc::mem_fun(*this, &NetworkPanel::submit_password));
    pw_buttons->append(*pw_connect);
    password_card_.append(*pw_buttons);
    password_card_.set_visible(false);
    append(password_card_);

    // -- disabled state (Noctalia look), replaces the list while radio is off --
    disabled_card_.add_css_class("np-disabled");
    disabled_card_.set_vexpand(true);
    auto* disabled_inner = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
    disabled_inner->set_valign(Gtk::Align::CENTER);
    disabled_inner->set_vexpand(true);
    auto* disabled_icon = Gtk::make_managed<Gtk::Label>(kIconWifiOff);
    disabled_icon->add_css_class("np-disabled-icon");
    disabled_inner->append(*disabled_icon);
    auto* disabled_title = Gtk::make_managed<Gtk::Label>("Wi-Fi is disabled");
    disabled_title->add_css_class("np-disabled-title");
    disabled_inner->append(*disabled_title);
    auto* disabled_sub =
        Gtk::make_managed<Gtk::Label>("Enable Wi-Fi to see available networks.");
    disabled_sub->add_css_class("bp-value");
    disabled_inner->append(*disabled_sub);
    disabled_card_.append(*disabled_inner);
    disabled_card_.set_visible(false);
    append(disabled_card_);

    // -- available networks -----------------------------------------------------
    auto* available_header = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    auto* available_title = Gtk::make_managed<Gtk::Label>("Available networks");
    available_title->add_css_class("np-section");
    available_title->set_halign(Gtk::Align::START);
    available_title->set_hexpand(true);
    available_header->append(*available_title);
    scan_status_.add_css_class("bp-value");
    scan_status_.set_halign(Gtk::Align::END);
    available_header->append(scan_status_);
    available_section_.append(*available_header);
    status_.add_css_class("bp-value");
    status_.set_halign(Gtk::Align::START);
    status_.set_wrap(true);
    status_.set_max_width_chars(40);
    status_.set_visible(false);
    available_section_.append(status_);
    scroller_.set_child(list_);
    scroller_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    scroller_.set_vexpand(true); // fills the panel's fixed height
    available_section_.append(scroller_);
    available_section_.set_vexpand(true);
    append(available_section_);

    auto& nm = NetworkManager::get();
    nm.signal_changed().connect(sigc::mem_fun(*this, &NetworkPanel::update_state));
    nm.signal_networks_changed().connect(
        sigc::mem_fun(*this, &NetworkPanel::rebuild_networks));
    nm.signal_action_done().connect([this](bool ok, const std::string& out) {
        scan_status_.set_text("");
        status_.set_text(ok ? "" : out);
        status_.set_visible(!ok);
    });

    update_state();
    rebuild_networks();
}

void NetworkPanel::refresh() {
    password_card_.set_visible(false);
    status_.set_text("");
    status_.set_visible(false);
    update_state();
    if (NetworkManager::get().wifi_enabled())
        NetworkManager::get().scan();
}

void NetworkPanel::update_state() {
    auto& nm = NetworkManager::get();
    updating_ = true;
    wifi_switch_.set_active(nm.wifi_enabled());
    updating_ = false;
    if (nm.wifi_enabled() != last_enabled_) {
        last_enabled_ = nm.wifi_enabled();
        rebuild_networks(); // swap between the list and the disabled card
    }
    update_connected();
}

// The scan list refreshes slowly (a rescan takes seconds); NM's primary
// connection over DBus is instant, so it wins for the connected card.
void NetworkPanel::update_connected() {
    auto& nm = NetworkManager::get();
    std::string ssid;
    int signal = 0;
    if (nm.kind() == NetworkManager::Kind::wifi && !nm.ssid().empty()) {
        ssid = nm.ssid();
        signal = nm.strength();
    } else {
        for (const auto& net : nm.networks())
            if (net.in_use) {
                ssid = net.ssid;
                signal = net.signal;
            }
    }
    connected_section_.set_visible(!ssid.empty() && nm.wifi_enabled());
    if (!ssid.empty()) {
        connected_icon_.set_text(signal_glyph(signal));
        connected_ssid_.set_text(ssid);
    }
}

void NetworkPanel::rebuild_networks() {
    auto& nm = NetworkManager::get();

    while (auto* child = list_.get_first_child())
        list_.remove(*child);

    update_connected();
    const Glib::ustring connected_ssid =
        connected_section_.get_visible() ? connected_ssid_.get_text() : "";

    // radio off: swap the whole list for the Noctalia-style disabled card
    const bool enabled = nm.wifi_enabled();
    disabled_card_.set_visible(!enabled);
    available_section_.set_visible(enabled);
    if (!enabled) {
        scan_status_.set_text("");
        connected_section_.set_visible(false);
        return;
    }
    status_.set_visible(false);
    scan_status_.set_text(nm.scanning() ? "Scanning…" : "");

    for (const auto& net : nm.networks()) {
        if (net.in_use || net.ssid == connected_ssid.raw())
            continue;
        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 9);
        row->add_css_class("np-net-row");
        auto* icon = Gtk::make_managed<Gtk::Label>(signal_glyph(net.signal));
        icon->add_css_class("bp-icon");
        row->append(*icon);
        auto* text = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
        text->set_hexpand(true);
        text->set_valign(Gtk::Align::CENTER);
        auto* ssid = Gtk::make_managed<Gtk::Label>(net.ssid);
        ssid->add_css_class("np-ssid");
        ssid->set_halign(Gtk::Align::START);
        ssid->set_ellipsize(Pango::EllipsizeMode::END);
        ssid->set_max_width_chars(22);
        text->append(*ssid);
        if (!net.security.empty()) {
            auto* sec = Gtk::make_managed<Gtk::Label>(kIconLock + (" " + net.security));
            sec->add_css_class("np-security");
            sec->set_halign(Gtk::Align::START);
            text->append(*sec);
        }
        row->append(*text);
        auto* connect = Gtk::make_managed<Gtk::Button>("Connect");
        connect->add_css_class("np-connect");
        connect->set_valign(Gtk::Align::CENTER);
        const std::string net_ssid = net.ssid;
        const bool needs_password = !net.security.empty() && !net.saved;
        connect->signal_clicked().connect([this, net_ssid, needs_password] {
            if (needs_password) {
                ask_password(net_ssid);
            } else {
                scan_status_.set_text("Connecting…");
                NetworkManager::get().wifi_connect(net_ssid);
            }
        });
        row->append(*connect);
        list_.append(*row);
    }
}

void NetworkPanel::ask_password(const std::string& ssid) {
    password_ssid_ = ssid;
    password_title_.set_text("Password for " + ssid);
    password_entry_.set_text("");
    password_card_.set_visible(true);
    password_entry_.grab_focus();
}

void NetworkPanel::submit_password() {
    if (password_ssid_.empty())
        return;
    password_card_.set_visible(false);
    scan_status_.set_text("Connecting…");
    NetworkManager::get().wifi_connect(password_ssid_, password_entry_.get_text());
    password_entry_.set_text("");
}

} // namespace hyprshell
