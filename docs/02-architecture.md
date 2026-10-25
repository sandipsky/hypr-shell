# 02 — Architecture

hypr-shell is two programs that share one file.

```
 hypr-shell-settings  ──writes──▶  ~/.config/hypr-shell/config.json  ◀──watches──  hypr-shell
 (libadwaita window)                                                                (the bar)
```

There is no socket, no DBus interface, no IPC between them. The settings app
edits JSON; the shell's `Config` service notices the file change and
re-applies everything. Hand-editing the file with a text editor works just
the same.

## The shell process, top to bottom

```
App (Gtk::Application, src/main.cpp)
 ├─ loads CSS: built-in theme (style.css → @import css/*.css) → opacity rule → user style.css
 ├─ Config::get()            singleton, config.json + FileMonitor
 ├─ GAction "launcher"       toggles the launcher; `hypr-shell --launcher` forwards to it
 ├─ Bar (Gtk::ApplicationWindow, src/bar/bar.cpp)
 │   ├─ layer-shell setup (edge, anchors, exclusive zone, namespace "hypr-shell")
 │   ├─ Gtk::CenterBox "bar-inner"
 │   │   ├─ start_box_   ← modules from bar.layout.left
 │   │   ├─ center_box_  ← modules from bar.layout.center
 │   │   └─ end_box_     ← modules from bar.layout.right
 │   ├─ modules (value members): Launcher, Workspaces, ActiveWindow, Network,
 │   │    Bluetooth, Volume, Battery, Notifications, Clock — each owns its
 │   │    popover/panel (Calendar, BatteryPanel, AudioPanel, NetworkPanel,
 │   │    BluetoothPanel, NotificationPanel)
 │   └─ trigger_ (second 1px layer window for auto-hide)
 ├─ NotificationPopups (Gtk::Window, layer "hypr-shell-notifications")  toast stack
 └─ LauncherWindow (Gtk::ApplicationWindow, layer "hypr-shell-launcher")  fullscreen overlay

Services (singletons, src/services/): Config, Hyprland, UPower, PowerProfiles,
NetworkManager, Pulse, Brightness, Bluez, NotificationService, Apps
(+ math_eval: pure functions for the launcher's calculator)
```

The bar is no longer the only window. Notification toasts and the launcher
are separate layer-shell windows owned directly by `App`; they consume the
same services as the bar modules.

### Three layers

1. **Services** (`src/services/`). One class per backend, each a singleton
   reached with `Foo::get()`. A service owns a connection to something
   outside the process (Hyprland's sockets, a DBus daemon, PulseAudio,
   sysfs) and exposes two things: plain getters for the current state, and
   `sigc::signal`s that fire when the state changes. Services never touch
   widgets.

2. **Modules** (`src/bar/modules/`). One small widget per bar item. In its
   constructor a module adds its CSS classes, connects to the services it
   needs, and calls `update()` once. `update()` reads the service getters
   and sets label text / CSS classes / visibility. Modules never do I/O
   themselves; they ask a service.

3. **Panels** (`src/bar/*_panel.cpp`, `calendar.cpp`). The content of the
   popover that opens when you click a module. Also service consumers, but
   they additionally *write* (set volume, connect Wi-Fi) through service
   setter methods.

The dependency direction is strictly downwards: panels → modules → services.
A service never includes a module header.

### The update cycle

Every visible thing follows the same loop:

```
backend event ──▶ service updates its fields ──▶ service emits signal_changed()
        ──▶ module/panel slot runs update() ──▶ widgets reflect the new state
```

There is no polling loop in the modules (the clock's once-a-minute timer is
the exception, and even that is a scheduled callback, not a loop). Because
every callback runs on the GTK main thread, there is no locking anywhere.

### Why singletons with signals

A bar has exactly one Hyprland, one battery, one default audio sink. Making
each backend a process-wide singleton means every module and panel sees the
same state and the same connection, and connecting is a one-liner:

```cpp
Pulse::get().signal_changed().connect(sigc::mem_fun(*this, &Volume::update));
```

The singleton is created on first use (`static Foo instance;` inside
`get()`), so a service whose module is disabled is simply never constructed.

## Configuration flow

`Config` (`src/services/config.cpp`) is itself a service:

- On construction it reads `config.json` synchronously (tiny file, needed
  before the first frame) and parses it with nlohmann-json. Every key is
  optional; missing or malformed values fall back to a default.
