# 06 — Services and async I/O

A *service* wraps one external data source and hands the rest of the program
a clean, synchronous-looking view of it: getters for the current state and
a signal for "something changed". This page shows the pattern and the four
kinds of backend the project already talks to.

## The skeleton

```cpp
// src/services/thing.hpp
#pragma once
#include <giomm.h>
#include <sigc++/sigc++.h>

namespace hyprshell {

class Thing {
public:
    static Thing& get();                       // singleton
    Thing(const Thing&) = delete;
    Thing& operator=(const Thing&) = delete;

    bool available() const { return available_; }   // false until connected
    int value() const { return value_; }

    void set_value(int v);                      // optional writer

    sigc::signal<void()>& signal_changed() { return changed_; }

private:
    Thing();                                    // starts the async connect
    void read_state();

    bool available_ = false;
    int value_ = 0;
    sigc::signal<void()> changed_;
};

} // namespace hyprshell
```

```cpp
// src/services/thing.cpp
Thing& Thing::get() {
    static Thing instance;   // constructed on first call, lives until exit
    return instance;
}
```

Rules:

1. **Everything is asynchronous.** The constructor starts a connection and
   returns. Results arrive in callbacks that run on the GTK main loop. The
   only allowed synchronous I/O is tiny local reads that must happen before
   the first frame (the config file) and sysfs (a few bytes, no blocking
   possible).
2. **No threads.** giomm's async APIs use the main loop; libpulse uses its
   glib mainloop adapter. Without threads there is nothing to lock.
3. **`available()` starts false** and modules hide themselves until it's
   true. Missing daemon = quiet `g_debug`, not a warning storm.
4. **Emit `signal_changed()` after every state update**, including the first
   one. Modules call `update()` once in their constructor too, so they're
   correct even if the service was already connected.
5. **Writers update local state first** ("optimistic"), emit, then send the
   request. The backend's own change notification confirms or corrects.
   Sliders would otherwise jump back while waiting.
6. **Log with `g_warning` for things the user should know** (a request
   failed) and `g_debug` for expected absences (no battery on a desktop).

## Callback lifetime

Async callbacks are lambdas that usually capture `this`. Services are
singletons that live for the whole process, so that is safe *for services*.
For widgets it is not: a module could be destroyed while its request is in
flight. Today modules live as long as the bar (forever), so nobody guards
against it, but if you ever create modules dynamically, either route the I/O
through a service or hold a `sigc::trackable`-aware connection.

Anything else the callback needs must be kept alive by capture: the
`Gio::SocketConnection`, the payload string, the `Gio::File`. Look at
`Hyprland::request()` for the pattern (`auto payload = std::make_shared<...>`
captured into the write callback).

## Racing replies: the serial guard

When events can arrive faster than replies, an old reply may land after a
newer one and overwrite fresher state. Guard with a counter:

```cpp
void Foo::refresh() {
    const auto serial = ++refresh_serial_;
    Backend::get().request("...", [this, serial](const std::string& reply) {
        if (serial != refresh_serial_)
            return;   // a newer refresh is in flight; ignore this one
        // ... apply reply ...
    });
}
```

Used in `Workspaces::refresh()`, `Bar::refresh_workspace_empty()`,
`NetworkManager::resolve_*`, `BatteryPanel::query_monitor()`.

## Backend recipes

### DBus property watcher (UPower, PowerProfiles, NetworkManager)

The lightest way to consume a DBus daemon is a `Gio::DBus::Proxy`: it caches
the object's properties and emits `signal_properties_changed()` when the
daemon notifies. No generated code, no extra library.

```cpp
PowerProfiles::PowerProfiles() {
    Gio::DBus::Proxy::create_for_bus(
        Gio::DBus::BusType::SYSTEM,
        "net.hadess.PowerProfiles",           // bus name
        "/net/hadess/PowerProfiles",          // object path
        "net.hadess.PowerProfiles",           // interface
        [this](Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                proxy_ = Gio::DBus::Proxy::create_for_bus_finish(result);
            } catch (const Glib::Error& e) {
                g_debug("power-profiles-daemon unavailable: %s", e.what());
                return;                       // available_ stays false
            }
            proxy_->signal_properties_changed().connect(
                [this](const Gio::DBus::Proxy::MapChangedProperties&,
                       const std::vector<Glib::ustring>&) { read_properties(); });
            read_properties();
        });
}

void PowerProfiles::read_properties() {
    Glib::VariantBase value;
    proxy_->get_cached_property(value, "ActiveProfile");
    if (value.gobj() != nullptr) {
        profile_ = Glib::VariantBase::cast_dynamic<Glib::Variant<Glib::ustring>>(value).get();
        available_ = true;
    }
    changed_.emit();
}
```

Reading a property: `get_cached_property(out, "Name")` then
`cast_dynamic<Glib::Variant<T>>(out).get()`. `T` must match the DBus
signature exactly (`bool` for `b`, `guint32` for `u`, `gint64` for `x`,
`double` for `d`, `Glib::ustring` for `s`, `Glib::DBusObjectPathString` for
`o`). A mismatch throws `std::bad_cast`.

Writing a property (no proxy helper for that): call
`org.freedesktop.DBus.Properties.Set` on the connection with a
`(ssv)` tuple. `PowerProfiles::set_profile()` is the reference.

Calling a method: `proxy_->get_connection()->call(path, interface, method,
parameters, callback, bus_name)`; finish with `call_finish(result)` and
unpack with `reply.get_child(variant, index)`. `UPower::find_battery_device()`
shows `EnumerateDevices` returning `ao`.

Explore a daemon before coding against it:

