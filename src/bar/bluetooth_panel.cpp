#include "bar/bluetooth_panel.hpp"

#include "services/bluez.hpp"

#include <string>
#include <vector>

namespace hyprshell {

namespace {

// tabler glyphs, \u escapes so the PUA codepoints survive every tool
constexpr const char* kIconBluetooth = "\uEA37";
constexpr const char* kIconBluetoothOff = "\uECEB";
constexpr const char* kIconRefresh = "\uEB13";

// Noctalia's BluetoothUtils.deviceIcon: keyword tests over the lowercased
// device name + BlueZ icon hint, display hints on the icon checked first so
// "audio" doesn't catch TVs.
const char* device_glyph(const Bluez::Device& dev) {
    auto lower = [](std::string s) {
        for (auto& c : s)
            c = static_cast<char>(g_ascii_tolower(c));
        return s;
    };
    const std::string name = lower(dev.name);
    const std::string icon = lower(dev.icon);
    auto hit = [&](const std::vector<const char*>& keys, bool icon_only = false) {
        for (const char* key : keys)
            if (icon.find(key) != std::string::npos ||
                (!icon_only && name.find(key) != std::string::npos))
                return true;
        return false;
    };
    constexpr const char* kTv = "\uEA8D"; // device-tv
    if (hit({"display", "tv", "monitor", "projector", "screen", "chromecast", "cast"},
            /*icon_only=*/true))
        return kTv;
    if (hit({"controller", "gamepad"}))
        return "\uF1D2"; // device-gamepad-2
    if (hit({"microphone"}))
        return "\uEAF0"; // microphone
    if (hit({"pod", "bud", "minor"}))
        return "\uF5A9"; // device-airpods
    if (hit({"headset", "arctis", "major"}))
        return "\uEB90"; // headset
    if (hit({"headphone"}))
        return "\uEABD"; // headphones
    if (hit({"mouse"}))
        return "\uF1D7"; // mouse-2
    if (hit({"keyboard"}))
        return kIconBluetooth;
    if (hit({"watch"}))
        return "\uEBF9"; // device-watch
    if (hit({"display", "tv", "monitor", "projector", "screen", "chromecast", "cast"}))
        return kTv;
    if (hit({"speaker", "audio", "sound"}))
        return "\uEA8B"; // device-speaker
    if (hit({"phone", "iphone", "android", "samsung"}))
        return "\uEA8A"; // device-mobile
    return kIconBluetooth;
}

} // namespace

BluetoothPanel::BluetoothPanel() : Gtk::Box(Gtk::Orientation::VERTICAL, 9) {
    add_css_class("bluetooth-panel");
    // Fixed size (popover-resize gotcha): a mapped popover surface never
    // resizes on Hyprland, and this panel's content changes while open.
    set_size_request(330, 400);

    // -- header: [bt] Bluetooth ......... [auto-connect] [switch] -------------
    auto* header = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 9);
    header_icon_.set_text(kIconBluetooth);
    header_icon_.add_css_class("bp-icon");
    header->append(header_icon_);
    auto* title = Gtk::make_managed<Gtk::Label>("Bluetooth");
    title->add_css_class("np-title");
    title->set_halign(Gtk::Align::START);
    title->set_hexpand(true);
    header->append(*title);
    // refresh: the glyph swaps for a spinner while discovery runs
    refresh_icon_.set_text(kIconRefresh);
    refresh_icon_.add_css_class("np-refresh-icon");
    refresh_spinner_.add_css_class("np-spinner");
    refresh_stack_.add(refresh_icon_, "icon");
    refresh_stack_.add(refresh_spinner_, "spinner");
    refresh_btn_.set_child(refresh_stack_);
    refresh_btn_.add_css_class("np-refresh");
    refresh_btn_.set_valign(Gtk::Align::CENTER);
    refresh_btn_.set_tooltip_text("Search for devices again");
    refresh_btn_.signal_clicked().connect([this] {
        status_.set_text("");
        status_.set_visible(false);
        Bluez::get().refresh_devices();
    });
    header->append(refresh_btn_);
    power_switch_.set_valign(Gtk::Align::CENTER);
    power_switch_.property_active().signal_changed().connect([this] {
        if (updating_)
            return;
        Bluez::get().set_enabled(power_switch_.get_active());
    });
    header->append(power_switch_);
    append(*header);

