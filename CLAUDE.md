# hypr-shell

A GTK4-native desktop shell for Hyprland, built from scratch **step by step**, replacing
the user's forked Noctalia (Quickshell/QML) setup. Primary goals: performance and low
footprint — native compiled code, no JS/QML runtime, minimal dependencies.

## How to work on this project

- Build **one roadmap phase at a time**. Do not scaffold ahead of the current phase;
  every session must end with the project compiling and runnable.
- The stack below is decided — do not switch language or toolkit without discussing
  it with the user first.
- When a phase (or notable sub-step) lands: tick the roadmap checkbox and, for any
  non-obvious choice, add a line to the decision log at the bottom.
- **Keep the settings app in sync with the shell.** Whenever a change introduces or
  alters user-facing customizable behavior (a new module, a position/size/format/
  threshold choice, a toggleable feature), wire it through `config.json`
  (`Services.Config`) **and** add the matching control to `hypr-shell-settings` in the
  same session. Decide sensible candidates yourself — hard-coded look-and-feel values
  a Noctalia/Waybar user would expect to tweak are candidates; internal constants are
  not — and the user may also name specific ones to expose.
- Verify with `meson compile -C build`, then test inside the live Hyprland session
  (see dev loop below).

## Stack (decided)

- **C++20 + gtkmm-4.0** (official GTK4 C++ bindings) — user's language choice; native
  performance, sigc++ signals, RAII. Same stack as Waybar, so its codebase is a good
  pattern reference. Async I/O via giomm (callback style) — never block the main loop.
- **gtk4-layer-shell** — a C library, called directly on the wrapped window:
  `gtk_layer_init_for_window(GTK_WINDOW(window.gobj()))`. pkg-config name
  `gtk4-layer-shell-0`. The same library ships ext-session-lock support
  (`gtk4-session-lock.h`, `GtkSessionLockInstance`) for the lock-screen phase.
- **nlohmann-json** — Hyprland IPC replies; later the config file.
  meson dependency name: `nlohmann_json`.
- **meson + ninja**, install prefix `~/.local` (no sudo needed beyond pacman).
- **libadwaita** — for the settings app only (GNOME-Settings look & UX). There is no
  official libadwaitamm, so `hypr-shell-settings` calls the libadwaita C API directly
  from C++ (standard practice); it depends only on libadwaita + nlohmann-json, no gtkmm.
  The shell itself stays plain GTK4 + custom CSS.
- Target platform: **Arch Linux + Hyprland** only (Hyprland 0.56+ at time of writing).

## Layout

```
meson.build                    single meson file; include root is src/
install.sh / uninstall.sh      Arch-only; deps via pacman, install via meson to ~/.local
data/style.css                 default theme entry — @imports data/css/* (GResource)
data/css/*.css                 per-area theme files: bar, calendar, panels,
                               notifications (GTK resolves the imports inside
                               the resource bundle)
data/hypr-shell.gresource.xml
data/hypr-shell-settings.desktop.in   (Exec gets the absolute bindir at build time)
data/fonts/                    noctalia-tabler-icons.ttf (installed to
                               ~/.local/share/fonts/hypr-shell, MIT license alongside)
src/main.cpp                   App (Gtk::Application), CSS loading + user-CSS hot reload
src/bar/bar.{hpp,cpp}          Bar window (layer-shell setup)
src/bar/modules/*.{hpp,cpp}    one widget per bar module (workspaces, active_window,
                               clock, network, volume, battery, bluetooth,
                               notifications)
src/services/config.{hpp,cpp}           config.json load + hot reload (Gio::FileMonitor)
src/services/hyprland.{hpp,cpp}         Hyprland IPC singleton
src/services/upower.{hpp,cpp}           battery via UPower DisplayDevice (Gio::DBus)
src/services/network_manager.{hpp,cpp}  NM primary connection + wifi strength (Gio::DBus)
src/services/power_profiles.{hpp,cpp}   active profile from power-profiles-daemon (Gio::DBus)
src/services/pulse.{hpp,cpp}            default-sink volume/mute (libpulse-glib)
src/services/brightness.{hpp,cpp}       backlight: sysfs reads + logind SetBrightness
src/bar/battery_panel.{hpp,cpp}         battery click panel (profile/brightness/rate)
src/bar/audio_panel.{hpp,cpp}           volume click panel (output/input levels)
src/bar/network_panel.{hpp,cpp}         network click panel (Wi-Fi selector)
src/services/bluez.{hpp,cpp}            BlueZ adapter/devices (Gio::DBus ObjectManager)
src/bar/bluetooth_panel.{hpp,cpp}       bluetooth click panel (power/auto-connect/pair)
src/services/notifications.{hpp,cpp}    org.freedesktop.Notifications daemon + history
src/bar/notification_panel.{hpp,cpp}    notification history panel (bell click)
src/bar/notification_popup.{hpp,cpp}    toast popup stack (layer window)
src/bar/notification_ui.{hpp,cpp}       shared icon/relative-time helpers
src/settings/main.cpp                   hypr-shell-settings (libadwaita C API, instant apply)
```

