# 13 — Roadmap guide

The checklist itself lives in `CLAUDE.md` (Roadmap section) and is the
source of truth; tick boxes there. `todo.txt` in the repo root is the user's
running wish list. This page adds *how* to approach each remaining item,
which existing code to copy, and which traps to expect.

The working rule: **one phase (or one clearly bounded sub-step) at a time,
and the tree compiles and runs at the end of every session.** Don't
scaffold files for a later phase.

## Status at a glance (2026-09-03)

| Phase | Done | Remaining |
|-------|------|-----------|
| 0 Scaffold + bar | all | |
| 1 Config | config.json + hot reload, 4 positions, module toggles, layout, visibility | **per-monitor bars** |
| 2 More modules | battery, network (+Wi-Fi panel), audio (+panel), bluetooth (+panel) | **system tray, keyboard layout, system stats** |
| 3 Notifications | **all**: daemon, history panel, toasts, DND, rules, sounds, settings page | (not ported: markdown, per-monitor popups, media/battery toasts, swipe/animations) |
| 4 Panels & OSD | calendar, battery panel, OSD (volume / mic / brightness / lock keys) | **control center** |
| 5 Lock & idle | idle daemon (ext-idle-notify-v1, fade grace period), lock screen (ext-session-lock + PAM, Noctalia's cover/login UI, background + blur settings) | **idle inhibitor DBus API (`org.freedesktop.ScreenSaver`), per-monitor lock wallpaper/monitor selection** |
| 6 Settings app | sidebar split view (Bar / Launcher / Notifications), module subpages | **full option coverage, search, the pages in todo.txt** |
| 7 Extras | app launcher (list view), app menu (grid, right-click pin menu), taskbar, wallpaper, night light | launcher grid view, screenshots, systemd units |

## Phase 1 — per-monitor bars

Today there is exactly one `Bar` owned by `App`. Hyprland tells us about
outputs via `j/monitors` and the `monitoradded` / `monitorremoved` events.

1. `App` keeps a `std::map<std::string, std::unique_ptr<Bar>>` keyed by
   monitor name; query `j/monitors` at startup, add/erase on the events.
2. `gtk_layer_set_monitor(window, GdkMonitor*)` pins a bar to an output.
   Map the Hyprland monitor name to a `Gdk::Monitor` via
   `Gdk::Display::get_monitors()` and the connector name.
3. Workspaces are per-monitor in Hyprland: filter `j/workspaces` by
   `monitor` and use the `focusedmon` event.
4. Config: `bar.monitors` (list of names, empty = all). The settings app
   can list connected outputs from `j/monitors`.
5. Notification popups then gain a monitor selection too (Noctalia has one;
   it was skipped for exactly this reason).

Trap: a removed monitor destroys widgets with in-flight async callbacks.
Route I/O through services and check `get_mapped()` before touching widgets
in callbacks.

## Phase 2 — remaining modules

**Keyboard layout.** Hyprland event `activelayout` (`keyboard,layout`) and
`j/devices` for the initial state. Tiny `Gtk::Label` module. Click →
`hl.dsp.switch_xkb_layout` (verify the dispatcher name with the `hl.dsp`
introspection trick in [Hyprland IPC](10-hyprland-ipc.md)). Setting: short
vs long layout names.

**System stats (CPU / memory / temperature).** Read `/proc/stat`,
`/proc/meminfo`, `/sys/class/hwmon/*/temp*_input` on a timer, asynchronously
(the [tutorial](05-tutorial-new-module.md) module is exactly this shape).
One `SystemStats` service feeding small modules. Config: interval, which
stats, format.

**System tray (StatusNotifierItem + DBusMenu).** The hardest remaining
module. You must *own* `org.kde.StatusNotifierWatcher` on the session bus
(the notification service shows how to own a name and serve methods), track
`RegisterStatusNotifierItem`, then per item create a proxy on
`org.kde.StatusNotifierItem` (`IconName` / `IconPixmap` / `Status` /
`ToolTip`), and on click `Activate` or open the `com.canonical.dbusmenu`
menu (`GetLayout` → `Gtk::PopoverMenu`). Waybar's `modules/sni/*.cpp` is the
closest reference. Land it in steps: watcher → icons → left click → menus.

## Phase 4 — OSD and control center

**OSD — landed 2026-09-03.** `services/osd` (when to show: Pulse/Brightness/
LockKeys diffs, startup grace, panel-open suppression) + `bar/osd_window`
(click-through overlay layer window, fixed size per orientation, card
animated with a `Gtk::Fixed` child transform) + `services/lock_keys`
(200 ms LED polling). Config `osd.enabled` / `osd.location`, settings page
"On-screen display". Remaining ideas from Noctalia: per-monitor OSD (needs
phase 1), configurable auto-hide / per-type toggles, `volumeOverdrive`
coloring above 100 %, IPC custom-text OSD.

**Control center.** A larger panel gathering the existing cards (audio,
network, bluetooth, brightness, power profile) plus quick toggles (DND,
Wi-Fi, Bluetooth). Extract the card widgets from the existing panels into
shared classes first so both places use them.

## Phase 5 — lock screen and idle

**Lock — landed 2026-09-03.** `bar/lock_screen` + `bar/lock_surface` +
`services/pam_auth`: `GtkSessionLockInstance`, one window per monitor,
PAM on a worker thread posting back through `Glib::Dispatcher`, PAM service
auto-detected (`login` first — no `/etc/pam.d/hypr-shell` needed). All lock
requests funnel through `request_lock()` (idle, session menus,
`hypr-shell --lock`, logind's `Lock` signal). Test with `HS_LOCK_PREVIEW=1`
(plain overlay window, Escape closes) before ever locking for real, and keep
a TTY open the first time. Remaining ideas: Noctalia's `lockScreenMonitors`
(black surfaces on chosen monitors), tint, font, fingerprint (`fprintd`).

**Idle.** `ext-idle-notify-v1` is a Wayland protocol: generate the client
code with `wayland-scanner` (meson `wayland` module), get the `wl_display`
from `gdk_wayland_display_get_wl_display()`, one notification per stage (dim
→ lock → dpms off via `hl.dsp.dpms`). Inhibitors: honour
`org.freedesktop.ScreenSaver` `Inhibit` on the session bus. Config: stage
timeouts, per-stage enable; settings page "Idle" from `todo.txt`.

## Phase 6 — settings app coverage

The sidebar exists. What remains is breadth: every config key must have a
control (audit against [config reference](04-config-reference.md)), plus the
pages the user listed in `todo.txt`:

- **User Interface**: font selection, font size, accent colour, dark mode.
  This is the theming work: turn the hardcoded colours in `data/css/*` into
  tokens the shell regenerates as a CSS provider from config (the
  `background_opacity` rule is the pattern, scaled up). Do the token
  refactor first, then the page.
- **Wallpaper**, **Display**, **On-Screen Display**, **Lock Screen**,
  **Idle**, **VPN**, **Hotspot**, **Session Menu**: each is a top-level
  config object + a sidebar page + (usually) a service. Follow the
  [settings app](07-settings-app.md#adding-a-sidebar-page) recipe.
- **Clock**: AD/BS date type in the calendar, tooltip format.
- A search entry over row titles would round out the GNOME-Settings feel.

## Phase 7 — extras

- **Launcher grid view** (default grid, view toggle), icon customisation
  (rocket / icon+text / text only), buttons next to the search (settings,
  session dropdown): all in `todo.txt`. The list-view code in
  `launcher_window.cpp` is provider-agnostic; the grid is a second row
  renderer plus a `Gtk::FlowBox` or `Gtk::GridView`.
- **Taskbar module**: landed 2026-09-04 (`bar/modules/taskbar`). Left for
  later: per-monitor filtering once per-monitor bars exist (today the single
  bar's monitor is used), Noctalia's drag shift animation, capsule clipping
  for `smart_width` without titles.
- **Wallpaper** via `hyprpaper`/`swww` IPC, **screenshot** helper wrapping
  `grim`/`slurp`, **systemd user unit** as an alternative to `exec-once`.

## Before starting any item

1. Read the corresponding Noctalia component if one exists: the project
   deliberately mirrors its semantics and look, and the decision log notes
   what was intentionally *not* ported.
2. Sketch config keys and settings controls **first**; they ship together.
3. Check what the backend needs; prefer DBus proxies over new libraries,
   and shelling out (nmcli, bluetoothctl) over reimplementing agents.
4. Add the decision-log line when you choose an approach, not at the end.