    // -- disabled state (Noctalia look), replaces the lists while powered off --
    disabled_card_.add_css_class("np-disabled");
    disabled_card_.set_vexpand(true);
    auto* disabled_inner = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
    disabled_inner->set_valign(Gtk::Align::CENTER);
    disabled_inner->set_vexpand(true);
    auto* disabled_icon = Gtk::make_managed<Gtk::Label>(kIconBluetoothOff);
    disabled_icon->add_css_class("np-disabled-icon");
    disabled_inner->append(*disabled_icon);
    auto* disabled_title = Gtk::make_managed<Gtk::Label>("Bluetooth is disabled");
    disabled_title->add_css_class("np-disabled-title");
    disabled_inner->append(*disabled_title);
    auto* disabled_sub =
        Gtk::make_managed<Gtk::Label>("Enable Bluetooth to see your devices.");
    disabled_sub->add_css_class("bp-value");
    disabled_inner->append(*disabled_sub);
    disabled_card_.append(*disabled_inner);
    disabled_card_.set_visible(false);
    append(disabled_card_);

    // -- device lists -----------------------------------------------------------
    scroller_.set_child(list_);
    scroller_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    scroller_.set_vexpand(true); // fills the panel's fixed height
    append(scroller_);

    // pair/connect errors, pinned under the list so they survive scrolling
    status_.add_css_class("bp-value");
    status_.set_halign(Gtk::Align::START);
    status_.set_wrap(true);
    status_.set_max_width_chars(40);
    status_.set_visible(false);
    append(status_);

    auto& bt = Bluez::get();
    bt.signal_changed().connect(sigc::mem_fun(*this, &BluetoothPanel::rebuild));
    bt.signal_action_done().connect([this](bool ok, const std::string& message) {
        status_.set_text(ok ? "" : message);
        status_.set_visible(!ok);
    });
    rebuild();
}

void BluetoothPanel::set_open(bool open) {
    open_ = open;
    auto& bt = Bluez::get();
    if (open) {
        status_.set_text("");
        status_.set_visible(false);
        rebuild();
        if (bt.enabled())
            bt.set_scanning(true);
    } else {
        bt.set_scanning(false);
    }
}