## Build / run / dev loop

```sh
./install.sh                 # deps (pacman) + build + install to ~/.local/bin
./install.sh --restart       # same, then restart the running instance
./uninstall.sh [--purge]     # remove binary [+ ~/.config/hypr-shell]

# dev loop while hacking:
meson compile -C build && pkill -x hypr-shell; ./build/hypr-shell
```

Autostart: `exec-once = ~/.local/bin/hypr-shell` in hyprland.conf.
Debug logging: run with `G_MESSAGES_DEBUG=all`.

## Architecture rules

- `src/services/` — singleton backends (`Foo::get()`) exposing state and
  `sigc::signal`s. **All I/O is async** (giomm async + callbacks); never block the GTK
  main loop, never spawn threads unless unavoidable.
- `src/bar/modules/` — small self-contained widgets that subscribe to services in
  their constructor. A module must degrade gracefully when its service is unavailable
  (e.g. running outside Hyprland). Modules live as value members of `Bar`; dynamically
  created children use `Gtk::make_managed`.
- One binary `hypr-shell` today. The settings app becomes a second executable
  (`hypr-shell-settings`) in this same meson project.
- Styling: default theme in `data/style.css` (GResource `/dev/hyprshell/Shell/`);
  user override `~/.config/hypr-shell/style.css` is hot-reloaded at
  `STYLE_PROVIDER_PRIORITY_USER`. CSS classes: `bar`, `bar-inner`, `module`, plus one
  class per module (`workspaces`, `active-window`, `clock`, ...).
- App id `dev.hyprshell.Shell` — GApplication uniqueness gives single-instance for free.
- Layer-shell gotchas: call `gtk_layer_init_for_window()` **before** the window
  is realized/presented; `set_decorated(false)` (GTK4 draws a CSD titlebar otherwise);
  keep `window.bar { background: transparent; }` in CSS. Layer namespace is
  `hypr-shell`, so users can target it in hyprland.conf: `layerrule = blur, hypr-shell`.

## Hyprland IPC cheat sheet

Sockets in `$XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE/`:

- `.socket.sock` — **one request per connection**: write the command, read the reply
  to EOF. Prefix `j/` for JSON (`j/workspaces`, `j/activeworkspace`, `j/activewindow`,
  `j/monitors`, `j/clients`) — query grammar unchanged.
- **Actions are Lua** since Hyprland 0.56: `dispatch <expr>` is shorthand for
  `return hl.dispatch(<expr>)`, where `<expr>` *constructs* a dispatcher, e.g.
  `hl.dsp.focus({ workspace = 3 })`, `hl.dsp.focus({ workspace = "e+1" })`,
  `hl.dsp.window.close()`. The pre-0.56 text grammar (`dispatch workspace 3`) is a
  Lua syntax error. Raw Lua runs via `eval <code>`; `eval` replies only `ok`/`error`,
  so introspect by smuggling values through `error(...)`, e.g.
  `hyprctl eval 'local t={} for k in pairs(hl.dsp) do t[#t+1]=k end error(table.concat(t," "))'`.
  Groups on 0.56.2: cursor dpms event exec_cmd exec_raw exit focus force_idle
  force_renderer_reload global group layout no_op pass release_input_capture
  send_key_state send_shortcut submap window workspace. From code, use the typed
  helpers on the service (`Hyprland::focus_workspace(...)`) so the Lua grammar
  stays in one file.
- `.socket2.sock` — newline event stream, `NAME>>DATA`. Events used so far:
  `workspace(v2)`, `createworkspace(v2)`, `destroyworkspace(v2)`, `renameworkspace`,
  `focusedmon`, `activewindow` (DATA is `class,title` — split on the **first** comma).
