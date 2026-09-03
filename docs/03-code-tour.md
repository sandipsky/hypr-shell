# 03 — Code tour

A file-by-file guide. Line counts are approximate and only there to tell you
what is small and what is not.

## Repository root

| File | Purpose |
|------|---------|
| `meson.build` | The whole build: dependencies, the list of `.cpp` files, the GResource step, both executables, font/sound install, the desktop entry. **Every new `.cpp` must be added to the `sources` list here.** |
| `install.sh` | Arch-only: installs deps via pacman, configures meson with prefix `~/.local`, compiles, installs. `--restart` restarts a running bar. |
| `uninstall.sh` | Reverse of the above; `--purge` also deletes `~/.config/hypr-shell`. |
| `CLAUDE.md` | Project charter: stack, rules, Hyprland IPC cheat sheet, roadmap with checkboxes, decision log. Keep it updated. |
| `todo.txt` | The user's wish list of future settings pages and bar features (theming, wallpaper, display, OSD, lock, idle, VPN, hotspot, session menu, taskbar, control center, launcher grid view). Not a roadmap; consult it when picking the next item. |
| `.gitignore` | Just `build/`. |
| `docs/` | These pages. |

## `data/`

| File | Purpose |
|------|---------|
| `style.css` | Theme entry point: only `@import`s the files below. Embedded as a GResource. |
| `css/bar.css` | Bar window, modules, workspace pills, status icons, bell badge. |
| `css/calendar.css` | Calendar popover (`cal-*`). |
| `css/panels.css` | Battery / audio / network / bluetooth panels (`bp-*`, `ap-*`, `np-*`, `bt-*`), the NToggle-style switch, rate buttons. |
| `css/notifications.css` | History panel and toast popups (`notif-*`, `window.notification-popups`). |
| `css/launcher.css` | Launcher window (`launcher-*`, `window.launcher`). |
| `hypr-shell.gresource.xml` | Lists every file bundled into the binary (style.css + css/*). Add new CSS files here. |
| `hypr-shell-settings.desktop.in` | Launcher entry template; `@bindir@` is replaced at build time. Presents as "Settings" with `Icon=org.gnome.Settings` (not shipped) and `StartupWMClass=dev.hyprshell.Settings`. |
| `fonts/noctalia-tabler-icons.ttf` | Tabler icon font (MIT) for almost every glyph. |
| `fonts/SegoeIcons.ttf` | Microsoft "Segoe Fluent Icons" for the Windows-11-style battery. Proprietary; personal use only. Remove before publishing. |
| `fonts/*-license.txt`, `*-NOTICE.txt` | License texts installed alongside the fonts. |
| `sounds/notification-generic.wav` | Default notification sound (from Noctalia, MIT), installed to `<datadir>/hypr-shell/sounds`. |

## `src/main.cpp` (~170 lines)

Defines `hyprshell::App`, a `Gtk::Application` subclass with app id
`dev.hyprshell.Shell`.

- Constructor: declares the `--launcher` command-line option and the
  `launcher` GAction that toggles the launcher window.
- `on_handle_local_options()`: runs in the *invoking* process. If
  `--launcher` was passed and an instance is already running, forwards the
  action to it and exits; otherwise remembers to open the launcher after
  startup.
- `on_activate()`: checks `gtk_layer_is_supported()`, calls `hold()`,
  installs the CSS providers, then creates and adds three windows: the
  `Bar`, `NotificationPopups`, and `LauncherWindow`.
- `apply_bar_opacity()` regenerates the tiny opacity CSS rule on every
  config change.

## `src/bar/`

### `bar.hpp` / `bar.cpp` (~400 lines)

The bar window. Owns every module as a value member and the auto-hide
machinery.

- `Bar::Bar()` — layer-shell init for the bar and the trigger strip, hover
  controllers, Hyprland event hookup for auto-hide, first `apply_config()`.
- `apply_config()` — re-anchors the window, sets CSS position classes, flips
  orientations, rebuilds the three section boxes from `bar.layout`, applies
  the visibility mode. Runs on every config change.
- `module_widget(name)` — string → widget pointer. **Add a branch here for a
  new module.**
- auto-hide: `set_hovered / schedule_show / schedule_hide / set_hidden /
  apply_slide / peek / refresh_workspace_empty`.

### `modules/`

Each module is a header + source pair. All of them add CSS classes `module`
and their own name, connect to services in the constructor, have an
`update()` that reads service state, and hide themselves when their backend
is missing.

| Module | Base | Backend | Notes |
|--------|------|---------|-------|
| `launcher` | `Gtk::Box` + icon label | none | Search glyph; click activates the app's `launcher` GAction. Default section: left. |
| `workspaces` | `Gtk::Box` of buttons | Hyprland | Rebuilds from `j/workspaces` + `j/activeworkspace` (serial-guarded). Scroll steps locally; fixed mode shows 1..N with placeholders. |
| `active_window` | `Gtk::Box` (icon + label + rotated drawing area) | Hyprland | `activewindow` event (`class,title`). Icon via `Gio::DesktopAppInfo`. Vertical bars draw the title rotated. |
| `network` | `Gtk::Box` + icon label | NetworkManager | Glyph from connectivity + strength; ethernet wins. Click → `NetworkPanel`. |
| `bluetooth` | `Gtk::Box` + icon label | Bluez | off / on / connected glyphs, tooltip = first connected device. Click → `BluetoothPanel`. |
| `volume` | `Gtk::Box` + icon label | Pulse | Glyph from mute/level. Left click → `AudioPanel`; right click toggles mute. |
| `battery` | `Gtk::Box` with `Gtk::Overlay` of two labels | UPower + PowerProfiles | Win11 look; CSS classes `charging` / `saver`. Click → `BatteryPanel`. |
| `notifications` | `Gtk::Box` with `Gtk::Overlay` (icon + badge) | NotificationService | Bell / bell-off (DND), unread dot. Click → `NotificationPanel`; right click toggles DND. `bar.notifications.*` hide rules. |
| `clock` | `Gtk::Label` | none | strftime formats from config. Click → `Calendar`. |

### Panels and other windows

| File | Opened by | Content |
|------|-----------|---------|
| `calendar.{hpp,cpp}` | clock | Header card with seconds ring (cairo) + month grid; scroll changes month. |
| `battery_panel.{hpp,cpp}` | battery | Charge card, power-profile slider, brightness slider, refresh-rate buttons; cards hide per backend / `bar.battery`. |
| `audio_panel.{hpp,cpp}` | volume | Output and Input cards: device, slider, percent, mute. |
| `network_panel.{hpp,cpp}` | network | Wi-Fi switch, Connected card, scrolled Available list, inline password. Fixed 330×440. |
| `bluetooth_panel.{hpp,cpp}` | bluetooth | Power switch, disabled card, Connected / Paired / Available lists; discovery runs while open (`set_open`). Pair, connect, disconnect. Fixed 330×400. |
| `notification_panel.{hpp,cpp}` | notifications | History list with cards (icon, urgency dot, app, time, summary, body, actions, expand, delete), "Clear All". Rebuilds only while open; `signal_request_close`. Fixed 380×480. |
| `notification_popup.{hpp,cpp}` | `App` (always exists) | Toast stack as a layer window: up to 5 cards, per-urgency countdown bars, hover pauses, click/close/right-click actions, compact density. |
| `notification_ui.{hpp,cpp}` | shared | Relative-time formatter and the rounded notification icon widget (image > themed icon > desktop icon > bell). |
| `osd_window.{hpp,cpp}` | `App` (always exists); `Osd::signal_show()` | On-screen display as a click-through layer window (empty input region): fixed-size window per orientation (320x72 / 80x280), the card placed inside with a `Gtk::Fixed` child transform (fade + scale animation), cairo progress bar, lock-key text; auto-hides after 2 s. `HS_OSD_SHOW=volume|input|brightness|lock` shows one at startup. |
| `lock_screen.{hpp,cpp}` | `App` (always exists); `request_lock()` | The session lock (`GtkSessionLockInstance`): one `LockSurface` per monitor, the shared password text and Noctalia's LockContext state machine over `PamAuth`, logind `Lock` signal + `SetLockedHint`. `HS_LOCK_PREVIEW=1` shows the UI as a plain overlay window. |
| `lock_surface.{hpp,cpp}` (~600 lines) | `LockScreen` | One monitor's lock UI: `LockBackground`, cover stage (clock/date), login stage (avatar, name, password dots), info/error/countdown pills, battery + power button, session menu with 10s countdown. Hidden `Gtk::Text` receives typing. |
| `lock_background.{hpp,cpp}` | `LockSurface` | Custom widget: wallpaper decoded async at monitor pixel size, cover-scaled, GSK-blurred once into a cached texture. |
| `launcher_window.{hpp,cpp}` (~680 lines) | `App` via GAction | Fullscreen overlay: dim backdrop, centred panel with search entry, result list, footer. Providers: apps, calculator, settings search, session commands, web search. Keyboard navigation, hover-after-move selection, pin buttons, Spotlight-style grow animation when `show_all_apps` is off. |

CSS prefixes: `bp-*` (battery, reused as the generic card style), `ap-*`,
`np-*`, `bt-*`, `cal-*`, `notif-*`, `launcher-*`, `lock-*`.

## `src/services/`

Every service is `class Foo { static Foo& get(); ... sigc::signal<void()>& signal_changed(); }`
(except `math_eval`, which is a namespace of pure functions).

| Service | Talks to | How |
|---------|----------|-----|
| `config` | `~/.config/hypr-shell/config.json` | Sync first read, `Gio::FileMonitor` reloads. One getter per `bar.*` field; structs `Notifications` and `Launcher` for the top-level objects. **Add new keys here.** |
| `hyprland` | Hyprland's two sockets | Async `Gio::SocketClient`. `request(cmd, cb)`, `dispatch(lua)`, `signal_event()`. Typed helpers `focus_workspace`, `focus_window`, `set_monitor_mode`. |
| `upower` | `org.freedesktop.UPower` | `Gio::DBus::Proxy` on `DisplayDevice`; second proxy on the real battery for health. |
| `power_profiles` | `net.hadess.PowerProfiles` | Proxy; `set_profile()` writes `ActiveProfile` optimistically. |
| `network_manager` | `org.freedesktop.NetworkManager` + `nmcli` | Proxy chain root → ActiveConnection → AccessPoint, serial-guarded. Wi-Fi actions via `Gio::Subprocess`. Pending-target guard for the radio switch. |
| `bluez` (~420 lines) | `org.bluez` + `bluetoothctl` + `rfkill` | `Gio::DBus::ObjectManagerClient`; every change → `rebuild()` + emit. Discovery, pair (bluetoothctl), trust+connect, disconnect, auto-connect on power-on edge. |
| `notifications` (~950 lines) | owns `org.freedesktop.Notifications` on the session bus | Serves Notify/CloseNotification/GetCapabilities/GetServerInformation; history (100, persisted to `~/.cache`), image-data → PNG cache, Pango sanitising, rules, popups with countdown timer, sounds via `paplay`, DND. |
| `apps` | desktop entries via `Gio::AppInfo` | Index + `AppInfoMonitor` reload, dedupe, `fuzzy_score()`, launch, pinned apps persisted to `~/.cache/hypr-shell/pinned_apps.json`. |
| `math_eval` | nothing (pure) | `is_math_expression`, `evaluate` (recursive descent, functions, constants), `format_result`. Port of Noctalia's AdvancedMath.js. |
| `pulse` | pipewire-pulse via libpulse | `pa_glib_mainloop`; default sink and source; by-name setters. |
| `brightness` | sysfs + logind `SetBrightness` | `Gio::FileMonitor` on the sysfs file (userspace writes do raise inotify events) + explicit `refresh()`; debounced DBus write. |
| `lock_keys` | `/sys/class/leds/input*::{capslock,numlock,scrolllock}` | 200 ms polling while enabled (LED state is kernel-driven, no inotify); first read syncs silently, then `signal_changed(key, on)`. |
| `osd` | `pulse`, `brightness`, `lock_keys`, `config` | Decides when the OSD shows: diffs Pulse state, 2 s startup grace, suppression while the audio/battery panels are open, 300 ms input suppression after a sink switch; `signal_show(type)` drives `OsdWindow`. |
| `session` | nothing | Enabled session actions, `run_session_action()`, `spawn_detached()`, `open_settings()`, and the lock hooks: `request_lock()` / `signal_lock_requested()` (services → lock screen) and `set_session_locked()` / `signal_session_locked()` (lock screen → services). |
| `pam_auth` | Linux-PAM | `PamAuth`: one worker thread per attempt (the project's only thread), conversation prompts handed to the main loop via `Glib::Dispatcher`, `respond()` wakes the waiting conversation. |
| `idle` | `ext-idle-notify-v1` | Stages screen off / lock / suspend with the fade grace period; "lock" is `request_lock()`, suspend waits for `signal_session_locked()` (3s cap) when `lock_before_suspend`. |

## `src/settings/main.cpp` (~2000 lines)

The settings app, one file, libadwaita C API.

1. **Tables**: `kModules[]` (key, title, subtitle, default section) and the
   string tables for combo rows (positions, visibility, active-window
   options, notification density/location, rule actions).
2. **`struct Settings`**: parsed JSON `root`, `loading` flag, a pointer to
   every row widget, the resolved layout, the rules list.
3. **`module_index(key)`**: finds a module's row so cog buttons attach by
   key, not by hard-coded index.
4. **`load()` / `save()`**: read/write the JSON file, preserving unknown keys.
5. **`populate()`**: fills every widget from `root` under `loading = true`.
6. **Handlers** `on_*`: each writes one key and saves. `*_object(s)` helpers
   return-and-create sub-objects (`bar.clock`, `bar.bluetooth`,
   `bar.notifications`, top-level `notifications` incl. `sounds` and
   `save_to_history`, top-level `launcher`).
7. **Layout editor** (`resolve_layout`, `rebuild_layout_rows`) and **rules
   editor** (`rebuild_rule_rows`, `AdwAlertDialog` for add/edit).
8. **`on_activate()`**: builds the Bar page + module subpages (Workspaces,
   Clock, Active window, Battery, Bluetooth, Notifications), the Launcher
   page, the Notifications page, connects signals, wires cogs, and
   assembles the `AdwNavigationSplitView` with a `GtkListBox` sidebar
   driving a `GtkStack`.

See [the settings app](07-settings-app.md) for how to extend it.
