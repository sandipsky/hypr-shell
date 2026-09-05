#pragma once

#include <giomm.h>
#include <sigc++/sigc++.h>

#include <set>
#include <string>
#include <vector>

namespace hyprshell {

// Bluetooth adapter + device state from BlueZ's org.bluez ObjectManager
// (system DBus, no extra library). Discovery runs while the panel is open;
// pairing shells out to bluetoothctl (it registers its own BlueZ agent
// internally — the same trick Noctalia's pair script uses), then trusts and
// connects over DBus. Auto-connect (bar.bluetooth.auto_connect) reconnects
// every paired device when the adapter powers on.
class Bluez {
public:
    struct Device {
        std::string path; // DBus object path — the stable key for actions
        std::string name;
        std::string address;
        std::string icon; // BlueZ icon hint ("audio-headset", ...)
        bool named = false; // a real name, not just the MAC (Noctalia's filter)
        bool connected = false;
        bool paired = false;
        bool trusted = false;
        bool blocked = false;
        bool busy = false; // a pair/connect/disconnect is in flight
        int battery = -1;  // 0..100 via org.bluez.Battery1, -1 = unavailable
    };

    static Bluez& get();

    Bluez(const Bluez&) = delete;
    Bluez& operator=(const Bluez&) = delete;

    bool available() const { return available_; } // bluetoothd up + adapter found
    bool enabled() const { return enabled_; }     // adapter Powered
    bool scanning() const { return scanning_; }   // adapter Discovering
    const std::vector<Device>& devices() const { return devices_; }
    bool any_connected() const;

    void set_enabled(bool enabled);   // rfkill unblock + Adapter1.Powered
    void set_scanning(bool active);   // Adapter1.Start/StopDiscovery
    // Panel refresh button: forget cached unpaired devices (they come back as
    // discovery finds them again) and restart discovery if it is wanted.
    void refresh_devices();
    void pair_device(const std::string& path); // bluetoothctl pair, then connect
    void connect_device(const std::string& path); // trusts first, like Noctalia
    void disconnect_device(const std::string& path);

    sigc::signal<void()>& signal_changed() { return changed_; }
    // (ok, error text) after a pair/connect/disconnect finishes
    sigc::signal<void(bool, const std::string&)>& signal_action_done() {
        return action_done_;
    }

private:
    Bluez();

    void rebuild(); // re-derive all state from the object tree, then emit
    Glib::RefPtr<Gio::DBus::Proxy> device_proxy(const std::string& path);
    void finish_action(const std::string& path, bool ok, const std::string& message);
    void schedule_auto_connect(); // 1.5s after power-on, like Noctalia
    void attempt_auto_connect();

    Glib::RefPtr<Gio::DBus::ObjectManagerClient> manager_;
    std::string adapter_path_;
    bool available_ = false;
    bool enabled_ = false;
    bool scanning_ = false;
    // Optimistic power target: BlueZ emits several PropertiesChanged (Class,
    // PowerState…) BEFORE Powered flips, and each rebuild re-reads the stale
    // Powered — without this the panel switch bounces mid-toggle. -1 = none.
    int pending_power_ = -1;
    sigc::connection pending_power_timer_;
    bool prev_enabled_ = false;  // power-on edge triggers auto-connect
    bool last_auto_connect_ = false;
    bool resume_scan_ = false;   // discovery paused during pairing
    bool want_scanning_ = false; // last set_scanning() request (panel open)
    std::set<std::string> busy_; // device paths with an in-flight call
    std::vector<Device> devices_;
    sigc::connection auto_connect_timer_;
    sigc::signal<void()> changed_;
    sigc::signal<void(bool, const std::string&)> action_done_;
};

} // namespace hyprshell