- Workspaces with negative ids are special workspaces (scratchpads) — hidden in the bar.
- Everything goes through `Services.Hyprland` (`request ()`, `dispatch ()`, `event` signal).
- Debugging: `hyprctl -j workspaces`; watch events with
  `socat -u UNIX-CONNECT:$XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE/.socket2.sock -`

## Roadmap

- [x] **Phase 0 — Scaffold + basic bar**: meson project, install/uninstall scripts,
      top bar with workspaces / focused-window title / clock, default CSS, user-CSS
      hot reload, Hyprland IPC service.
- [ ] **Phase 1 — Config system**: `~/.config/hypr-shell/config.json` + hot reload;
      bar position (top/bottom), height, module toggles & order; per-monitor bars
      (monitor add/remove handling).
      *(pulled forward 2026-08-31: config.json + hot reload, bar position
      (top/bottom/left/right — left/right are vertical bars), module toggles,
      module layout (section + order), and bar visibility (always show /
      always hide / auto-hide with Noctalia semantics) landed; per-monitor
      bars remain. bar.height was removed 2026-08-31 — CSS owns sizing)*
- [ ] **Phase 2 — More bar modules**: battery (UPower), network (NetworkManager),
      bluetooth (BlueZ), audio (PipeWire), system tray (StatusNotifierItem + DBusMenu),
      keyboard layout, system stats.
      *(pulled forward 2026-08-30: battery + network + audio status icons landed;
      2026-09-01: bluetooth (BlueZ) icon + click panel landed;
      tray, keyboard layout, stats remain)*
- [x] **Phase 3 — Notifications**: own `org.freedesktop.Notifications` daemon —
      popups, history/notification center, do-not-disturb. Only one daemon can own the
      bus name: mako/dunst/Noctalia's daemon must be disabled when this ships.
      *(2026-09-01: daemon (Notify/Close/capabilities, image-data caching),
      100-entry persisted history, DND, bell bar module + history panel, toast
      popups with per-urgency countdowns, per-app rules, sounds, and the
      settings app's Notifications sidebar page all landed. Not ported from
      Noctalia: per-monitor popup selection (needs phase 1's per-monitor
      work), markdown rendering, media/keyboard/battery toasts (need MPRIS/
      keyboard/battery-event services), swipe-to-dismiss, popup animations)*
- [ ] **Phase 4 — Panels & OSD**: volume/brightness OSD, calendar popover,
      control-center panel.
      *(pulled forward 2026-08-31: calendar popover on clock click, and the
      battery panel on battery click (power profile / brightness / refresh
      rate) landed; volume/brightness OSD and control center remain)*