```sh
busctl --system introspect org.freedesktop.UPower /org/freedesktop/UPower/devices/DisplayDevice
busctl --system get-property net.hadess.PowerProfiles /net/hadess/PowerProfiles net.hadess.PowerProfiles ActiveProfile
```

For **session** bus daemons (notifications, media players) use
`Gio::DBus::BusType::SESSION`.

### DBus object trees (BlueZ)

Some daemons expose many objects (an adapter, each device) and add or
remove them at runtime. `Gio::DBus::ObjectManagerClient` tracks the whole
tree for you: it emits `signal_object_added` / `signal_object_removed` /
`signal_interface_proxy_properties_changed`. `Bluez` funnels all three into
one `rebuild()` that re-derives every field from the current tree and emits
once. That is simpler and safer than tracking deltas. Per-object proxies
come from `manager_->get_interface(path, "org.bluez.Device1")`.

### Optimistic value with a pending target

Some daemons emit several `PropertiesChanged` *before* the property you
actually set flips (BlueZ `Powered`, NetworkManager `WirelessEnabled`).
Each re-read would bounce the panel switch back mid-toggle. The fix used in
both services: remember the target (`pending_power_ = 1`), prefer it over
the stale read while set, and clear it when the daemon confirms or a short
timer expires. Use this whenever a toggle visibly flickers.

### Owning a DBus name and serving methods (NotificationService)

The notification daemon is the other direction: *we* are the server.

1. `Gio::DBus::own_name(SESSION, "org.freedesktop.Notifications", on_bus_acquired, ...)`.
   With default flags, if another daemon holds the name we queue and get
   `name_acquired` when it exits; `available()` flips then.
2. In `on_bus_acquired`, parse the interface XML with
   `Gio::DBus::NodeInfo::create_for_xml()` and
   `connection->register_object(path, interface_info, vtable)`. The vtable's
   method handler receives `(sender, path, interface, method, parameters,
   invocation)`; unpack `parameters` with `get_child()` and reply with
   `invocation->return_value(...)`.
3. Emit signals with `connection->emit_signal(path, interface, "ActionInvoked", "", params)`.
4. Keep the registration id: unowning the name (config `enabled=false`)
   must not unregister the object, because re-registering on the same
   connection errors.

`src/services/notifications.cpp` is long but linear; read `on_method_call`
and `handle_notify` first.

### Persisting runtime state

Services that must remember things across restarts (history, pins) write
JSON into `~/.cache/hypr-shell/`, never into `config.json`. Pattern: an
initial synchronous read at construction (same exception as Config), then
debounced (~200 ms) asynchronous saves via `Gio::File::replace_contents_async`.

### Unix socket request/reply and event stream (Hyprland)

`Gio::SocketClient::connect_async` with a `Gio::UnixSocketAddress`, then
`write_all_async` and a recursive `read_bytes_async` until EOF. For a
persistent event stream keep the connection, keep reading, and split on
newlines into a buffer you own. Full walk-through in
[Hyprland IPC](10-hyprland-ipc.md); the code is `src/services/hyprland.cpp`
and is short enough to read top to bottom.

### Library with its own event loop (libpulse)

libpulse wants to own a mainloop. `pa_glib_mainloop_new(nullptr)` plugs it
into GLib's default context so all pulse callbacks fire on the GTK thread.
Static C callbacks receive `void* self`, which you cast back to the
service. The context is created with `PA_CONTEXT_NOFAIL` so a PipeWire
restart reconnects on its own. Read `src/services/pulse.cpp`.

The same approach works for any C library with a GLib integration
(libnm, bluez via GDBus, etc.), but prefer plain DBus proxies when the
protocol is simple: fewer dependencies.

### sysfs + privileged writes (Brightness)

sysfs files don't emit inotify events, so the service exposes `refresh()`
and the UI calls it when a panel opens. Writes to `/sys` need root; the
project avoids that by asking logind (`org.freedesktop.login1.Session`
`SetBrightness` on session `auto`), which the session owner may call
without polkit. Writes are debounced (100 ms) so a slider drag doesn't
flood the bus. `src/services/brightness.cpp`.

### Shelling out (nmcli, bluetoothctl, paplay, rfkill)

When a native protocol is impractical (NetworkManager's Wi-Fi connect flow
with secrets agents, BlueZ pairing which needs an Agent1 implementation),
run a command asynchronously with `Gio::Subprocess`
(`communicate_utf8_async`) and parse its output. Rules learned the hard way:

- Force a stable locale/format (`nmcli -t -f ... --escape yes`) and handle
  the `\:` escapes.
- `nmcli` can exit 0 on failure; check the output for `Error`.
- Never `system()` or `popen()`: they block.

`NetworkManager::run_nmcli()` is the helper to copy.

### Polling only while a consumer is registered (SystemStats, Mpris)

A value that needs a timer to stay fresh must not cost anything while
nothing shows it. Expose `register_consumer()` / `unregister_consumer()`,
start the timer when the count goes 0 → 1 and stop it at 1 → 0; the widget
registers in `set_open(true)` and unregisters in `set_open(false)` and its
destructor. `Mpris` also refreshes immediately on registration so the panel
never shows a stale position, and `Mpris::position()` extrapolates from the
last known value so nothing is lost while no one polls.

## Where to add a service

1. `src/services/<name>.{hpp,cpp}`, listed in `meson.build`.
2. New dependency? Add `dependency('...')` in `meson.build` and the pacman
   package to `DEPS` in `install.sh`. Prefer DBus proxies over new libraries.
3. A row in the services table in [code tour](03-code-tour.md) and in the
   Layout block of `CLAUDE.md`.
4. A decision-log line if you chose between backends (e.g. "BlueZ via DBus
   proxies, no libbluetooth").