void BluetoothPanel::rebuild() {
    auto& bt = Bluez::get();

    updating_ = true;
    power_switch_.set_active(bt.enabled());
    power_switch_.set_sensitive(bt.available());
    updating_ = false;
    header_icon_.set_text(bt.enabled() ? kIconBluetooth : kIconBluetoothOff);

    while (auto* child = list_.get_first_child())
        list_.remove(*child);

    disabled_card_.set_visible(!bt.enabled());
    scroller_.set_visible(bt.enabled());
    if (!bt.enabled()) {
        update_busy();
        return;
    }

    // pairing pauses discovery (see the service) — don't tug it back on
    bool any_busy = false;
    std::vector<const Bluez::Device*> connected;
    std::vector<const Bluez::Device*> paired;
    std::vector<const Bluez::Device*> available;
    for (const auto& dev : bt.devices()) {
        any_busy = any_busy || dev.busy;
        if (dev.blocked)
            continue;
        if (dev.connected)
            connected.push_back(&dev);
        else if (dev.paired || dev.trusted)
            paired.push_back(&dev);
        else if (dev.named) // hide MAC-only names, like Noctalia's filter
            available.push_back(&dev);
    }
    if (open_ && !bt.scanning() && !any_busy)
        bt.set_scanning(true); // e.g. the switch was just turned on
    update_busy();             // after the possible restart above

    auto add_section = [this](const char* text) {
        auto* label = Gtk::make_managed<Gtk::Label>(text);
        label->add_css_class("np-section");
        label->set_halign(Gtk::Align::START);
        list_.append(*label);
    };

    if (!connected.empty()) {
        add_section("Connected");
        for (const auto* dev : connected) {
            auto* card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 9);
            card->add_css_class("np-connected");
            auto* icon = Gtk::make_managed<Gtk::Label>(device_glyph(*dev));
            icon->add_css_class("np-connected-icon");
            card->append(*icon);
            auto* text = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
            text->set_hexpand(true);
            text->set_valign(Gtk::Align::CENTER);
            auto* name = Gtk::make_managed<Gtk::Label>(dev->name);
            name->add_css_class("np-connected-ssid");
            name->set_halign(Gtk::Align::START);
            name->set_ellipsize(Pango::EllipsizeMode::END);
            name->set_max_width_chars(20);
            text->append(*name);
            std::string sub = dev->busy ? "Disconnecting…" : "Connected";
            if (!dev->busy && dev->battery >= 0)
                sub += " · " + std::to_string(dev->battery) + "%";
            auto* sub_label = Gtk::make_managed<Gtk::Label>(sub);
            sub_label->add_css_class("np-connected-sub");
            sub_label->set_halign(Gtk::Align::START);
            text->append(*sub_label);
            card->append(*text);
            auto* disconnect = Gtk::make_managed<Gtk::Button>("Disconnect");
            disconnect->add_css_class("np-disconnect");
            disconnect->set_valign(Gtk::Align::CENTER);
            disconnect->set_sensitive(!dev->busy);
            const std::string path = dev->path;
            disconnect->signal_clicked().connect(
                [path] { Bluez::get().disconnect_device(path); });
            card->append(*disconnect);
            list_.append(*card);
        }
    }

    // paired and available rows share one look; only sub text + button differ
    auto add_row = [this](const Bluez::Device& dev, const std::string& sub,
                          const char* action, void (Bluez::*method)(const std::string&)) {
        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 9);
        row->add_css_class("np-net-row");
        auto* icon = Gtk::make_managed<Gtk::Label>(device_glyph(dev));
        icon->add_css_class("bp-icon");
        row->append(*icon);
        auto* text = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
        text->set_hexpand(true);
        text->set_valign(Gtk::Align::CENTER);
        auto* name = Gtk::make_managed<Gtk::Label>(dev.name);
        name->add_css_class("np-ssid");
        name->set_halign(Gtk::Align::START);
        name->set_ellipsize(Pango::EllipsizeMode::END);
        name->set_max_width_chars(20);
        text->append(*name);
        if (!sub.empty()) {
            auto* sub_label = Gtk::make_managed<Gtk::Label>(sub);
            sub_label->add_css_class("bt-sub");
            sub_label->set_halign(Gtk::Align::START);
            text->append(*sub_label);
        }
        row->append(*text);
        auto* button = Gtk::make_managed<Gtk::Button>(action);
        button->add_css_class("np-connect");
        button->set_valign(Gtk::Align::CENTER);
        button->set_sensitive(!dev.busy);
        const std::string path = dev.path;
        button->signal_clicked().connect([path, method] { (Bluez::get().*method)(path); });
        row->append(*button);
        list_.append(*row);
    };

    if (!paired.empty()) {
        add_section("Paired devices");
        for (const auto* dev : paired) {
            std::string sub;
            if (dev->busy)
                sub = "Connecting…";
            else if (dev->battery >= 0)
                sub = "Battery " + std::to_string(dev->battery) + "%";
            add_row(*dev, sub, "Connect", &Bluez::connect_device);
        }
    }

    add_section("Available devices"); // the header spinner says "scanning"
    if (available.empty()) {
        if (!bt.scanning()) {
            auto* none = Gtk::make_managed<Gtk::Label>("No devices found");
            none->add_css_class("bp-value");
            none->set_halign(Gtk::Align::START);
            list_.append(*none);
        }
    } else {
        for (const auto* dev : available)
            add_row(*dev, dev->busy ? "Pairing…" : "", "Pair", &Bluez::pair_device);
    }
}

void BluetoothPanel::update_busy() {
    auto& bt = Bluez::get();
    const bool scanning = bt.enabled() && bt.scanning();
    refresh_stack_.set_visible_child(scanning ? "spinner" : "icon");
    refresh_spinner_.set_running(scanning);
    refresh_btn_.set_sensitive(bt.enabled());
}

} // namespace hyprshell