- [ ] **Phase 5 — Lock & idle**: lock screen via ext-session-lock
      (gtk4-layer-shell's session-lock API) + PAM auth; idle service via
      ext-idle-notify-v1 (dim → lock → dpms off) with inhibitor support.
- [ ] **Phase 6 — Settings app**: `hypr-shell-settings` (GTK4 + libadwaita,
      GNOME-Settings-style sidebar + search) editing the same config.json; the shell
      applies changes live.
      *(pulled forward 2026-08-31: the executable exists — single bar page with
      position/height/module toggles, instant apply; the sidebar + search layout and
      full option coverage come with this phase)*
- [ ] **Phase 7 — Noctalia-parity extras** (as desired): app launcher, wallpaper
      handling, screenshot helpers, systemd user units.

## Decision log

- 2026-08-30 — Started in Vala, then **switched to C++ (gtkmm-4.0) at the user's request**
  before the first build; all Vala sources were removed. Waybar is the precedent for
  C++/gtkmm bars. For phase 3's DBus daemon, use Gio::DBus (giomm) with XML introspection
  data.
- 2026-08-30 — User-local install (`~/.local`): no sudo except for pacman deps.
- 2026-08-30 — Bar shows dynamic workspaces (existing ones only), specials hidden;
  persistent/pinned workspace display deferred to the phase-1 config.
- 2026-08-30 — Config will be a JSON file (not GSettings/dconf): dotfile-friendly,
  same hot-reload path serves both manual edits and the future settings app.
- 2026-08-30 — Workspace scroll originally used Hyprland's `"e+1"`/`"e-1"`
  selectors; **superseded 2026-08-31** by local stepping over the displayed
  workspace list, because fixed mode must navigate placeholder workspaces and
  the wrap-around toggle needs clamping Hyprland doesn't offer. Smooth deltas
  are still accumulated to one switch per wheel notch.
- 2026-08-30 — Hyprland 0.56 turned `.socket.sock` actions into Lua (see cheat
  sheet); the old `dispatch workspace 3` grammar silently broke every dispatch
  (bar clicks included — the g_warning was invisible because install.sh's restart
  sends stderr to /dev/null). Dispatcher Lua is built only inside
  `services/hyprland.cpp` (`focus_workspace()` helpers); modules never hand-write it.
- 2026-08-30 — Status icons render from **two icon fonts**, matching the user's
  Noctalia fork: wifi/volume use Tabler glyphs from `noctalia-tabler-icons.ttf`
  (MIT; bundled in data/fonts/, installed to ~/.local/share/fonts/hypr-shell), the
  battery is the win11-style Segoe Fluent Icons look ported from the fork's
  NBatteryWin11.qml — decile fill glyph (U+E851..E859, E83F) under a
  BatteryCharging0 frame (U+E85A) with green fill when plugged. "Segoe Fluent
  Icons" (SegoeIcons.ttf) is bundled too at the user's request — it is Microsoft
  proprietary, personal use only (see data/fonts/SegoeIcons-NOTICE.txt); strip it
  before ever publishing the repo. Icon thresholds copied from
  Noctalia: wifi ≥80/60/35/15, volume muted/0/≤50%/high. Bar text font set to
  "Fira Sans" (Noctalia's fontDefault).
- 2026-08-30 — Backend choices for status services: UPower and NetworkManager are
  consumed with plain `Gio::DBus::Proxy` (no libupower-glib / libnm dependency;
  NM chain root → ActiveConnection → AccessPoint is re-resolved on every NM
  property change, guarded by a serial like the workspaces refresh). Audio uses
  libpulse-mainloop-glib against pipewire-pulse (Waybar precedent), with
  PA_CONTEXT_NOFAIL so a PipeWire restart self-heals. Battery shows all three
  win11 looks: plugged (bolt frame E85A, green fill) wins over power-saver
  (leaf frame E863, amber fill, via net.hadess.PowerProfiles) wins over plain.
- 2026-08-31 — Settings app pulled forward from phase 6 (user request), bringing the
  config.json core of phase 1 with it so the settings have something to edit. Schema so
  far: `bar.position` ("top"/"bottom"), `bar.height` (px, 0 = automatic CSS height),
  `bar.modules.<name>` (bool; **absent = enabled**, so new modules default on).
  Instant-apply, GNOME-style: every widget change writes config.json (unknown keys
  preserved), the shell's Config service hot-reloads it — no IPC between the binaries.
  UI is AdwToolbarView + AdwPreferencesPage (AdwPreferencesWindow avoided — deprecated
  since libadwaita 1.6); AdwSwitchRow/AdwSpinRow need libadwaita >= 1.4.
- 2026-08-31 — Desktop entries must carry an **absolute Exec path** (meson
  configure_file from `.desktop.in`): the session/systemd-user PATH that app
  launchers resolve Exec against does not include ~/.local/bin, so a bare
  `Exec=hypr-shell-settings` silently launched nothing outside a shell.
- 2026-09-01 — Settings app presents as plain "Settings" (desktop Name, window
  title, sidebar header) with `Icon=org.gnome.Settings` — the GNOME Control
  Center icon, which hypr-shell does NOT ship: the user installs the svg
  themselves as org.gnome.Settings.svg in the icon theme path.
  StartupWMClass=dev.hyprshell.Settings maps the running window to the entry
  so docks pick up the same icon.
- 2026-08-31 — Module placement lives in `bar.layout` = `{left:[], center:[],
  right:[]}` (ordered name lists). Resolution: unknown names dropped, duplicates
  keep first placement, modules missing from every list append to their default
  section — so hand-edited partial configs and future modules keep working. The
  bar applies "enabled" by (un)parenting modules, not set_visible(): modules
  keep set_visible() for their own service availability (battery with no UPower
  etc.) without fighting the config — an enabled-but-hidden fight was a real
  bug. Settings app: per-module up/down + section dropdown, groups per section.
- 2026-08-31 — Per-module settings live under `bar.<module>` objects (first:
  `bar.workspaces` = mode "dynamic"/"fixed", fixed_count, scroll_wrap — semantics
  copied from Noctalia's workspaceMode/fixedWorkspaces: fixed shows 1..N with
  placeholders, keeps real workspaces beyond N, click on a placeholder creates
  it). In the settings app each such module gets an AdwNavigationView subpage,
  opened by a cog suffix on its module row; `HS_SETTINGS_PAGE=<tag>` opens a
  subpage directly (dev/screenshot hook).
- 2026-08-31 — Calendar popover (`src/bar/calendar.{hpp,cpp}`, opened by
  clicking the clock) is a 1:1 port of Noctalia's CalendarHeaderCard +
  CalendarMonthCard, including the seconds-progress ring (Gtk::DrawingArea /
  cairo) and scroll-to-change-month. Colors are hardcoded from a snapshot of
  the user's `~/.config/noctalia/colors.json` (mPrimary #bfc2ff etc.) — theme
  tokens become config when theming lands. GTK popovers with autohide grabs
  work fine on Hyprland layer surfaces. `bar.clock.first_day_of_week` (0=Sun
  default, 1=Mon) has a Clock settings subpage; calendar events (khal/EDS)
  were NOT ported. Dev hook: `HS_OPEN_CALENDAR=1` pops it on startup.
- 2026-08-31 — Vertical bars (`bar.position` left/right): all boxes flip
  orientation, anchors span top..bottom, css classes `left`/`right` flip the
  hairline + rotate paddings, active_window draws its title rotated 90°
  (book-spine, PangoCairo in a Gtk::DrawingArea — GTK4 labels can't rotate;
  icon stays upright above it), and the clock uses `bar.clock.format_vertical`
  (Noctalia
  semantics: strftime, space-separated tokens render stacked) vs
  `format_horizontal` (default "%H:%M %a, %b %d" ≙ Noctalia's
  "HH:mm ddd, MMM dd"). `bar.height` was dropped — CSS owns bar thickness.
  Invalid strftime input (mid-typing in settings) falls back to "%H:%M".
- 2026-08-31 — ActiveWindow gained Noctalia's widget settings (minus color):
  `bar.active_window` = hide_mode visible/hidden/transparent (default hidden,
  transparent keeps its space at opacity 0), show_title, title_mode
  title/appname (appname = desktop-entry display name), no_window_text
  default/desktop/none, show_icon. The module is now a Box (Gtk::Image +
  label); icons resolve via Gio::DesktopAppInfo from the window class
  (exact + lowercased "<class>.desktop", fallback generic executable icon).
  Settings subpage rows 3–4 hide when show_title is off, like Noctalia.
- 2026-08-31 — Bar visibility: `bar.visibility` = visible/hidden/auto_hide plus
  `bar.show_on_workspace_switch` (default true) and `bar.show_when_workspace_empty`
  (default false), semantics and timings copied from Noctalia's displayMode
  (hide 500ms after unhover, show 150ms after hover, ~200ms slide — ease-in-quad
  out, ease-out-cubic in). Auto-hide slides the bar off-screen via a **negative
  layer-shell margin** on its anchored edge (window stays mapped → no remap
  latency; off-screen surfaces get no input) with exclusive zone forced to 0;
  a second 1px layer window (`hypr-shell-trigger`) on the same edge re-reveals
  it on hover. Two hard-won gotchas: (1) a **fully transparent GTK4 window never
  commits a real buffer** and the layer surface then sizes to GtkWindow's 200px
  fallback instead of its 1px request — the trigger is painted rgba(0,0,0,0.01),
  ~1/255 alpha, invisible but sized correctly (plain `background: transparent`,
  `opacity: 0` in CSS, and `set_opacity(0)` all break it); (2) hiding every
  window (visibility=hidden) made GTK quit the app — `App::on_activate` now
  calls `hold()`. An open popover (calendar) blocks hiding via a 500ms re-check
  poll; the active workspace's emptiness comes from `j/activeworkspace` on
  workspace/openwindow/closewindow/movewindow events, serial-guarded. Peek on
  workspace switch shows then restarts the hide cycle. Note: opening the
  calendar needs the pointer on the bar surface itself — a popup grab from the
  1px trigger's input serial is denied and the popover instantly dismisses.
- 2026-08-31 — `bar.background_opacity` (0..1, default 0.88 = the theme's alpha,
  Noctalia's backgroundOpacity): the shell regenerates a one-rule CSS provider
  (`.bar-inner { background-color: alpha(#11111b, X); }`) at APPLICATION+1
  priority on every config reload — above the built-in theme, below the user's
  style.css. The alpha is formatted with g_ascii_dtostr (locale-proof: a
  decimal comma would break CSS parsing). Settings: GtkScale suffix in an
  AdwActionRow (libadwaita has no slider row), whole percents.
- 2026-08-31 — Battery click panel (`src/bar/battery_panel.{hpp,cpp}`), Noctalia's
  BatteryPanel.qml with the calendar's design language: charge card (level bar,
  "Plugged in"/time text), power-profile slider (3 snapped stops, leaf/scale/
  gauge tabler icons — written as C++ `\uXXXX` escapes, never literal PUA
  glyphs), brightness slider, refresh-rate pill buttons. Backends: profile via
  writable ActiveProfile property (optimistic local update so the slider
  doesn't bounce); brightness via a new service — sysfs reads (no inotify on
  sysfs → explicit refresh() when the panel opens) + logind Session.SetBrightness
  on session "auto" (session owner needs no polkit), 100ms write debounce;
  refresh rate via `eval hl.monitor({ output, mode = "WxH@R", position, scale })`
  (Hyprland 0.56 rejects `keyword monitor`; only an "ok" reply counts, then
  re-query j/monitors), rates = distinct Hz among availableModes at the current
  resolution, card hidden with <2 rates. Cards toggle via `bar.battery.show_*`
  (default true) with a Battery settings subpage. GTK gotcha: rounded scale/
  progressbar CSS needs explicit min-width/min-height on highlight/progress
  nodes or GTK warns about -2 min sizes. Dev hook: HS_OPEN_BATTERY=1.
  **Popover-anchor gotcha**: a popover parented to a module's Gtk::Box gets
  allocated INLINE by the box while open — the module grew by the popover's
  width and slid out of its bar section (a Gtk::Label anchor like the clock's
  is fine); parented to the battery's fill label (which sits under the frame_
  overlay child) the popover unmapped right after popup(). Anchor module
  popovers to a Label or a Gtk::Overlay, never to the module Box itself.
- 2026-08-31 — Audio panel on volume click (`src/bar/audio_panel.{hpp,cpp}`,
  Noctalia's AudioPanel output/input cards, bp-* styles): per card
  "<Kind> - <device description>", slider, %, round mute-toggle button
  (volume/mic glyphs, slider dims at 0.5 opacity while muted). Right click on
  the volume module toggles output mute (user request; Noctalia opens a
  context menu there instead). Pulse service extended to the default source
  (SOURCE subscription, get_source_info_by_name) plus setters
  set_[input_]volume/muted via the by-name pa APIs with the device's channel
  count from its last info callback; local state updates optimistically
  before the server round-trip. Popover anchored to the icon label (see the
  popover-anchor gotcha). Dev hook: HS_OPEN_AUDIO=1.
- 2026-08-31 — Wi-Fi selector on network click (`src/bar/network_panel.{hpp,cpp}`,
  Noctalia's Wi-Fi panel in the shared theme): header with wifi-radio switch
  (WirelessEnabled via DBus property Set; styled dark trough + primary knob),
  Connected card (mPrimary bg, Disconnect), scrolled Available list (signal
  glyph, lock + security, Connect), inline password card for new secured
  networks. Transient "Scanning…"/"Connecting…" text lives at the right of
  the section header row so the panel height never jumps. While the radio is
  off the whole list swaps for a Noctalia-style disabled card (big wifi-off
  glyph, "Wi-Fi is disabled"); the swap happens on the enabled-state edge in
  update_state, since scans don't run while disabled. **Popover-resize
  gotcha**: on Hyprland a mapped popover surface never resizes — content
  shrinking leaves dead space, growing gets clipped. Any panel whose content
  changes while open must therefore have a FIXED size (this one is 330x440,
  like Noctalia's fixed 440x460 panel): the list scroller vexpands into it
  and the disabled card centers in it. Wifi
  management shells out to **nmcli** via Gio::Subprocess (async, LC-safe terse
  parsing with "\:"-escape handling; strongest BSS per SSID; saved profiles
  from `connection show` decide `connection up id` vs `device wifi connect
  [password]`; nmcli can exit 0 on failure so the output is checked for
  "Error") — NM's raw DBus connect flow (settings matching, secrets agent)
  isn't worth reimplementing. The Connected card reads NM's DBus primary
  connection (instant) and falls back to the scan list, which refreshes only
  after each action's rescan. Bar icon: `ethernet_connected()` (any ACTIVATED
  802-3-ethernet among ActiveConnections) wins over wifi, like Noctalia; the
  ethernet tab/traffic stats of Noctalia's panel were NOT ported. Dev hook:
  HS_OPEN_NETWORK=1.
- 2026-09-01 — Bluetooth module + panel (Noctalia's bluetooth bar widget/panel):
  service `Bluez` consumes org.bluez via `Gio::DBus::ObjectManagerClient`
  (object-added/removed + interface-properties-changed all funnel into one
  rebuild()+emit; Battery1.Percentage read off the same objects). Panel:
  header (icon/title/power switch — Noctalia's settings, close and
  auto-connect buttons dropped per user), disabled card, Connected list (mPrimary
  card, Disconnect), Paired list (Connect = Trusted=true then Device1.Connect,
  Noctalia's connectDeviceWithTrust) and — unlike Noctalia, which pairs in its
  settings window (user request) — an Available-devices list fed by
  Start/StopDiscovery while the popover is open (module's signal_closed stops
  it; unnamed/MAC-named devices hidden via Noctalia's normalize filter).
  Pairing shells out to `bluetoothctl --timeout 30 pair` (it registers its own
  BlueZ agent internally — same trick as Noctalia's pair script, and the nmcli
  rationale: no Agent1 implementation; PIN-pairing keyboards won't pair),
  pausing discovery meanwhile, then trust+connect over DBus; errors surface in
  a status line under the list. Enabling powers on via `rfkill unblock
  bluetooth` then Adapter1.Powered=true (a persisted rfkill block makes bare
  Powered=true a no-op, like Noctalia found). Auto-connect
  (`bar.bluetooth.auto_connect`, default off): on the power-on edge (startup
  included) connect every paired device, staggered 500ms, 1.5s after the edge
  (Noctalia's timings; its per-device opt-in checkboxes were NOT ported —
  ours is global), toggled from the settings app's Bluetooth subpage (a panel
  header button existed briefly; removed 2026-09-01 per user — the shell
  never writes config.json). Fixed 330x400 panel
  (popover-resize gotcha); device glyphs ported from BluetoothUtils.deviceIcon
  keyword tests. Bar icon: off/on/connected tabler glyphs, tooltip = first
  connected device (+ N). Dev hook: HS_OPEN_BLUETOOTH=1. Settings cog rows now
  attach by module key (module_index()), not hard-coded kModules indices.
- 2026-09-01 — The wifi/bluetooth panel switches are a 1:1 NToggle port
  (superseding the network panel's original "dark trough + primary knob"
  styling): 36x22 mOutline-bordered pill — mSurface track/mPrimary knob off,
  mPrimary track/mOnPrimary knob on, off-state knob ringed 2px mSurface (the
  on-state ring was dropped 2026-09-01 per user). GTK box-model
  note: borders sit OUTSIDE min-width/height, so the CSS says 34x20 + 1px
  border (track) and 14px + 2px ring (18px knob), margin 1px.
- 2026-09-01 — Notification module + panel (phase 3 pulled forward; Noctalia's
  NotificationHistory widget/panel with user changes: no close button, no
  All/Today/Yesterday/Earlier tabs, header = DND NToggle then a "Clear All"
  pill (trash glyph + text), no settings cog). Service `NotificationService`
  owns org.freedesktop.Notifications with default own_name flags, so while
  another daemon runs (the user's Noctalia today) we sit in the bus queue and
  take over automatically when it exits — the module degrades to an empty
  history meanwhile, tooltip says so. History: 100 entries (Noctalia's
  maxHistory) in ~/.cache/hypr-shell/notifications.json (+ last_seen_ts for
  the unread badge), initial read synchronous like Config's, saves debounced
  200ms + async; `image-data` hints are decoded via GdkPixbuf, downscaled to
  ≤96px and cached as PNGs in ~/.cache/hypr-shell/notifications/ (deleted with
  their entry; sender-provided paths never deleted). Summary/body are
  sanitized to Pango markup (b/i/u kept, br→\n, rest dropped/escaped;
  unbalanced markup falls back to plain via pango_parse_markup). App names run
  through a port of Noctalia's getAppName. DND is runtime-only (the shell
  never writes config.json) and currently gates nothing — popups don't exist
  yet; history records during DND like Noctalia. Actions: invoking emits
  ActionInvoked(original_id, key) — we ARE the daemon (original_id is seeded
  past loaded history so ids stay unique across restarts); card click invokes
  "default" else focuses the sender via a fuzzy class match over j/clients
  (new `Hyprland::focus_window(address)`, `hl.dsp.focus({ window =
  "address:0x…" })` — verified against 0.56). Panel: fixed 380x480
  (popover-resize gotcha), rebuild-on-change gated to open, scroll position
  restored across rebuilds, relative times re-rendered every 30s while open;
  the expand chevron appears only when a label is actually ellipsized,
  measured from a tick callback because pango layouts are only valid after
  allocation. Icon fallback chain: image file (Gtk::Picture, COVER, clipped
  by Gtk::Overflow::HIDDEN) > themed icon > desktop-entry icon > bell glyph.
  Bar module: bell/bell-off, unread dot = entries newer than last_seen (set
  on panel open), right-click toggles DND; `bar.notifications` =
  show_unread_badge / hide_when_zero / hide_when_zero_unread (Noctalia's
  widget metadata defaults) with a settings subpage. Dev hook:
  HS_OPEN_NOTIFICATIONS=1.
- 2026-09-01 — Full NotificationService parity + toast popups + settings
  sidebar (completing phase 3, pulling the sidebar forward from phase 6).
  Config gained a top-level `notifications` object (Noctalia's
  Settings.data.notifications, exposed as `Config::Notifications`): enabled,
  density default/compact, location (6 anchors), overlay_layer,
  background_opacity, respect_expire_timeout, {low,normal,critical}
  _urgency_duration (1–30s; defaults 3/8/15), clear_dismissed,
  save_to_history.{low,normal,critical}, sounds.{enabled,volume,
  separate_sounds,*_sound_file,excluded_apps}, rules[] ({pattern, action
  block/hide/mute} — /regex/, *glob* (icase) or substring (icase) over
  "app summary body", first match wins; port of NotificationRulesService,
  stored in config.json instead of Noctalia's separate rules file).
  notifications.enabled=false unowns the bus name (registration id kept —
  re-registering the object on the same connection would error). Popups
  (`NotificationPopups`, layer window "hypr-shell-notifications", overlay or
  top layer, exclusive zone 0 so the bar strip is respected): max 5, newest
  on top, per-urgency countdown on a 50ms service timer (progress signal →
  DrawingArea bars shrinking from both ends, mError/mOnSurface/mPrimary per
  urgency), hover pauses, click = default action else focus sender, right
  click/close button dismiss (+ clear_dismissed deletes the history entry),
  replaces_id updates a live popup in place (countdown kept, no sound),
  duplicate content replaces the older popup, DND suppresses popups only.
  **Layer-window width gotcha**: a wrapping GTK label reports the full text
  as natural width and the layer surface grows to natural size — popup labels
  get max_width_chars(1)+hexpand so the stack's width request (440/320
  compact) wins. Sounds via `paplay` (pipewire-pulse; Gio spawn, silent) —
  default asset Noctalia's notification-generic.wav installed to
  <datadir>/hypr-shell/sounds; excluded_apps compares prettified names,
  100ms rate limit, muted Pulse output skips. invoke_action searches live
  popups too (transient notifications never reach history).
  hypr-shell-settings became an AdwNavigationSplitView: sidebar "Bar" (the
  old page + module subpages) then "Notifications" (groups mirroring
  Noctalia's tab: general/duration/history/sound/rules; rules edited in an
  AdwAlertDialog, sound files picked via GtkFileDialog into entry rows).
  NOT ported (deps missing): monitors multi-select (per-monitor bars are
  phase 1 leftovers), enableMarkdown (Pango ≠ markdown), toast subtab
  (media/keyboard/battery toasts), sounds file *picker filters*, popup
  spring animations + swipe-dismiss.
  Dev hook: HS_SETTINGS_PAGE=notifications_page opens the sidebar page.
- 2026-09-01 — DND moved out of the history panel header (user request): it
  lives in the settings app (`notifications.do_not_disturb`, after Density
  like Noctalia's tab) plus the bell's right click. Since the shell never
  writes config.json, the service adopts the config value only on
  value-change **edges** (config_dnd_ tracking) — a runtime right-click
  toggle survives unrelated config reloads instead of being clobbered on
  every save from the settings app.
- 2026-08-31 — Config's initial load is a synchronous read (tiny local file, needed
  before the first frame so the bar doesn't flash defaults) — accepted deviation from
  the async-I/O rule; reloads go through Gio::FileMonitor. Invalid JSON warns and falls
  back to defaults rather than crashing or keeping stale state. Bottom-positioned bar
  gets a `bottom` CSS class on the window so the theme can flip the hairline border.