- It installs a `Gio::FileMonitor` on the file. On change it reloads and
  emits `signal_changed()`.
- `Bar::apply_config()` and every module that has settings connect to that
  signal and re-read the getters. Nothing is cached in the widgets.

So a change made in the settings app, or with `vim`, propagates to the bar
in well under a second.

The config has two kinds of top-level object. `bar` holds everything about
the bar and its modules (`bar.<module>` sub-objects for per-module options).
`notifications` and `launcher` are top-level because they configure windows
that are not part of the bar; they are exposed as plain structs
(`Config::Notifications`, `Config::Launcher`) rather than one getter per
field.

**The shell only reads `config.json`; it never writes it.** Runtime state it
must keep (notification history, launcher pins, a do-not-disturb toggle
made from the bell) lives in `~/.cache/hypr-shell/` or in memory. When a
config value can also be changed at runtime (DND), the shell adopts the
config value only when it *changes*, so an unrelated save from the settings
app does not clobber the runtime toggle.

`Bar::apply_config()` is worth understanding: it removes all module widgets
from the three section boxes and re-appends them in the order given by
`bar.layout`, skipping disabled ones. A disabled module is therefore
*unparented*, not hidden. Modules keep `set_visible()` for their own use
(hide when the backend is missing) without fighting the config.

## Styling flow

Three CSS providers are stacked on the display, lowest priority first:

1. `data/style.css`, compiled into the binary as a GResource
   (priority `APPLICATION`). It only `@import`s the per-area files in
   `data/css/` (bar, calendar, panels, notifications, launcher), which are
   bundled too; GTK resolves the imports inside the resource.
2. A one-rule provider generated from `bar.background_opacity`
   (priority `APPLICATION + 1`).
3. `~/.config/hypr-shell/style.css`, hot-reloaded (priority `USER`).

Users can override anything without touching the source. See
[styling](09-styling-and-icons.md).

## The layer-shell window

A normal GTK window would float above other windows and get a title bar. The
bar instead uses `gtk4-layer-shell`, which turns the `GtkWindow` into a
Wayland *layer surface*: anchored to a screen edge, with an *exclusive zone*
so tiled windows don't overlap it. The C library is called directly on the
wrapped C object:

```cpp
auto* window = GTK_WINDOW(gobj());
gtk_layer_init_for_window(window);          // BEFORE the window is realized
gtk_layer_set_namespace(window, "hypr-shell");
gtk_layer_set_layer(window, GTK_LAYER_SHELL_LAYER_TOP);
gtk_layer_auto_exclusive_zone_enable(window);
gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_TOP, true);
// ...
```

`Bar::apply_config()` re-anchors at runtime when `bar.position` changes,
flips box orientations for vertical bars, and toggles CSS classes
`bottom`/`left`/`right` on the window so the theme can move the hairline
border.

## Auto-hide

`bar.visibility = "auto_hide"` is implemented in `bar.cpp` without unmapping
the window: the bar slides off-screen via a negative layer-shell margin on
its anchored edge (a `Gdk::FrameClock` tick callback animates it), and a
second 1px-tall layer window (`trigger_`, namespace `hypr-shell-trigger`)
sits on the same edge to detect hover and bring the bar back. Timings and
easing are copied from Noctalia. Popovers being open, or an empty workspace
(if configured), block hiding via a re-checking timer.

## The settings app

`src/settings/main.cpp` is a single file, plain C++ calling the libadwaita C
API (there are no C++ bindings for libadwaita). Its state is one `Settings`
struct holding the parsed JSON (`root`) and pointers to every row widget.
Each row's signal handler writes a key into `root` and calls `save()`, which
dumps the whole JSON back to disk. Keys the app doesn't know about are
preserved. The window is a GNOME-Settings-style split view with a sidebar
(Bar, Launcher, Notifications); the Bar page has per-module subpages. See
[the settings app](07-settings-app.md).

## Process lifetime

`App::on_activate()` calls `hold()` so the application stays alive even when
no window is mapped (`bar.visibility = "hidden"` unmaps the only window, and
GTK would otherwise quit). GApplication's app-id uniqueness
(`dev.hyprshell.Shell`) means launching a second copy just pokes the first.

That uniqueness is also how keybinds reach the shell: `App` registers a
GAction named `launcher`, and `hypr-shell --launcher` in a second process
sees (in `on_handle_local_options`) that an instance is already running,
calls `activate_action("launcher")` on it over DBus, and exits. The bar's
launcher module triggers the same action from inside the process.
