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
package.sh                     git-archives HEAD to dist/hypr-shell.tar.gz (gitignored) for the
                               dotfiles repo's applications/hypr-shell/; commit first
data/style.css                 default theme entry — @imports data/css/*, declares the
                               @define-color palette tokens + the text-font rule (GResource)
data/css/*.css                 per-area theme files: bar, calendar, panels,
                               notifications, launcher, app_menu, session, idle,
                               lock, osd, wallpaper, control_center
                               (GTK resolves the imports inside the resource bundle)
data/hypr-shell.gresource.xml
data/hypr-shell-settings.desktop.in   (Exec gets the absolute bindir at build time)
data/avatar-fallback.svg       default profile picture (GResource) when ~/.face is missing
data/fonts/                    noctalia-tabler-icons.ttf (installed to
                               ~/.local/share/fonts/hypr-shell, MIT license alongside)
src/main.cpp                   App (Gtk::Application), CSS loading + user-CSS hot reload
src/bar/bar.{hpp,cpp}          Bar window (layer-shell setup)
src/bar/bar_popover.hpp        place_bar_popover(): module popover side + gap from the bar
src/bar/modules/*.{hpp,cpp}    one widget per bar module (launcher, app_menu,
                               workspaces, taskbar, active_window, clock, network,
                               volume, battery, bluetooth, control_center,
                               clipboard, notifications, session)
src/services/config.{hpp,cpp}           config.json load + hot reload (Gio::FileMonitor)
src/services/palette.hpp                theme palette derivation (accent + dark/light → the m*
                                        tokens), header-only, shared with the settings app
src/services/theme.{hpp,cpp}            Theme singleton: derived palette / font from `ui.*`;
                                        cairo/GSK drawing code reads colours here
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
src/bar/busy_indicator.{hpp,cpp}        BusyIndicator: Noctalia's NBusyIndicator spinner (cairo arc,
                                        tick callback only while mapped; colour from CSS `color`)
src/services/mpris.{hpp,cpp}            MPRIS players on the session bus (metadata, position, controls)
src/services/system_stats.{hpp,cpp}     CPU / temperature / memory / disk sampling while a card is open
src/bar/control_center_panel.{hpp,cpp}  control center cards: audio, brightness, media, system monitor
src/services/notifications.{hpp,cpp}    org.freedesktop.Notifications daemon + history
src/bar/notification_panel.{hpp,cpp}    notification history panel (bell click)
src/bar/notification_popup.{hpp,cpp}    toast popup stack (layer window)
src/bar/notification_ui.{hpp,cpp}       shared icon/relative-time helpers
src/services/lock_keys.{hpp,cpp}        Caps/Num/Scroll Lock via /sys/class/leds polling (200ms)
src/services/osd.{hpp,cpp}              OSD trigger logic (Pulse/Brightness/LockKeys diffs + suppression)
src/services/battery_alerts.{hpp,cpp}   low (20%) / critical (5%) battery notifications via the daemon
src/bar/osd_window.{hpp,cpp}            on-screen display layer window (volume/mic/brightness/lock keys)
src/bar/launcher_window.{hpp,cpp}       app launcher overlay (fullscreen layer window)
src/bar/clipboard_window.{hpp,cpp}      clipboard history overlay (launcher design, cliphist entries)
src/services/clipboard.{hpp,cpp}        cliphist history: wl-paste watchers, list/decode/copy/paste/delete
src/bar/app_menu_panel.{hpp,cpp}        app menu popover (search + settings/session buttons + app grid)
src/services/apps.{hpp,cpp}             desktop-entry index + fuzzy match + pinned apps +
                                        window-class → entry lookup (Noctalia's ThemeIcons)
src/services/math_eval.{hpp,cpp}        launcher calculator (AdvancedMath.js port)
src/services/session_actions.hpp        session action table (key/label/glyph/command/defaults),
                                        shared with the settings app
src/services/session.{hpp,cpp}          enabled session actions (session.items) + run,
                                        detached spawn / open-settings helpers
src/bar/session_menu.{hpp,cpp}          dropdown session list (bar module popover + app menu button)
src/bar/session_window.{hpp,cpp}        fullscreen session menu (overlay layer window)
src/services/app_menu_icons.hpp         app menu icon presets, shared with the settings app
src/services/idle.{hpp,cpp}             idle daemon: ext-idle-notify-v1 stages (screen off /
                                        lock / suspend) with fade grace period; holds an
                                        idle-inhibit-v1 inhibitor while a controller is in use;
                                        protocol glue is generated by wayland-scanner in meson.build
src/services/gamepad.{hpp,cpp}          game controller activity from evdev (/dev/input/event*,
                                        joystick nodes via systemd's uaccess ACL, hotplug)
src/bar/idle_fade.{hpp,cpp}             fade-to-black overlay shown before each idle action
src/services/pam_auth.{hpp,cpp}         PAM authentication on a worker thread (Glib::Dispatcher)
src/bar/lock_screen.{hpp,cpp}           session lock (gtk4-session-lock) + LockContext state machine,
                                        logind Lock signal; one LockSurface per monitor
src/bar/lock_surface.{hpp,cpp}          per-monitor lock UI (cover/login stages, pills, session menu)
src/bar/lock_background.{hpp,cpp}       blurred cover-scaled wallpaper widget (GSK blur, cached)
src/services/user_info.hpp              display name (GECOS, else login) + avatar path (~/.face), shared
src/bar/avatar.{hpp,cpp}                load_avatar_texture(): square avatar, bundled fallback picture
src/services/wallpaper.{hpp,cpp}        wallpaper folder scan, current image + slideshow (state in
                                        ~/.cache/hypr-shell/wallpaper.json)
src/services/wallpaper_files.hpp        image extension list, shared with the settings app
src/services/night_light.{hpp,cpp}      hyprsunset runner: sunrise/sunset schedule, forced mode,
                                        stale-instance kill, crash restart, resume re-apply
src/bar/wallpaper_window.{hpp,cpp}      per-monitor background layer window: fill modes + GSK
                                        mask-node transitions (fade/wipe/disc/stripes)
src/settings/main.cpp                   hypr-shell-settings (libadwaita C API, instant apply)
src/settings/search.{hpp,cpp}           settings search: sidebar toggle + search bar, widget-tree
                                        index of every preferences row, navigate/scroll/highlight
src/settings/about_page.{hpp,cpp}       "About" page: hardware + software facts (sysfs, /proc,
                                        os-release, hyprctl version), GNOME Settings layout
src/settings/hotspot_page.{hpp,cpp}     "Hotspot" page: NetworkManager AP mode over nmcli (profile
                                        "Hotspot"; name/security/password/band/hidden, virtual
                                        AP interface for Wi-Fi + hotspot, autostart, live status)
src/settings/vpn_page.{hpp,cpp}         "VPN" page: NetworkManager vpn/wireguard profiles over
                                        nmcli (connect switches, import .conf/.ovpn, delete)
src/settings/command.{hpp,cpp}          run_command(): async GSubprocess helper + string utils
                                        shared by the hotspot and VPN pages
docs/                                   long-form developer docs (start at docs/README.md)
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
Idle daemon testing: `HS_IDLE_DRY_RUN=1` logs actions instead of blanking/
locking/suspending; `HS_IDLE_SIMULATE=screen_off|lock|suspend` (+
`HS_IDLE_SIMULATE_RESUME=<ms>`) drives a stage without the seat being idle;
`HS_IDLE_SIMULATE_GAMEPAD=<ms>` reports controller activity (the idle
inhibitor hold) without a pad — with `G_MESSAGES_DEBUG=all` and the seat
idle for a second, "heartbeat resumed" at that moment proves the compositor
honoured it.
Never force real idleness while another idle daemon (Noctalia's, hypridle)
runs — it would act for real.
OSD testing: `HS_OSD_SHOW=volume|input|brightness|lock` shows that OSD 1.2s
after startup; real triggers that restore themselves:
`wpctl set-volume @DEFAULT_AUDIO_SINK@ 1%+` (then `1%-`),
`@DEFAULT_AUDIO_SOURCE@` for the microphone, `brightnessctl s +1%` / `1%-`.
Lock screen testing: `HS_LOCK_PREVIEW=1` shows the lock UI as a plain overlay
window (Escape on the cover closes it; `=2` also opens the session menu),
`HS_LOCK_AVATAR=<path>` overrides `~/.face`, `HS_PAM_SERVICE=<name>` the PAM
service. Real lock: `hypr-shell --lock` — keep a TTY open the first time.
Night light testing: `HS_NIGHT_LIGHT_DRY_RUN=1` logs the hyprsunset command
instead of spawning it (and skips the stale-process kill). Only one
hyprsunset can hold Hyprland's CTM: two night light daemons fight (each
restarts 2s after being killed), so Noctalia's nightLight.enabled was set to
false in ~/.config/noctalia/settings.json on 2026-09-04 (backup:
settings.json.pre-hypr-shell-nightlight).
Panel dev hooks: `HS_OPEN_<PANEL>=1` pops a panel 800ms after startup; a
value above 1 is the delay in ms (`=3000`) — the main loop can stall for ~1s during startup and a popup issued
meanwhile is dismissed at once (the HS_OPEN_AUDIO freeze noted in memory).
`HS_POPOVER_DEBUG=1` logs each module popover's anchor / natural size /
alignment and its final surface position (see `bar/bar_popover.hpp`), which
verifies placement even when a screenshot is impossible (locked screen).
Settings app testing: `HS_SETTINGS_PAGE=<tag>` opens a page or module
subpage; `HS_SETTINGS_SEARCH=<query>` opens the sidebar search pre-filled and
`HS_SETTINGS_SEARCH_OPEN=1` additionally activates the first result 1.5s after
startup (navigates, scrolls to and flashes the row) — screenshots need no
scripted key presses. `HS_SETTINGS_TIMING=1` prints milliseconds since
main() at each startup phase (build / populate / present / realize / first
frames) and at every config.json write — a launch must print no write.
`HS_HOTSPOT_SAVE=1` writes the Hotspot page's shown
settings to the NM profile 2s after startup (creates "Hotspot" from the
defaults, never activates it). **Never activate the hotspot from the tool
shell on the single adapter: it disconnects the user's Wi-Fi.**
Clipboard testing: `HS_OPEN_CLIPBOARD=3000` opens the history window after 3s
(`hypr-shell --clipboard` against a running instance also works). It needs
`clipboard.enabled` in config.json; Noctalia's own `wl-paste … cliphist store`
watchers are detected by pgrep and ours are then not started.
Wallpaper testing: the desktop is usually covered, so `HS_WALLPAPER_DUMP=<dir>`
renders the first monitor's frames offscreen (frame-25/50/75.png as the first
transition passes those marks, frame-final.png after 4s) and
`HS_WALLPAPER_TRANSITION=fade|wipe|disc|stripes` forces the type; change
`wallpaper.current` in config.json to trigger an image-to-image transition.

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
- Layer-shell gotchas: **never parent a dialog (file chooser etc.) to a
  layer-shell window** — `xdg_toplevel.set_parent` on a layer surface is a
  protocol error that kills the shell; pass a null parent (the portal shows
  it). Call `gtk_layer_init_for_window()` **before** the window
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
      2026-09-01: bluetooth (BlueZ) icon + click panel landed; 2026-09-04: VPN
      pill + panel (Noctalia's VPN widget over nmcli) landed, then moved to a
      settings page and removed from the bar 2026-09-05; 2026-09-04:
      taskbar (Noctalia's Taskbar widget — running + pinned apps) landed;
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
      rate) landed; 2026-09-03: the OSD landed — output volume, microphone,
      brightness and Caps/Num/Scroll Lock, Noctalia's OSD.qml with an
      "On-screen display" settings page (position + enable); 2026-09-04: the
      control center landed — bar button + panel with Noctalia's audio,
      brightness, media and system monitor cards, each toggleable)*
- [ ] **Phase 5 — Lock & idle**: lock screen via ext-session-lock
      (gtk4-layer-shell's session-lock API) + PAM auth; idle service via
      ext-idle-notify-v1 (dim → lock → dpms off) with inhibitor support.
      *(pulled forward 2026-09-03: the idle service landed — Noctalia's
      IdleService: screen off / lock / suspend stages with a fade-to-black
      grace period, resume handling, per-stage and custom commands; the
      compositor honors inhibitors for v1 notifications. Settings expose
      only the three timeouts, per user. 2026-09-03: the lock screen landed —
      ext-session-lock via gtk4-session-lock, PAM auth, Noctalia's two-stage
      lock UI; settings expose only background image + blur, per user.
      Remaining: inhibitor DBus API (org.freedesktop.ScreenSaver))*
- [ ] **Phase 6 — Settings app**: `hypr-shell-settings` (GTK4 + libadwaita,
      GNOME-Settings-style sidebar + search) editing the same config.json; the shell
      applies changes live.
      *(pulled forward 2026-08-31: the executable exists — single bar page with
      position/height/module toggles, instant apply; the sidebar + search layout and
      full option coverage come with this phase)*
- [ ] **Phase 7 — Noctalia-parity extras** (as desired): app launcher, wallpaper
      handling, screenshot helpers, systemd user units.
      *(pulled forward 2026-09-02: the app launcher landed — list view only,
      applications + calculator always on, settings/session/web search behind
      `launcher.*` toggles, bar search-icon module, `hypr-shell --launcher`
      for keybinds, pin-to-store; grid view, categories, clipboard/emoji/
      windows providers, ">" command mode and usage tracking were NOT ported;
      2026-09-03: the `app_menu` bar module landed — Noctalia's Launcher bar
      widget (rocket / preset / distro / custom icon, icon+text, text) opening
      a grid app menu popover with search, settings and session buttons;
      `hypr-shell --app-menu` toggles it for keybinds; 2026-09-03: the
      `session` bar module + shared session menu (dropdown or fullscreen
      large buttons, single row / grid, per-action visibility) with its own
      settings page and `hypr-shell --session`; 2026-09-04: wallpaper
      handling landed — the shell draws the wallpaper itself (per-monitor
      background layer, Noctalia's fill modes + fade/wipe/disc/stripes
      transitions, slideshow) with a "Wallpaper" settings page holding the
      folder picker and image grid; no separate selector window, per user;
      2026-09-04: night light landed — Noctalia's NightLightService over
      hyprsunset with a "Night light" settings page, minus the day
      temperature option, per user; 2026-09-05: clipboard history landed —
      Noctalia's cliphist-backed clipboard provider as its own overlay
      window with a "Clipboard" settings page, bar module and
      `hypr-shell --clipboard`)*

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
  **Superseded 2026-09-05**: the icon is bundled — `data/icons/
  dev.hyprshell.Settings{,-symbolic}.svg` are gnome-control-center 50.4's
  org.gnome.Settings icons renamed (GPL-2.0-or-later, `data/icons/NOTICE.txt`),
  installed to `<datadir>/icons/hicolor/{scalable,symbolic}/apps`, and the
  desktop entry says `Icon=dev.hyprshell.Settings`. GTK finds them under
  ~/.local/share/icons/hicolor without an icon cache.
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
  mPrimary track/mOnPrimary knob on. The knob is a solid 14px circle with a
  3px margin (2026-09-05, user request: "ring colour = knob colour"), which
  looks like NToggle's 18px knob + 2px mSurface ring — that ring was dropped
  for the on state 2026-09-01, restored briefly 2026-09-05, then replaced by
  the plain margin. GTK box-model
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
- 2026-09-02 — App launcher (phase 7 pulled forward; Noctalia's overlay-layer
  launcher in **list view only**, user request). `LauncherWindow` is a
  fullscreen overlay layer window ("hypr-shell-launcher", exclusive keyboard,
  exclusive zone -1): mSurface-at-0.2 dimmer backdrop (click closes), centered
  panel max(25% of screen, 552) x max(50%, 600) sized from the window's first
  allocation (the layer surface spans the output, so its size IS the screen's).
  Toggling: the app exposes a GApplication **action "launcher"**; the bar's
  search-icon module (user's pick over Noctalia's rocket; `launcher` module
  key, default left section) activates it, and `hypr-shell --launcher`
  forwards it from a second process via handle-local-options →
  register_application() → activate_action() → exit — that's the keybind hook
  (`bind = SUPER, SPACE, exec, hypr-shell --launcher`), documented on the
  settings page. Providers, merged and sorted by score like LauncherCore
  (apps 0..1, session −1.., settings −2.., web −3): applications (Gio::AppInfo
  + AppInfoMonitor reload, Noctalia's id+exec dedupe, fuzzy over
  name/description/exec basename, limit 20, alphabetical browse on empty query
  gated by `launcher.show_all_apps`); calculator (`math_eval` port of
  AdvancedMath.js — recursive descent, locale-proof from_chars/g_ascii_formatd,
  same isMathExpression gate and formatting; Enter copies via wl-copy);
  settings search (static index of hypr-shell-settings rows, opens the app via
  its HS_SETTINGS_PAGE hook with `env` in the argv); session search (lock =
  loginctl lock-session until phase 5, suspend/reboot/poweroff via
  systemctl||loginctl, logout = hl.dsp.exit(), Noctalia keywords); web search
  (Google in the default browser, not in Noctalia — default OFF). Config:
  top-level `launcher` object (enable_settings_search / enable_session_search
  / enable_web_search / show_result_count / show_all_apps), settings app grew
  a Launcher sidebar page (sidebar is now Bar/Launcher/Notifications).
  Keyboard: Esc/Enter/Up/Down-wrapped/Tab/Shift-Tab/Home/End/PgUp/PgDn on a
  CAPTURE-phase key controller so the entry keeps focus; hover only selects
  after the mouse moved ≥5px since opening (Noctalia's ignoreMouseHover).
  **Pinning**: the selected row of an application showed a pin/unpin button
  (removed 2026-09-04 per user — pinning moved to the app menu's right-click
  menu); pins are stored in ~/.cache/hypr-shell/pinned_apps.json (shell never
  writes config.json) and are consumed by the taskbar module. NOT ported: grid view + view toggle, categories, density,
  clipboard/emoji/windows/command providers, ">" command mode, usage tracking
  (sortByMostUsed), custom launch prefix/terminal override, entrance
  animation. Gotcha: a GTK entry draws its own blue focus ring — needs
  `outline: none` alongside the themed border. Dev hooks: HS_OPEN_LAUNCHER=1;
  HS_LAUNCHER_QUERY=<text> pre-fills the search on the first open only
  (the search itself never remembers the previous query);
  HS_SETTINGS_PAGE=launcher_page opens the settings page.
- 2026-09-02 — `launcher.show_all_apps = false` now means **Spotlight mode**
  (user request): the panel is content-sized — just the input box when the
  query is empty, growing with the results up to a screen-derived cap
  (ScrolledWindow propagate_natural_height; the fullscreen layer surface never
  resizes, only the centered child does, so the popover-resize gotcha doesn't
  apply). The input is **pinned**: valign START + a top margin equal to the
  fixed panel's top edge — both modes put the search box in the same place —
  so growth only extends downward, matching the fixed panel's footprint when
  fully grown. Growth/shrink is **animated** (260ms ease-out quart tick
  callback driving the scroller's min+max content height together — min
  forces the shrink direction, max the cap).
  show_all_apps = true keeps the fixed centered Noctalia-style panel. The
  list also gained a persistent scrollbar (set_overlay_scrolling(false) —
  the default overlay one only appears while scrolling), slim-thumb styled
  in launcher.css.
- 2026-09-03 — App menu module (`bar/modules/app_menu`, `bar/app_menu_panel`,
  module key `app_menu`, default left section, enabled by default like every
  new module — disable the search-icon `launcher` module if only one is
  wanted). Bar button = Noctalia's Launcher widget: `bar.app_menu.display`
  icon/icon_text/text, `text` (default "Apps"), `icon` = a preset key from
  `services/app_menu_icons.hpp` (curated tabler glyphs, rocket default —
  Noctalia's full icon picker was NOT ported), `"distro"` (the LOGO= icon
  name from /etc/os-release, resolved by the icon theme — Noctalia's
  useDistroLogo without its path probing) or `"custom"` + `custom_icon`
  (themed icon name or image path via g_icon_new_for_string; Noctalia's
  customIconPath; no colorization shader). The header is shared with
  hypr-shell-settings so the dropdown and renderer never drift. Panel: a bar
  popover (fixed 480 wide; the grid area's height is measured per open —
  `grid_.measure()` works before mapping — for up to 5 rows and then held in a
  Gtk::Stack so the "no matches" swap can't shrink it: popover-resize gotcha)
  — Noctalia's launcher grid view rather than its overlay: search entry, then optional round buttons
  (`show_settings_button` → hypr-shell-settings, `show_session_button` →
  a **nested Gtk::Popover dropdown** via Gtk::MenuButton listing lock/
  suspend/reboot/logout/shutdown; nested popovers work fine on Hyprland),
  then a Gtk::Grid of **fixed-size tiles** (user request: a two-result row
  must not stretch to fill the panel): width = (480 − gaps) / `columns`
  (3..8, default 5) — the scrollbar's measured width is subtracted only when
  the WHOLE app list overflows the 5-row area (filtering never adds rows),
  and then the scrollbar policy is ALWAYS for that open so tiles never
  shift; otherwise no scrollbar and no right gap (user request); height measured from
  a probe tile holding one or two lines of text (`multiline_labels`, default
  off = Noctalia's single line; on = wrap to 2 lines, ellipsized), attached
  to the grid, measured, removed — CSS padding/font apply to in-tree widgets
  even before mapping. Icon size from a per-column table; tile labels use
  max_width_chars(1)+hexpand so long names never widen a tile; names are
  normal weight (user request). Empty query lists every app alphabetically;
  otherwise fuzzy_score over name/description/exec like the launcher,
  unlimited; more than 5 rows scroll with the launcher's thin scrollbar. Keys on a CAPTURE controller: Left/Right/Tab step,
  Up/Down move rows (wrapping), Home/End/PgUp/PgDn, Enter launches, Esc
  closes — ignored while the session dropdown is open so GTK navigates it.
  Actions run from an idle callback after the popover closes. The session
  table moved to `services/session.{hpp,cpp}` (with `spawn_detached` /
  `open_settings`) so the launcher's session provider and the dropdown share
  one list. Settings: "App menu" module row + cog subpage (display, label,
  icon dropdown + custom entry, columns, search bar (`show_search` — off
  hides the entry, right-aligns the buttons and makes the panel itself the
  focus target so arrows/Enter keep working), two-line names, the two
  buttons). Keybinds: `hypr-shell --app-menu` (GApplication action "app-menu", same
  handle-local-options forwarding as `--launcher`) toggles the popover in
  the running instance — an auto-hidden bar peeks first; documented on the
  subpage as `bindr = SUPER, SUPER_L, exec, hypr-shell --app-menu` (release
  bind = the bare Super key). Popup grabs work without any prior input on
  the bar (the dev hooks prove it), so a keybind-opened popover gets
  keyboard focus. Dev hook: HS_OPEN_APP_MENU=1 (=2 also pops the session
  dropdown). Not ported: Noctalia's list/grid view toggle (grid
  only, per user), categories, pin actions on tiles, icon colorization.
- 2026-09-03 — Session menu (Noctalia's SessionMenu widget + panel). One
  shared backend: `services/session_actions.hpp` is a header-only table
  (key, label, description, glyph, keywords, `sh -c` command, destructive,
  default_on) of Noctalia's 8 actions — lock, suspend, hibernate, reboot,
  logout, shutdown, reboot to UEFI (`systemctl reboot --firmware-setup`),
  userspace reboot (`systemctl soft-reboot`) — consumed by the shell AND
  hypr-shell-settings so the settings page lists exactly the menu entries.
  Defaults deviate from Noctalia (which enables everything): hibernate /
  UEFI / soft-reboot start OFF, matching the user's own Noctalia config and
  the five entries the menus had before; `session.items.<key>` toggles them
  and applies to the bar module, the app menu's power button AND the
  launcher's session search. Top-level `session` config: `mode` dropdown
  (default) / fullscreen (Noctalia's largeButtonsStyle), `fullscreen_layout`
  single_row (default) / grid. Dropdown = `SessionMenuList` (self-rebuilding
  glyph+label buttons, shutdown tinted mError) in a popover — the bar
  `session` module (power glyph in mError like the user's Noctalia widget;
  default right section after the clock) anchors it to its icon label; the
  app menu's power button became a plain Gtk::Button + parented Popover
  (was a MenuButton) so it can branch on the mode at click time. Fullscreen
  = `SessionWindow`, an overlay layer window like the launcher (exclusive
  keyboard, 0.6-alpha dimmer, click-outside closes) with 200px buttons in
  one row or Noctalia's grid (min(3, ceil(sqrt(n))) columns), nothing
  selected until keyboard/mouse (Noctalia), navigateGrid port (row wrap,
  column clamp), digits 1-9 pick an entry (Noctalia's default keybinds),
  Space/Enter activate. The App owns it and exposes action "session" /
  `hypr-shell --session` which dispatches on the mode: fullscreen → toggle
  the window, dropdown → the bar module's popover (bar peeks if
  auto-hidden). Settings: "Session menu" sidebar page (Bar / Launcher /
  Session menu / Notifications; HS_SETTINGS_PAGE=session_page) with Style,
  Fullscreen layout (hidden unless fullscreen) and one switch per action;
  the Session bar module row has no cog (everything is on that page). Dev
  hook: HS_OPEN_SESSION=1 activates the action after startup. NOT ported:
  Noctalia's confirmation countdown, per-entry custom commands/keybinds,
  header/close button, the panel-position setting, hover scale animation.
- 2026-09-03 — Idle daemon (`services/idle`, Noctalia's IdleService ported
  1:1; `bar/idle_fade` = its IdleFadeOverlay). Protocol: ext-idle-notify-v1
  spoken directly — the wl_display/wl_seat come from GDK
  (`gdk_wayland_display_get_wl_display`, `gdk_wayland_seat_get_wl_seat`),
  the notifier is bound from our own registry after one
  `wl_display_roundtrip` (gtk4-layer-shell does the same), and the C glue is
  generated by `wayland-scanner` in meson.build from wayland-protocols' XML
  (new deps: wayland, wayland-protocols). v1 notifications are used on
  purpose — the compositor drops idle events while an idle inhibitor is
  active. One notification per stage + a 1s heartbeat (idle seconds) + one
  per `idle.custom_commands` entry; the protocol has no update-timeout
  request, so a changed timeout recreates its notification. State machine
  copied from Noctalia: idled → fade for `fade_duration` (default 5s) →
  execute → 500ms cleanup → run queued stages; `resumed` on any stage cancels
  the fade and restores DPMS if the screen was turned off; stage resume
  commands fire from the heartbeat's resume. Actions: screen off =
  `hl.dsp.dpms({ action = "off" })` via `Hyprland::set_dpms()` (Noctalia's
  exact Lua — **Hyprland accepts any dpms argument table without error, and
  an empty/wrong table turned the user's screen OFF during probing; never
  call dpms blind**), lock = `loginctl lock-session` (phase-5 stand-in),
  suspend = `systemctl suspend`, optionally lock first with a 1s delay
  (`lock_before_suspend`, Noctalia's general.lockOnSuspend). Config
  `idle.*`: enabled (default ON — Noctalia defaults off, but the settings
  page has no master switch; 0 disables a stage), screen_off_timeout /
  lock_timeout / suspend_timeout (Noctalia's 600/660/1800), fade_duration,
  lock_before_suspend, *_command, resume_*_command, custom_commands[]. The
  fade overlay is a single overlay layer window (keyboard NONE, whole
  output), opacity eased in from 0.01 (a fully transparent window never
  commits a buffer) — it swallows pointer input for those seconds like
  Noctalia's; per-monitor overlays wait for phase 1. Settings: "Idle"
  sidebar page (Bar / Launcher / Session menu / Idle / Notifications;
  HS_SETTINGS_PAGE=idle_page) with exactly three spin rows (Turn off screen /
  Lock screen / Suspend), per user. Testing: HS_IDLE_DRY_RUN + HS_IDLE_SIMULATE
  (see dev loop) — the seat must never be forced idle while Noctalia runs.
- 2026-09-03 — Lock screen (Noctalia's LockScreen/LockContext/LockScreenPanel/
  LockScreenBackground ported 1:1, minus what's noted). Protocol: ext-session-
  lock-v1 through gtk4-layer-shell's `GtkSessionLockInstance` (`bar/lock_screen`):
  lock → `::monitor` per output → a `LockSurface` (Gtk::Window, `make_managed`
  — the library destroys them on unlock; pointers dropped from
  `signal_destroy`) assigned with `assign_window_to_monitor`, then presented.
  Auth: `services/pam_auth` — pam_authenticate + pam_acct_mgmt on a **worker
  thread** (the project's one thread; PAM is synchronous and pam_unix sleeps
  ~2s on failure), conversation prompts posted to the main loop via
  Glib::Dispatcher, a prompt is answered from the password given to `start()`
  when non-empty, else waits on a condvar for `respond()` (Quickshell
  PamContext semantics). PAM service detected like Noctalia (login →
  system-auth → common-auth; `HS_PAM_SERVICE` override) — no
  /etc/pam.d/hypr-shell needed, `login` works unprivileged via unix_chkpwd.
  All lock requests funnel through `request_lock()` in `services/session`
  (idle daemon, session actions — the "lock" action no longer shells out to
  loginctl —, `hypr-shell --lock` / GAction "lock", and logind's Session
  `Lock` signal so `loginctl lock-session` still works; session resolved by
  PID, else `$XDG_SESSION_ID` since a terminal-started shell sits in another
  scope); the lock screen reports back with `set_session_locked()`, which
  `Idle` waits for (3s cap, Noctalia's lockAndSuspend) before suspending when
  `lock_before_suspend`. UI (`bar/lock_surface`): Windows-11 two-stage
  panel — cover (12h clock 72pt + "dddd, MMMM d") at 38% height, any key/click
  → login (avatar ring 130/124, GECOS name, 200x30 password pill) at 42%,
  animated 300ms OutCubic with a 40px shift via `Gtk::Overlay::
  signal_get_child_position` (so the overlay positions/animates the two
  columns); Escape clears + returns to the cover. Typing lands in a hidden
  `Gtk::Text` (password mode, 1x1, opacity 0) mirrored through the
  controller so multi-monitor surfaces show the same dots; dots are plain
  circle-filled glyphs (user request — Noctalia's passwordChars shapes were
  dropped), clipped at 110px, caret blinks 530ms via a CSS opacity
  transition; a 30px eye / eye-off button toggles plain text. **Per user: no
  submit arrow button** (Enter submits) **and no focus border** on the field.
  Animations mirror Noctalia's Behaviors: stage swap 300ms OutCubic with the
  40px shift, the cover column slides down from y = -h/2 on appear (Noctalia's
  column is created before its parent has a height, so its `Behavior on y`
  slides it into place), pills fade 300ms and the session menu 150ms through a
  per-widget opacity tick fader (widgets stay mapped, `can_target` follows). Pills 200px above the
  bottom: info (mTertiary, "Password"), error (mError, "Authentication
  failed"), countdown (mSurface) — the power button's menu (suspend, logout,
  [hibernate if `session.items.hibernate`], reboot, shutdown; commands from
  `session_actions.hpp`) arms Noctalia's 10s countdown, second click fires,
  X/Escape cancels. Battery bottom-right = the bar's win11 glyph logic.
  Background (`bar/lock_background`): `LockWallpaperCache` (Noctalia's
  ImageCacheService role) decodes the image **async at each monitor's pixel
  size** (`gdk_pixbuf_new_from_stream_at_scale_async`, header read sync to
  pick the covering axis) and renders blur = `lock_screen.blur` × 48px
  (Noctalia's blurMax) ONCE into a texture with a **private GL renderer**
  (`gsk_gl_renderer_new` + `gsk_renderer_realize_for_display`, GTK ≥ 4.14 —
  no window needed; gtkmm has no Gsk::Renderer, so C API) with 2×radius
  overscan so edges don't fade. LockScreen prepares every monitor at startup,
  on config change and on monitor changes, so a surface's first frame paints
  the finished wallpaper — decoding/blurring at lock time made the screen
  appear black and stuttered the clock slide-in. The four-stop mShadow
  gradient is a CSS linear-gradient box. Avatar: `~/.face` pre-scaled + center-cropped to
  124px (a Gtk::Picture otherwise grows to the file's size — that bug
  shipped for one build), fallback = tabler "user" glyph on mSurfaceVariant.
  Config `lock_screen.background` / `lock_screen.blur` (defaults "" / 0 =
  Noctalia's); settings app "Lock screen" sidebar page (Bar / Launcher /
  Session menu / Lock screen / Idle / Notifications; `lock_page`) with an
  image entry row + GtkFileDialog (image/* filter) and a blur % slider.
  Empty background = plain black + gradient (no wallpaper service yet).
  Dev: `HS_LOCK_PREVIEW=1|2` renders the UI as a normal overlay layer window
  (no session lock, Escape closes) so styling and PAM can be exercised
  without lock-out risk. NOT ported: lockScreenMonitors (black surfaces on
  unselected monitors), tint, lock font setting, avatar setting, fprintd,
  screen corners, media visualizer/weather, autoStartAuth, per-entry custom
  commands.
- 2026-09-03 — OSD (Noctalia's OSD.qml + LockKeysService, phase 4). Split in
  two: `services/osd` decides *when* (diffs Pulse output/input state on every
  signal_changed, Brightness and LockKeys signals; Noctalia's gating: 2s
  startup grace, no volume OSD while the audio popover is mapped, no
  brightness OSD while the battery popover is mapped — modules report via
  `Osd::set_*_panel_open()` from the popover's map/unmap —, 300ms microphone
  suppression after the default sink changed) and `bar/osd_window` renders.
  The window is a click-through overlay layer surface (`Gdk::Surface::
  set_input_region` with an empty region on every map — Noctalia's `mask:
  Region {}`), exclusive zone 0 so a top bar's strip pushes it down, 9px edge
  margins, and a **fixed window size per orientation** (320x72 horizontal,
  80x280 vertical, Noctalia's numbers) with the card positioned inside via
  `gtk_fixed_set_child_transform` — that transform also gives the 0.85→1
  scale + opacity fade (300ms InOutQuad; opacity floor 0.01, the buffer
  gotcha) without ever resizing the mapped surface; the lock-key card is
  content-sized (min 180/153) and hugs the anchored side. Progress bar = cairo
  DrawingArea with value + color eased over 300ms; text/icon colors via CSS
  state classes (`primary`/`error`/`dim`) with a CSS color transition. Two
  backend findings: (1) **sysfs backlight writes DO raise inotify events**
  (verified with a Gio.FileMonitor + brightnessctl) — the kernel calls
  fsnotify for write(2) on sysfs attributes — so `Brightness` now watches its
  file and external changes (brightnessctl, keybinds) show the OSD; the
  earlier "no inotify on sysfs" note only holds for kernel-driven changes;
  (2) keyboard LEDs are kernel-driven, so `LockKeys` polls
  `/sys/class/leds/input*::{caps,num,scroll}lock/brightness` every 200ms like
  Noctalia (OR-ed across keyboards, first read silent), only while
  `osd.enabled`. Config `osd.enabled` / `osd.location` (8 anchors, default
  top_right) / `osd.orientation` (auto / landscape / portrait — auto is
  Noctalia's position-derived rule, the explicit values are the user's
  addition; the card hugs the anchored edges in either orientation) with an
  "On-screen display" settings page holding Position, Orientation and
  Enable (per user); Noctalia's autoHideMs (2000), overlayLayer
  (on), backgroundOpacity (1) and enabledTypes (all four here) are fixed —
  the user's own keybinds still call `qs -c noctalia-shell ipc call volume …`,
  which does nothing without Noctalia; wpctl/brightnessctl trigger the OSD.
  NOT ported: per-monitor OSD (phase 1), volumeOverdrive >100% coloring, the
  drop-shadow effect beyond a CSS box-shadow, IPC custom-text OSD.
- 2026-09-04 — Wallpaper (Noctalia's WallpaperService + Background.qml, phase
  7). Rendering is in-process like Noctalia: `WallpaperWindow` = one
  background-layer window per `Gdk::Monitor` (`gtk_layer_set_monitor`,
  exclusive zone -1, hotplug via the monitor list's items-changed), mapped
  only while there is an image so an empty setup leaves any other wallpaper
  tool visible — there is no enable switch (per user; Noctalia's
  wallpaper.enabled was dropped the same day it landed). `WallpaperView` draws one `append_scaled_texture` per fill
  mode (Noctalia's calculateUV: center 1:1, crop cover, fit contain, stretch,
  repeat via `push_repeat` from the top-left) on black, textures decoded
  async at monitor pixel size per fill (`WallpaperTextureCache`, cover /
  contain / exact / native capped at 2x screen, EXIF orientation applied).
  Transitions are Noctalia's shaders re-expressed as GSK nodes — no GLSL
  (GtkGLShader is gone): fade = `push_cross_fade`; wipe / disc / stripes =
  `push_mask(ALPHA)` with linear / radial gradients as the mask, reproducing
  the shader math (wipe's biased float direction buckets, disc's aspect-
  corrected radius from a random centre, stripes' 4..24 bands at a random
  angle with the 10% wave lag and alternating sweep), InOutCubic over
  `transition_duration_ms`, edge softness with Noctalia's quadratic mapping
  (0.001+0.499s² / 0.001+0.299s²), one type picked at random per change from
  the `transitions` set. **pixelate and honeycomb are NOT rendered** (they
  need per-frame offscreen re-rendering); the keys are accepted so a Noctalia
  config round-trips, and the settings page lists only the four. Startup
  animates the first image from black (Noctalia's startup transition, 100ms
  after mapping, centred disc) — the views must stay empty until then. The
  `Wallpaper` service scans the folder asynchronously (not recursive, hidden
  files skipped, extensions in the shared `wallpaper_files.hpp`), rescans on
  folder changes (300ms coalesce), keeps the image on screen + the random
  shuffle bag in `~/.cache/hypr-shell/wallpaper.json` (the shell never writes
  config.json; `wallpaper.current` is the settings app's pick, adopted on
  value-change edges like DND), and runs the slideshow (Noctalia's timings:
  interval restarts on any manual pick, enabling or changing the order
  switches immediately; random = shuffle bag, alphabetical = next in sorted
  order). Settings: "Wallpaper" sidebar page right after Bar — enable, folder
  entry + folder dialog (no enable switch), a 4-column `GtkFlowBox` grid of 120x80 cover
  thumbnails (Noctalia's tile: rounded, accent border on the current one,
  others dimmed until hovered, filename underneath; thumbnails decoded async,
  centre-cropped to 384px squares and cached as PNG under
  `~/.cache/hypr-shell/wallpapers/thumbnails/<sha256(path@384x384@mtime)>.png`
  like Noctalia's ImageCacheService; the grid is the LAST group, inside a
  ScrolledWindow capped at 420px that scrolls — per user), fill mode,
  transitions switch (user's addition — Noctalia has none), transition types
  expander, duration slider (0.5–10s), slideshow switch, order, interval in
  minutes (stored in seconds). `edge_smoothness` stays config-only (its
  slider was removed per user). The grid follows the folder (GFileMonitor) and the
  shell's state file so slideshow picks move the highlight. The settings
  binary now loads a small CSS provider for the tiles (first custom CSS in
  hypr-shell-settings). NOT ported: per-monitor folders/wallpapers,
  recursive/browse view modes, sort orders, favorites, Wallhaven, solid
  colour mode, fill colour, `useOriginalImages`, the separate wallpaper
  selector panel (per user: settings only), `hypr-shell --wallpaper-*` IPC.
  Dev hooks: `HS_WALLPAPER_DUMP`, `HS_WALLPAPER_TRANSITION` (see dev loop).
- 2026-09-04 — Night light (Noctalia's NightLightService + NightLightSubTab
  over hyprsunset; two user cuts: no day temperature — day is a fixed
  neutral 6500 K, so the day phase simply has no filter process, Noctalia's
  own "6500 → stop hyprsunset" branch — and no automatic (location-based)
  scheduling: the sunrise/sunset times are the only schedule, greyed out in
  the settings while force activation is on. A first version had ported the
  location flow (api.noctalia.dev geocode + Open-Meteo sun times via curl);
  it was removed the same day at the user's request). `services/night_light`
  spawns `hyprsunset -t <K>` (Gio::Subprocess, `wait_async` → crash
  handling: a non-requested exit while night/forced restarts after 2 s, 5
  attempts, like runner.onExited) and computes the schedule itself since
  hyprsunset has none: night = `[sunset, sunrise)` (inverted pairs handled),
  a one-shot timer to the next boundary; forced mode bypasses it. **Only one
  CTM manager may run per compositor**: a second hyprsunset prints "A CTM
  manager is already running" and exits within milliseconds — that is why
  the feature "did not work" next to Noctalia's daemon, and why every start
  first runs Noctalia's `pkill -x hyprsunset; pkill -x wlsunset`, then waits
  until the processes are gone **plus 300 ms** (Hyprland frees the slot a
  moment after the client disconnects; without the grace the first start
  still died once). Resume: logind `PrepareForSleep(false)` → apply(force)
  + a 2 s retry (Time.onResumed + resumeRetryTimer). Not ported: Noctalia's
  toasts on toggle. Config `night_light.*` (enabled, forced, night_temp
  1000..6000 — Noctalia's 6500-500 cap —, manual_sunrise/sunset "HH:MM").
  Settings: "Night light" page after Wallpaper — Enable (insensitive with
  an explanatory subtitle when `hyprsunset` is not in PATH, replacing
  Noctalia's `command -v hyprsunset` check + warning toast), Night slider
  (saves debounced 250 ms; the shell debounces temp changes 300 ms too so
  dragging doesn't restart hyprsunset per pixel), a Scheduling heading with
  Sunrise/Sunset combos in 30-minute steps, Force activation. Disabling
  clears `forced`, like Noctalia. Dev: `HS_NIGHT_LIGHT_DRY_RUN=1`.
- 2026-09-04 — Profile picture + name fallbacks (user request): the lock
  screen and the control center's profile card share `services/user_info.hpp`
  (`user_display_name()` = trimmed GECOS real name, else the login name —
  GLib returns "Unknown" for an empty GECOS; `user_avatar_path()` = ~/.face
  or the HS_LOCK_AVATAR override) and `bar/avatar.cpp`'s
  `load_avatar_texture(path, size)`, which centre-crops the picture and,
  when the path is empty or unreadable, decodes the bundled
  `data/avatar-fallback.svg` (a silhouette on mSurfaceVariant, GResource,
  via librsvg's pixbuf loader) so both places show a picture instead of the
  tabler "user" glyph — the glyph remains only as the last resort if the
  resource itself fails. Noctalia keeps the glyph fallback (NImageRounded's
  fallbackIcon "person"); the image is the user's preference.
- 2026-09-04 — Control center (Noctalia's ControlCenter widget +
  ControlCenterPanel, scoped by the user to the profile row plus four cards
  with on/off switches and no close button — the shortcuts / weather rows,
  the position setting, the right-click menu and the launcher middle click
  were NOT ported). **Profile card** (64px, always shown, added the same day
  when the user noticed it missing): 41px avatar ringed 2px mPrimary (45px since 2026-09-05, user request)
  (`~/.face` / `HS_LOCK_AVATAR`, pre-scaled + centre-cropped like the lock
  screen's, tabler user glyph fallback), real name (bold 11pt) over
  "Uptime: 1d 2h 3m" (Noctalia's formatVagueHumanReadableDuration from
  /proc/uptime, refreshed every 60 s while open), then 33px round Settings
  (`open_settings()`) and Session menu (GAction "session" — dropdown or
  fullscreen per `session.mode`) buttons; both close the popover first via
  the panel's `signal_request_close`. Noctalia's third button (Close) was
  dropped per user. Module `control_center` (default right section): the noctalia
  glyph (U+EC33 in the bundled font; replaced on 2026-09-05 by the tabler
  "adjustments-horizontal" sliders U+EC38 — Noctalia's control-center
  settings icon — since the gear already means hypr-shell-settings inside
  the panel, per user), click toggles a 440px popover; cards
  are stacked with Noctalia's 13px margins at its fixed heights — audio 110
  (was Noctalia's 60 until 2026-09-05, see below), brightness 60, media 220,
  sysmon 84 — so the popover never resizes
  (popover-resize gotcha; the media card swaps empty/active content inside
  its fixed box). **Audio card** = Noctalia's AudioCard: output | input
  columns (output ABOVE input since 2026-09-05, user request), 15px
  transparent mute button (7pt glyph, mError when muted; 20px / 12pt since
  2026-09-05) + device description (9pt; 12pt since 2026-09-05) over an
  NSlider-look GTK scale (16px knob with a
  3px mPrimary ring, 6px track, gradient fill), wheel steps 5%, half
  opacity without a device; writes go straight to `Pulse` (optimistic local
  state already avoids Noctalia's 100ms sync loop). **Brightness card**:
  icon (sun-off ≤0.1%, low ≤50%, high), "Brightness", "NN%", slider,
  100ms debounce into `Brightness` (which debounces its logind write too);
  hidden without a backlight; default OFF like Noctalia. **Media card**:
  new `Mpris` service (session-bus proxies per org.mpris.MediaPlayer2.*
  name, Position polled 1s while playing since it has no change signal,
  active = playing else most recently updated, `set_active` for the picker)
  — background = `MediaBackground` custom widget: rounded clip, mSurface,
  cover-scaled art (decoded async at 256px from file:// or http(s) via
  Gio::File) under `gtk_snapshot_push_blur(8×0.33)` and a 65% mSurface
  scrim; content = player picker (caret + identity, only with >1 players,
  a nested popover in the session-dropdown style), disc glyph at 72pt when
  idle, else title (13pt bold, 2 lines) / artist (10pt mSecondary) / album
  (11pt), a 20px-knob seek scale (user seeks debounced 75ms via
  `signal_change_value`, external position updates held 700ms after the
  last drag) and prev / play-pause / next 33px round buttons hidden when
  the capability is missing (row re-centres). Noctalia's virtual
  browser+app player merge, MPRIS blacklist, preferred player and the
  spectrum visualizer were NOT ported. **System monitor**: new
  `SystemStats` service (Noctalia's formulas: /proc/stat Δ(total−idle)/Δtotal
  with iowait counted idle, MemTotal−MemAvailable, statvfs with df's
  ceil(used/(used+avail)), hwmon coretemp mean of temp*_input / k10temp
  temp1 / cpu*thermal zones max), polling 1s/5s/30s only while the card's
  panel is open (register/unregister_consumer), and `CircleStat` = NCircleStat
  in cairo (57px gauge, 5.7 line, 240° arc from 150°, value bold 9.4pt,
  icon 10.45pt in the gap, 300ms OutCubic; since 2026-09-05 scaled to a 66px
  gauge filling the card, value bold 12pt, icon 13pt — user request), colours mPrimary / mTertiary
  ≥80 / mError ≥90, the temperature arc normalised to 100 °C. Settings:
  "Control center" module row + cog subpage with the four switches. Dev
  hook: HS_OPEN_CONTROL_CENTER=1 (Chromium's YouTube tab was the MPRIS test
  player).
- 2026-09-04 — VPN module (Noctalia's VPN widget + VPNPanel + VPNService,
  1:1). Service `VpnService` (`services/vpn`): everything through nmcli like
  Noctalia — `-t -f NAME,UUID,TYPE,DEVICE connection show` parsed from the
  right so names with ':' survive, only `vpn` / `wireguard` types, active =
  bound to a device; `connection up|down|delete uuid X`, `connection import
  type wireguard|openvpn file P` (type from the `.ovpn` extension), success
  judged by Noctalia's output strings ("successfully activated" /
  "Connection successfully" / "successfully deleted" / "successfully added"),
  stderr's first line as `last_error`. Refresh 1 s after any
  `NetworkManager::signal_changed` (Noctalia greps `nmcli monitor` lines
  instead) plus a 60 s poll. Module `vpn` (default right section, enabled by
  default like every module) is the first **BarPill** port: icon label +
  `Gtk::Revealer` text (SLIDE_LEFT when the module sits in the right section
  — text on the inner side of the icon, Noctalia's oppositeDirection —,
  SLIDE_RIGHT otherwise), `bar.vpn.display_mode` on_hover (500 ms delay,
  300 ms slide, hidden on leave) / always_show / always_hide, icon-only on
  vertical bars or with empty text; text = first active profile (+ N) or
  the connecting one; `icon_color` / `text_color` palette keys become
  `.color-<key>` CSS classes. Panel = Noctalia's VPNPanel
  at its 440x500 (popover-resize gotcha): header card with shield (mPrimary
  when active), "VPN", a round import button; per profile a
  card with NToggle-style switch (name + Connected / Not connected /
  Connecting… / Disconnecting… / Removing…), trash button → inline confirm
  row ("Delete this VPN profile?", Delete in mError, X); empty state with
  shield-off, hint and Import button; import via `Gtk::FileDialog`
  (*.conf, *.ovpn, starts in ~/Downloads). The panel refreshes on every open
  like Noctalia. Removed the same day per user: the right-click context
  menu (Noctalia's NPopupContextMenu with Disconnect/Connect/Widget
  settings), the panel's refresh and close buttons, and the settings cog
  subpage — `bar.vpn.display_mode` / `icon_color` / `text_color` stay
  config-only (the "VPN" module row has no cog). Not ported: Noctalia's
  toasts on connect/disconnect/import/delete (no toast UI). Tested with a
  temporary `nmcli connection add type wireguard` profile (deleted
  afterwards); the machine has no real VPN profiles. **Superseded 2026-09-05:
  the whole module was removed in favour of the settings page (see below).**
  The import dialog is
  opened with a **null parent**: parenting it to the bar (a layer surface)
  was a Wayland protocol error that closed the whole shell ("Lost connection
  to Wayland compositor") — shipped that way for one build. Dev hooks:
  HS_OPEN_VPN=1 (panel), =3 (panel + import dialog).
- 2026-09-04 — Module popovers float 6px off the bar (user request): the
  eight per-module "pick the free side" switches collapsed into
  `place_bar_popover()` (`bar/bar_popover.hpp`), which sets the position AND
  `set_offset()` along the bar's normal (+y below a top bar, -y above a
  bottom bar, ±x for vertical bars). `kBarPopoverGap` is a compile-time
  constant, not config — nothing in Noctalia/Waybar exposes it either.
  **2026-09-05**: two fixes after the user saw the 440px control-center
  popover cut off at the screen's right edge and no gap on vertical bars.
  (1) GTK hangs a popover off the *anchor widget's* edge, so the 6px offset
  was measured from the icon, not the bar: on a 35px bar the icon's far
  edge is 4px (horizontal) or 9px (vertical) short of the bar's outer edge,
  and on vertical bars the popover overlapped the bar. `place_bar_popover`
  now adds the anchor-to-bar-edge distance (anchor bounds via
  `compute_bounds(window)`) to the gap, so the contents start exactly
  kBarPopoverGap outside the bar on every side. (2) Along the bar the
  popover is centred only when that fits inside the bar window (= the
  monitor); otherwise it is hung off the anchor's near edge through the
  popover's own halign (horizontal bars) / valign (vertical bars) — GTK
  maps START/END to the corner gravities (the app-menu context-menu
  finding) — instead of relying on the compositor's SLIDE constraint, which
  Hyprland applies from the positioner's *requested* size (a popover that
  ends up wider than requested — the control center measures 466px, not
  its 440px request — still hangs over). The fit test measures the
  popover's *child* + 12px padding per side: a hidden popover itself
  measures 0x0, and its shadow may legitimately hang off screen. Later the
  same day (user request, seeing the Apps popover 18px and the clipboard
  popover 13px from the screen edge): a START/END-aligned popover is also
  shifted along the bar (the offset's other axis) so its near edge sits
  exactly kBarPopoverGap from the screen edge — the same 6px as between bar
  and popover — on horizontal and vertical bars alike. Dev hooks:
  every `HS_OPEN_*` hook now calls `place_bar_popover()` before `popup()`
  (they had skipped it, so hook screenshots never exercised the placement);
  `HS_POPOVER_DEBUG=1` logs the anchor bounds, window size, natural size,
  chosen alignment and 600ms later the popup surface's final position/size
  (`gdk_popup_get_position_x/y`) — positions are relative to the bar
  surface and include the shadow margins (GTK's `box-shadow: 0 8px 24px` →
  35px left/right, 26 top, 44 bottom). Verified by those numbers only: the
  session was locked by Noctalia's idle daemon (the user was away) while
  testing, so screenshots showed the lock screen.
- 2026-09-04 — Taskbar module (Noctalia's Taskbar widget + TaskbarSettings,
  ported 1:1 except where noted; module key `taskbar`, default left section
  after workspaces, enabled by default like every module). Data: `j/monitors`
  → `j/clients` → `j/activewindow` on every window/workspace/monitor event,
  coalesced 30 ms and serial-guarded; a window is "focused" only when it is
  the active window AND sits on an active workspace (Noctalia's check);
  windows sort by workspace, x, y, address (toSortedWindowList). Filters:
  `only_same_monitor` compares the client's monitor name with the bar
  surface's `Gdk::Monitor` connector (single bar today; refreshed on map),
  `only_active_workspaces` uses each monitor's activeWorkspace (+ special).
  Pinned apps are the launcher's store (`Apps::pinned()`,
  `~/.cache/hypr-shell/pinned_apps.json`, Noctalia's dock.pinnedApps role);
  ids compare through `normalize_app_id()` (lowercase, ".desktop" stripped)
  so Noctalia-style and our ids both match, and `Apps::is_pinned` /
  `toggle_pinned` now use it too. Model = Noctalia's updateCombinedModel +
  sortApps: pinned first in pinned order (running or not), then running
  apps in a transient session order; drag-and-drop (Gtk::DragSource /
  DropTarget carrying the index) reorders and persists the pinned order
  (`Apps::set_pinned`); Noctalia's shift animation while dragging was NOT
  ported. Pinning itself happens in the app menu's right-click menu (the
  taskbar has no menu). Window class → desktop entry is `Apps::lookup_for_class()`, a port
  of ThemeIcons.findAppEntry: id / StartupWMClass, the substitutions and
  regex tables, reverse-domain / hyphen variants, fuzzy over ids, icons,
  names, then separator-stripped containment — cached until entries
  reload; icon = entry icon, else the class as an icon name, else
  application-x-executable. Look: 25px mSurfaceVariant capsule (Noctalia's
  default-density capsuleHeight, radiusM, marginM padding), items to_odd(25
  × `icon_scale`) = 21px, a 4px indicator hanging 2px under the icon —
  mPrimary focused, mHover hovered — via a per-item Gtk::Overlay.
  Hide modes visible / hidden / transparent (300 ms OutCubic opacity tick).
  Left click focuses (`focus_window` now also raises via
  `hl.dsp.window.alter_zorder`, Noctalia's focusWindow) or launches a pinned
  entry via GIO; wheel cycles focus with the 150 ms cooldown. Config-only
  extras (Noctalia's remaining widget settings): `show_title` (+
  `title_width`, `smart_width`, `max_width_percent` shrink titles to fit the
  screen share — the capsule itself is not clipped), `icon_scale`,
  `item_gap` (default 6 — Noctalia's 2 was too tight for the user).
  Settings: "Taskbar" module row + cog subpage with the four rows the user
  kept (Hiding mode, Only from same monitor, Only from active workspaces,
  Show pinned apps). Removed the same day per user: the right-click context
  menu (Focus / Pin / Close / desktop actions / Widget settings — it had
  shipped for one build, with a GSK colour-matrix "colorize icons" option
  and `Hyprland::close_window`, all dropped), the capsule background (the
  icons sit directly on the bar, so Noctalia's capsuleColor is not
  reproduced) and the Colorize icons option.
- 2026-09-04 — Pinning moved from the launcher to the app menu (user
  request): the launcher's per-row pin button is gone; right-clicking an app
  menu tile opens a one-entry context popover ("Pin to taskbar" / "Unpin
  from taskbar", pin / pinned-off glyphs) that toggles `Apps::toggle_pinned`
  on the entry id. One `Gtk::Popover` is re-parented to the clicked tile's
  Gtk::Image on each open (an Image ignores popover children in its layout;
  a tile Box would allocate the popover inline — the popover-anchor gotcha)
  and unparented on every grid rebuild / close; keys are left to GTK while
  it is open, like the session dropdown. **Popover-alignment finding**: GTK
  anchors a popover by the popover's own `halign` — START hangs it off the
  parent's left edge, END off the right, FILL/CENTER centres it — so the
  menu uses START on the left half of the columns and END on the right half
  and stays inside the panel (centred, it spilled past the panel on the
  first column). Dev hook: HS_OPEN_APP_MENU=3 opens
  it on the first tile.
- 2026-09-05 — Settings app made GNOME-Settings-like (user request, three
  items from todo.txt): (1) **sidebar icons** — `kSidebarPages` became a
  `SidebarPage {name, title, icon}` table (`settings/search.hpp`) and each
  row is a symbolic icon + label (Adwaita names: focus-top-bar, wallpaper,
  night-light, system-shutdown, system-lock-screen, alarm,
  display-brightness, notifications, help-about; the Launcher row uses the
  app menu's tabler rocket glyph U+EC45 via an `icon = "glyph:…"` entry
  rendered by `make_page_icon()` as a `.page-glyph` label in the bundled
  font, since Adwaita has no rocket, per user); rows keep libadwaita's
  navigation-sidebar padding with 6px vertical / 4px horizontal box margins
  (an earlier `padding: 4px 0` override left the icons flush with the edge);
  sidebar 200–260px, window 920x660. (2) **Search** (`settings/search.cpp`): a toggle at the start of
  the sidebar header + a GtkSearchBar under it (Ctrl+F = `win.search`;
  type-to-search via our own CAPTURE key controller on the window, which
  ignores modifiers, non-graphic keys and any focus inside a
  GtkEditable/GtkText — GtkSearchBar's built-in key capture was NOT used
  because it would steal keys from entry rows). The index is **not a table**:
  every query walks the widget tree of each stack child, collecting
  AdwPreferencesRow titles/subtitles with an AdwNavigationPage › group ›
  expander breadcrumb, skipping rows hidden by state (visibility checked up
  to the AdwPreferencesPage only — stack/nav children are legitimately not
  the visible child) — so new rows are searchable automatically. Scores:
  title prefix 5 > title substring 4 > all words in title 3 > subtitle 2 >
  crumb 1; a page-title hit floats above its rows; 40 max. Results replace
  the page list (GtkStack pages/results/"No Results Found" AdwStatusPage);
  activating selects the sidebar page, pops the Bar nav to root and pushes
  the row's subpage tag, expands enclosing AdwExpanderRows, scrolls with
  `gtk_viewport_scroll_to` and flashes `row.search-hit` for 1.8s. **Gotcha**:
  GtkViewport computes scroll_to from the descendant's *previous*
  allocation (it runs before laying out its child), so on a page shown for
  the first time the request is silently dropped — the scroll is issued
  from a tick callback once the row has a height. Enter in the entry
  activates the first hit. The launcher's static settings index
  (`launcher_window.cpp`) stays as is — the two binaries share no widgets.
  (3) **About page** (`settings/about_page.cpp`, last sidebar entry like
  GNOME): property-style rows (`.property`, selectable values) — Device
  Name (/etc/machine-info PRETTY_HOSTNAME else hostname), Hardware Model
  (DMI sys_vendor + product_family|product_name, placeholder strings
  dropped), Memory (MemTotal, IEC), Processor (cpuinfo model name through
  GNOME's prettify: ®/™, drop CPU/Processor/"@ x GHz", "× N" cores),
  Graphics [1..n] (every /sys/class/drm/card*/device with PCI class 0x03,
  deduped by real path, named from hwdata's pci.ids with "X [Y]" reordered
  to "Y (X)"), Disk Capacity (sum of non-removable /sys/block sizes, SI),
  Firmware Version (DMI bios_version), OS Name (os-release PRETTY_NAME —
  **no logo, per user**), OS Type, Kernel, Windowing System (GdkDisplay
  type name), Hyprland Version (async `hyprctl -j version` → "version",
  "Not running" without an instance signature), hypr-shell Version
  (`-DHS_VERSION` from meson's project version). Dev hooks:
  `HS_SETTINGS_SEARCH=<q>`, `HS_SETTINGS_SEARCH_OPEN=1`,
  `HS_SETTINGS_PAGE=about_page`.
- 2026-09-05 — Hotspot page (todo.txt item; no Noctalia counterpart, GNOME's
  Wi-Fi hotspot dialog + Windows' Mobile hotspot as the model).
  **State lives in NetworkManager, not config.json**: the hotspot is a live
  network action (like the Wi-Fi list in the network panel), NM already
  persists the profile, and a config-driven on/off would drift the moment
  activation fails — so the shell is not involved at all and the page
  (`settings/hotspot_page.cpp`) talks to `nmcli` itself, async through
  GSubprocess (`run_cmd` + a weak "alive" guard). One profile, id
  "Hotspot" (nmcli's own hotspot name — an existing one is adopted),
  created on the first edit with `type wifi … 802-11-wireless.mode ap
  ipv4.method shared ipv6.method ignore connection.permissions
  user:<name>` — user-owned so `--show-secrets` reads the PSK back without
  a polkit prompt (verified: a system-owned profile would prompt). Fields:
  SSID (1–32 bytes), Security None / WPA2 (`key-mgmt wpa-psk`, rsn/ccmp) /
  WPA3 (`key-mgmt sae`, `pmf 3`) — None is `modify … remove
  802-11-wireless-security`; Password (AdwPasswordEntryRow, 8–63, refresh
  button generates 12 unambiguous chars; the enable switch is insensitive
  with an explanatory subtitle while invalid, rows get `.error`); Band
  Automatic / 2.4 GHz / 5 GHz = `802-11-wireless.band` "" / bg / a — a
  single AP interface broadcasts on one channel, so "both" (the user's
  wording) maps to Automatic like Windows' "Any available"; Hidden
  (`802-11-wireless.hidden`); Wi-Fi adapter combo (physical wifi devices =
  `/sys/class/net/<dev>/device` exists; row hidden with one adapter);
  **Keep Wi-Fi connected** = the hotspot runs on a virtual AP interface
  "<adapter>-ap" (name encodes the parent; "ap0" if too long) with
  `cloned-mac-address stable` (a virtual AP must not share the parent's
  MAC), created on enable via `pkexec iw dev <adapter> interface add
  <name> type __ap` (CAP_NET_ADMIN; the polkit agent prompts; it does not
  survive a reboot, so autostart in this mode only works once the
  interface exists), then `nmcli device set <name> managed yes` 1.2s after
  it appears and `connection up`. Support is read from `iw phy <phy>
  info`'s "valid interface combinations" (a combination with managed + AP
  and total ≥ 2; `#channels <= 1` adds "same band, use Automatic" to the
  subtitle); **iw is not installed here**, so the switch shows "Install the
  iw package…" and the whole concurrent path is UNTESTED (the adapter is
  iwlwifi AX201, which normally allows AP+STA on one channel). Start
  automatically = `connection.autoconnect`. Edits save debounced 600ms via
  `connection modify`; NM only applies AP changes on activation, so a
  running hotspot is re-`up`ed after each save ("restarts briefly", said on
  the page). Status row (`.property`): Off / Starting… / "Active on <dev> ·
  <gateway from ip -4 addr> · N devices connected" (`iw dev station dump`,
  else reachable `ip -4 neigh` entries) / errors in `.error`; the page
  polls `connection show Hotspot` every 3s while mapped, and never rewrites
  a row that has a pending save or keyboard focus (the poll would move the
  cursor). Tested: create/modify/read-back (nmcli-modified profile shows
  correctly), not activation — enabling on the single adapter would drop
  the user's Wi-Fi. NOT ported/added: GNOME's QR code (needs a QR encoder),
  "share from" interface choice (NM picks the default route), client list
  with names, data usage.
- 2026-09-05 — App menu button is icon-only on vertical bars (user request;
  `bar.app_menu.display` is ignored there — text/icon_text fall back to the
  icon, since a side bar has no room for a label; Noctalia's side-bar pills
  behave the same). Applies on config reload like every other setting.
- 2026-09-05 — Theming: "User interface" settings page (todo.txt) with dark
  mode (default on), accent colour and font. **Mechanism**: every hard-coded
  colour in `data/css/*.css` became a GTK named colour (`@mPrimary`,
  `@mOnPrimary`, `@mPrimaryHover`, `@mSecondary`, `@mOnSecondary`,
  `@mTertiary`, `@mOnTertiary`, `@mError`, `@mOnError`, `@mSurface`,
  `@mOnSurface`, `@mSurfaceVariant`, `@mOnSurfaceVariant`, `@mOutline`,
  `@mShadow`, `@mHover`, `@mOnHover` — Noctalia's names) whose defaults are
  `@define-color`d in `data/style.css`; the shell's theme provider
  (`App::apply_theme`, APPLICATION+1, the old opacity provider grown up)
  re-`@define-color`s all of them from `ui.*`, and **a higher-priority
  provider's @define-color overrides a lower one's across providers** —
  verified live (light mode recoloured the bar/launcher). Catppuccin
  leftovers were folded into tokens (#11111b bar → mSurface, #cdd6f4 /
  #e0eaff / #ffffff text → mOnSurface, #a6adc8 → mOnSurfaceVariant);
  battery green/amber and the trigger window's rgba stay literal. Palette
  derivation (`services/palette.hpp`, header-only, shared): dark → the
  accent IS mPrimary (a swatch shows as picked — a tone-80 pastel rule was
  tried and dropped the same day), mOnPrimary picked for contrast: a 30%
  shade of the hue when the accent's WCAG relative luminance is above 0.35,
  else white (the blue swatch's calendar header was unreadable with the
  shade, per user), secondary/tertiary/
  neutrals tinted with the accent hue (tertiary = hue+87°, surfaces L 0.08 /
  0.13, on-surface L 0.89) — the default accent #bfc2ff reproduces the CSS
  snapshot within a couple of RGB steps; light → Material tone-40 primary
  (accent darkened to L ≤ 0.42 for white text), surfaces L 0.985 / 0.92,
  error #ba1a1a. **Font**: the nine per-area `font-family: "Fira Sans"`
  rules collapsed into one inherited `window, popover { font-family }` in
  style.css that the theme provider re-emits with `ui.font` (icon fonts keep
  their class rules; "Fira Sans SemiBold" became the inherited family +
  weight 600). Cairo/GSK code (calendar ring, OSD bar + track, control
  center gauges + their "Fira Sans Bold" label, notification countdown)
  reads `Theme::get().rgba("mX")` / `font()` instead of constants. Config
  `ui.font` / `ui.accent` ("#rrggbb", else default) / `ui.dark_mode`.
  Settings page after Bar (appearance icon), laid out like GNOME's
  Appearance panel (user request, superseding a first version with a switch,
  a GtkColorDialogButton and reset buttons): a "Style" boxed row with two
  cairo-drawn preview tiles (Default / Dark — blue desktop, two dark windows
  behind, a light or dark one in front; the selected tile gets a 2px
  `var(--accent-bg-color)` outline), an "Accent Color" boxed row of ten
  round swatches (grouped GtkCheckButtons — note their CSS node is `radio`,
  not `check`; the shell's lavender default first, then GNOME's nine:
  blue teal green yellow orange red pink purple slate; a custom hex in
  config leaves none checked), and a Font row (GtkFontDialogButton at
  family level, use-font preview). American "Color" on this page, per
  user. The settings window follows the
  theme itself: AdwStyleManager FORCE_DARK/LIGHT and the accent as
  libadwaita's accent — **libadwaita ≥ 1.6 ignores `@define-color
  accent_bg_color` from an app provider; it must be set as `:root {
  --accent-bg-color; --accent-fg-color; --accent-color }`** (both forms are
  emitted). The libadwaita accent is the *light-mode* primary (a mid tone
  that carries white text) in both modes. Not done: font size, per-token
  overrides (users keep `~/.config/hypr-shell/style.css`, which may also
  `@define-color` any token), the settings window's font.
- 2026-09-05 — Uniform module gaps (user request): `.module` padding is
  6px each side for every module (was 10px, with 5px overrides on the four
  status icons and 6px on the taskbar); workspaces keep 4px + the buttons'
  2px margin and the taskbar 6px, so every neighbouring pair is 12px apart.
- 2026-09-05 — Accent usage trimmed (user request, after trying the blue
  swatch): the control center's profile picture lost its 2px accent ring,
  its Settings / Session buttons use the app menu's round-button look
  (`.cc-profile-btn`: mSurfaceVariant, mOutline, mOnSurface glyph, tertiary
  hover) instead of accent glyphs, and the system-monitor gauges draw in
  mOnSurface (warning mTertiary / critical mError kept). The calendar's
  today cell is the accent with mOnPrimary text (was mSecondary). Later the
  same day: the notification panel's header bell and the audio panel's
  Output / Input labels went from mPrimary to mOnSurface as well. The
  launcher's search box took Noctalia's NTextInput metrics (36px tall: 22px
  min-height + 6px padding + 1px border; 11pt; radius 16) and its
  hovered / selected row is a grey shade (`alpha(@mOnSurface, 0.14)`, text
  colours unchanged) instead of Noctalia's mHover tint (user request) —
  then generalised: the `mHover` / `mOnHover` tokens themselves became the
  grey shade (dark: hsl(hue, 6%, 24%) ≈ #393a3f, light: L 0.86) with
  on-surface text, and every hover / selection rule (taskbar title pill,
  app menu header buttons + tiles + context menu, session dropdown, control
  center buttons, launcher rows) uses them; the taskbar's hovered indicator
  is mOnSurfaceVariant and the control-center slider knob's :active state
  mPrimaryHover. mTertiary remains only where it is a status colour (lock
  screen info pill, system-monitor warning arc).
- 2026-09-05 — VPN moved out of the bar into hypr-shell-settings (user
  request): `bar/modules/vpn`, `bar/vpn_panel`, `services/vpn`, the
  `bar.vpn.*` config keys (`Config::Vpn`), the `vpn` module row/default
  section, the `.vpn` / `.vp-*` CSS and the `HS_OPEN_VPN` hook are gone; a
  `vpn` name left in an old config's `bar.modules` / `bar.layout` is dropped
  by the layout resolver. Replacement: `settings/vpn_page.cpp`, a sidebar
  page after Hotspot (tabler shield-lock glyph) — Status row ("Connected to
  X and N more"), a Profiles group with an Import header button
  (GtkFileDialog, *.conf / *.ovpn, ~/Downloads; parented to the settings
  window — a normal toplevel, so the layer-shell parenting crash does not
  apply) and one AdwSwitchRow per `vpn` / `wireguard` profile (subtitle
  WireGuard / VPN plugin + "connected on <dev>", trash suffix → AdwAlertDialog
  confirm, disabled while connected). nmcli grammar and Noctalia's
  success-text checks are the VpnService's, now in the settings binary;
  `run_command()` + `first_line` / `trim` / `split_lines` were factored out
  of the hotspot page into `settings/command.{hpp,cpp}` for both. Refresh on
  map + every 3 s while mapped (no NM DBus signal in the settings app),
  rows rebuilt from `connection show` parsed from the right. Tested with a
  throwaway WireGuard profile (deleted afterwards); the machine still has no
  real VPN profile.
- 2026-09-05 — Wi-Fi / Bluetooth panels gained a header **refresh button**
  (tabler refresh U+EB13, transparent 28px round `button.np-refresh` left of
  the switch, insensitive while the radio is off) and lost their
  "Scanning…"/"Connecting…" text in favour of a spinner (user request):
  `bar/busy_indicator` is Noctalia's NBusyIndicator — a 270° round-capped
  arc rotating once per 900 ms (animationSlow × 2), cairo in a DrawingArea,
  colour from the widget's CSS `color` (`.np-spinner` mOnSurface,
  `.np-spinner-on-primary` on the connected card), tick callback only while
  mapped and running. The refresh button's child is a Gtk::Stack glyph ↔
  spinner, so scanning shows in place. Wi-Fi: click = `NetworkManager::
  scan()` (no-op while a scan runs); the row being connected swaps its
  Connect button for a spinner (Noctalia's per-row busy indicator, tracked
  as `connecting_ssid_` in the panel and cleared on `signal_action_done`)
  and Disconnect swaps for one too. Bluetooth: click = new
  `Bluez::refresh_devices()` — `Adapter1.RemoveDevice` on every unpaired /
  untrusted / unconnected / idle device (BlueZ's in-memory discovery cache
  only; they reappear as discovery finds them, which makes the list visibly
  repopulate) then StopDiscovery, restarting via `set_scanning(true)` in the
  stop callback if still wanted (`want_scanning_`) and not already re-armed
  by the panel's rebuild; the spinner stays on throughout since `scanning_`
  is left optimistic. "No devices found" still shows when idle with an
  empty list. Live-tested: Wi-Fi scan spinner + refresh; the Bluetooth
  adapter was off (disabled state verified; discovery refresh untested
  live).
- 2026-09-05 — Control center type scale (user request: bigger system
  monitor fonts, bigger icons, matching sizes, output over input): body
  12pt everywhere (`cc-device`, `cc-uptime`, `cc-player-name` 9→12,
  `cc-artist` 10→12, `cc-album` 11→12, gauge values 9.4→12 bold), headings
  14pt bold (`cc-name`, `cc-title` 13→14); mute buttons 20px / 12pt glyph
  and the brightness icon 12pt (were 15px / 10pt); the system-monitor gauge
  scaled 66/57 so it fills the card's 66px inner height at the same
  kCardSysmon 84 (radius 27.5, line 6.6, icon 13pt). The audio card's two
  columns became two stacked rows (kCardAudio 60→110, `audio_card_`
  VERTICAL, spacing 8).
- 2026-09-05 — Panel type scale (user request: slightly larger, cohesive
  fonts across the battery / audio / network / bluetooth / notification /
  session popovers): panel title 18px bold, section or card heading 16px
  bold, body 16px (SSIDs, device names, notification summaries, session
  labels), secondary 15px (values, notification bodies), caption 14px
  (security / battery subs, app name, time), pill buttons 15px bold, row
  icons 18px (connected-card icon 19). Each was 2–3px smaller and the
  captions ranged 11–12px before (raised in two steps the same day). The scale is written at the top of
  `data/css/panels.css`; toasts, the calendar and the control center (12pt
  body / 14pt headings, set the same day) were not touched. Every `HS_OPEN_*`
  hook now takes a delay in ms (a value above 1), not just the three noted
  above.
- 2026-09-05 — Clipboard history (Noctalia's ClipboardService +
  ClipboardProvider, as a **separate overlay window** that shares the
  launcher's design — user request; nothing was added to the launcher).
  `services/clipboard`: the history IS cliphist's database. While
  `clipboard.enabled` (and cliphist + wl-clipboard are in PATH) the service
  runs Noctalia's two watchers, `wl-paste --type text|image --watch cliphist
  store`, restarted 1 s after they exit, spawned with
  `prctl(PR_SET_PDEATHSIG)` in the child so they die with the shell — unless
  `pgrep -f` finds watchers already running (Noctalia's Quickshell owns two
  today: its enableClipboardHistory is on), then ours stay off so nothing is
  stored twice. Everything else shells out asynchronously: `cliphist list
  -preview-width 100` (tab-separated `id<TAB>preview`, made valid UTF-8; the
  "[[ binary data 1.2 MiB png 1920x1080 ]]" meta parsed like parseImageMeta;
  Noctalia's browser-junk filters and its text/link/file/code/color
  detection ported), `cliphist decode` (thumbnails decoded from the bytes
  with gdk_pixbuf_new_from_stream_at_scale_async at 128 px, LRU of 100 —
  **one decode at a time from a queue** (Noctalia's _b64Queue): the first
  version spawned a `cliphist decode` per image row inside the click
  handler, 55 fork/execs, and the window took 600 ms to map; now 15 ms
  cold, ~65 ms warm with 65 rows; the queue is cleared on every row
  rebuild),
  `cliphist decode ID | wl-copy [--type mime]` to copy, plus the paste shortcut,
  pressed 150 ms after our window closed (focus is back on the previous
  window) and 60 ms after `wl-copy` exited (it forks; the child serves the
  selection). **The shortcut is sent by Hyprland** — `Hyprland::
  send_shortcut()` → `hl.dsp.send_shortcut({ mods, key })`, no window =
  current focus, grammar taken from `/usr/share/hypr/stubs/hl.meta.lua` +
  LuaBindingsDispatchers.cpp and verified against a non-existent window
  target — because wtype's virtual keyboard (Noctalia's way) carries its own
  keymap and Chromium/Electron apps (VS Code) dropped its Ctrl+V while GTK
  apps took it (user report). wtype stays the fallback outside Hyprland,
  with every modifier released via `-m` (a wtype run exiting with modifiers
  held left Ctrl+Shift stuck in Hyprland's seat, which blocked the user's
  touchpad workspace gestures after a paste). Noctalia sends Ctrl+Shift+V
  for all text; that is VS Code's Markdown preview, so the focused window's
  class from `j/activewindow` decides — Ctrl+Shift+V for terminals (kitty,
  foot, alacritty, wezterm, ghostty, konsole, …), Ctrl+V elsewhere and for
  images —, `printf ID | cliphist delete`, `cliphist wipe`. Ids
  are digit-checked before reaching `sh -c`. `bar/clipboard_window`: overlay
  layer, **exclusive zone 0** (unlike the launcher's -1) so the surface
  excludes the bar and a top/bottom position sits beside it, and keyboard
  mode **ON_DEMAND**, not EXCLUSIVE: Hyprland routes pointer input only to
  exclusive layer surfaces while one is mapped (`InputManager::
  mouseMoveUnified` consults `m_exclusiveLSes` first — verified in the
  0.56.2 source), so with EXCLUSIVE the bar's clipboard button could not be
  clicked to close the window (user report); on-demand layers still receive
  keyboard focus on map (`LayerSurface::onMap`, GRABSFOCUS), verified with
  GTK's is-active after mapping;
  panel = the launcher's fixed max(25 %, 552) × max(50 %, 600) box with the
  launcher's CSS classes (window `launcher`, `launcher-panel`,
  `launcher-search`, `launcher-row`, …) placed by `clipboard.position`
  (center / top_left / top / top_right / bottom_left / bottom /
  bottom_right, 13 px from the edges). Rows: type glyph, colour swatch
  (cairo, for #rgb/#rrggbb entries) or a 64×36 thumbnail — the Picture is an
  unmeasured, clipped Gtk::Overlay child, because a plain Picture reports
  the scaled image as natural width and a 1920×49 banner widened the row —
  title/description per formatTextEntry / formatImageEntry (no relative
  times: cliphist has none, Noctalia fakes them). Enter/click copies, or
  pastes with `paste_on_click`; the selected row shows a trash button and
  Delete (with an empty query) removes; footer = count + "Clear all"
  (wipe). Substring search over the preview like Noctalia; no category
  chips, no preview pane, no annotation tool. Config `clipboard.*`:
  enabled (default off — needs cliphist; turned on in the user's config
  the same day), show_images (off hides image entries), paste_on_click,
  position. Module `clipboard` (tabler clipboard glyph, default right
  section before notifications, hidden while history is disabled or
  cliphist is missing, no cog — per user) and GAction "clipboard" /
  `hypr-shell --clipboard` (`bind = SUPER, V, exec, hypr-shell --clipboard`,
  shown on the settings page). Settings: "Clipboard" sidebar page after
  Launcher (edit-paste icon) with the four rows; Enable is insensitive with
  an install hint without cliphist, Paste on click without wtype. Dev hook:
  HS_OPEN_CLIPBOARD (delay in ms above 1).
- 2026-09-05 — Notifications settings page trimmed to three rows (user
  request): Do not disturb, Always on top (`overlay_layer`), Position. The
  daemon is on by default, so the Enable switch went; density, background
  opacity, the duration / history / sound / filter-rule groups are hidden
  with `gtk_widget_set_visible(FALSE)` rather than deleted — the widgets
  stay parented so the existing load / save / sensitivity code is untouched
  and the rows can return by flipping the flag. Those keys are config-only
  now; the page description says so. The launcher's static settings index
  lost the matching entries.
- 2026-09-05 — Clock tooltip (user request): hovering the clock shows
  `bar.clock.tooltip_format` (strftime through Glib::DateTime, default
  "%A, %B %-d, %Y" → "Saturday, September 5, 2026"; empty = no tooltip,
  invalid = no tooltip rather than a fallback), re-rendered with the label
  every minute. Settings: a "Tooltip" entry row under the Clock subpage's
  Time format group, saved through the same `format-key` handler as the two
  label formats.
- 2026-09-05 — Clock format guide in the settings app (user request):
  Noctalia's NDateTimeTokens ported under the Clock subpage's Time format
  group as a "Format guide" boxed list — category badge (libadwaita's
  accent / success / warning / error variables standing in for Noctalia's
  mPrimary / mSecondary / mTertiary / mError), monospace token pill,
  description, live example pill re-rendered every second while mapped.
  Tokens are strftime for Glib::DateTime (`%-d` = no leading zero) instead
  of Qt's; the 8 "Common" patterns plus hour / minute / second / AM-PM /
  timezone / year / month / day tokens and `%%`. Clicking a row appends its
  token to the format entry that had focus last (Noctalia's tokenClicked
  into its single field; ours has three), defaulting to the horizontal one.
  The list scrolls inside a 360px cap like the wallpaper grid.
- 2026-08-31 — Config's initial load is a synchronous read (tiny local file, needed
  before the first frame so the bar doesn't flash defaults) — accepted deviation from
  the async-I/O rule; reloads go through Gio::FileMonitor. Invalid JSON warns and falls
  back to defaults rather than crashing or keeping stale state. Bottom-positioned bar
  gets a `bottom` CSS class on the window so the theme can flip the hairline border.
- 2026-09-03 — Added `docs/` (16 numbered pages + README) so the project can be
  continued by hand without an AI assistant: getting started, architecture,
  code tour, config reference, a full add-a-module tutorial, services/async
  patterns, settings-app recipe, panels, styling, Hyprland IPC, gtkmm primer,
  gotchas, roadmap guide, workflow checklist, plus a C++ tutorial and a GTK
  tutorial scoped to this codebase (refreshed the same day after bluetooth, notifications,
  the launcher and the settings sidebar landed). CLAUDE.md stays the short
  authoritative charter; the docs are the long-form companion and must be
  updated alongside it (config reference for new keys, code tour for new files,
  gotchas for new traps).
- 2026-09-05 — Resource pass over the whole tree (user request: less RAM/CPU,
  leaks, dead code, comment noise). Findings that changed code: (1) the lock
  screen's wallpaper cache never freed anything — every blur value the
  settings slider passed through left an 8–33 MB blurred texture behind, and
  the unblurred decode stayed next to the blurred one; `LockWallpaperCache::
  retain(path, blur)` now runs from `prepare_wallpapers()` and keeps only the
  current image/radius, dropping the unblurred decode once its blur exists
  (`Entry` owns its GdkTextures). (2) `WallpaperTextureCache::retain` compares
  the fill mode too, and a view prunes when a re-decode lands, so a fill
  change no longer leaves the old texture around. (3) Hyprland IPC requests
  go through a queue with at most 4 in flight: an event burst (a window
  opening fires openwindow + activewindow + windowtitle + focusedmon …) opened
  a dozen sockets at once and Hyprland's listen backlog refused some with
  EAGAIN, so whichever module lost the race kept stale state (seen live:
  10 "Resource temporarily unavailable" warnings when the settings window
  opened). Workspaces coalesce events 30 ms like the taskbar and reuse their
  buttons; the taskbar keeps its widgets and only refreshes focus / tooltip
  / title text when the item set and layout are unchanged (windowtitle
  events fire constantly). (4) The control center's cards only update while
  the popover is open — the media card was downloading album art on every
  track change with the panel closed — and MPRIS position polling runs only
  while a consumer is registered (`Mpris::register_consumer`, like
  SystemStats); positions extrapolate in between. (5) `std::regex` is gone
  (notification rules were re-compiled per notification per rule): rules
  compile once per config change, clipboard / app-id patterns are static
  `Glib::Regex` (PCRE2). Glib::Regex takes `Glib::UStringView`, i.e. pass
  `.c_str()`, and replacements use `\1`, not `$1`. (6) LockKeys keeps the
  LED fds open and `pread()`s (three syscalls per 200 ms poll instead of
  open/read/close plus allocations); ActiveWindow caches the desktop entry of
  the current class; the bar only asks for the workspace's emptiness when
  auto-hide needs it; the clock places its popover on click, not every
  minute. (7) Settings app: `nl_time_option` leaked a `g_strdup_printf`
  string per call (~100 at startup), two GtkStringLists leaked a ref, the
  clock guide's 1 s example timer ran for the app's lifetime (now only while
  the list is mapped), the VPN page rebuilt identical rows every 3 s, and the
  Settings state's timers/monitors are released with it. Build: `b_lto=true`,
  `b_ndebug=if-release`, `strip=true` in meson's default_options (an existing
  build dir needs `meson configure build -Db_lto=true -Dstrip=true`).
  Measuring: the shell's RSS (~330 MB) is mostly shared GPU-driver libraries
  (libLLVM, libnvidia-*, libgallium) mapped by every GTK4 process; compare
  `Pss`/`Private_Dirty` from `/proc/<pid>/smaps_rollup` (~53 MB private)
  instead. Comment cleanup was limited to stale, wrong or code-restating
  comments; rationale ("why") comments stay.
- 2026-09-05 — Noctalia replaced (user request). Everything the user's
  keybinds and lid switch called through `qs -c noctalia-shell ipc call …`
  now has a hypr-shell equivalent: `--control-center` (toggles the bar
  popover), `--wallpaper-next` (`Wallpaper::next()`, the slideshow order),
  `--lock-and-suspend` (`lock_and_suspend()` in services/session: lock, then
  suspend once the lock screen confirms or after 3 s — Noctalia's
  lockAndSuspend, used by the lid switch), and volume / brightness keys go
  straight to `wpctl` / `brightnessctl` (5 % steps, Noctalia's defaults; the
  OSD reacts to the resulting changes). Noctalia's "wallpaper toggle" (its
  selector panel) maps to opening hypr-shell-settings on the Wallpaper page.
  `services/battery_alerts` ports Noctalia's battery toasts (enableBatteryToast
  was on): "Low battery" at ≤20 % and "Critical battery" at ≤5 % while
  discharging, once per discharge cycle, delivered through
  `NotificationService::notify_local()` (the same rules / history / DND /
  popup flow as a DBus Notify); `notifications.battery_alerts` (default true)
  has a "Battery warnings" switch on the Notifications page. In
  ~/.config/hypr/conf, keybinds.lua and autostart.lua were rewritten (backups
  `*.pre-hypr-shell`); autostart runs `/home/sandip/.local/bin/hypr-shell`
  instead of Noctalia. Noctalia's process was stopped, its config and
  package left in place for a fallback week. Known remaining gaps versus
  Noctalia: no system tray (no SNI apps were running), no
  org.freedesktop.ScreenSaver inhibit API (Wayland idle-inhibit still
  works), no keyboard-layout toast. The real session lock was still
  untested at switch time — the user tests `hypr-shell --lock` with a TTY
  open; recovery from a stuck lock is `hyprctl dispatch exit` from a TTY.
- 2026-09-05 — Controller input resets idleness (user request: the idle
  daemon blanked/locked mid-game). Hyprland only counts keyboard / pointer /
  touch input, so a new `services/gamepad` service reads gamepad / joystick
  evdev nodes itself: `/dev/input/event*` opened read-only + non-blocking
  (only ID_INPUT_JOYSTICK nodes carry the seated user's ACL from systemd's
  70-uaccess.rules — keyboards and mice stay root:input and fail with
  EACCES, which doubles as the filter; a node must also advertise a
  joystick / gamepad / d-pad / trigger-happy button, so a controller's
  motion-sensor and touchpad nodes are skipped), hotplug via a
  Gio::FileMonitor on /dev/input (CREATED retries up to 8×400 ms while udev
  has not applied the ACL yet, ATTRIBUTE_CHANGED re-tries once, DELETED /
  ENODEV removes), events read on `Glib::signal_io`. Activity = a button
  press or an axis moving past a dead zone of max(driver flat, range/8)
  from its last reported value (resting sticks drift), rate-limited to one
  signal per 250 ms. The `Idle` service turns that into an
  **idle-inhibit-v1 inhibitor** held for 3 s after the last report, on a
  bare `wl_surface` with no role and no buffer: Hyprland 0.56.2's
  `IdleInhibitor.cpp` finds no desktop surface behind it and marks the
  inhibitor "non-desktop, assumed visible", so it inhibits without any
  mapped window; `setInhibit(true)` disarms every obeying notification and
  sends `resumed` to idled ones (a running fade cancels), `setInhibit(false)`
  re-arms them with their full timeouts — the idle clock restarts, exactly
  like a key press. Verified live: with the seat idle for 5 s the heartbeat
  reported `resumed` the instant the (simulated) inhibitor appeared. No
  config key and no settings row: controller input is treated like mouse
  and keyboard input, which have none either. Glue for
  `idle-inhibit-unstable-v1.xml` is generated by wayland-scanner like the
  notify protocol. Dev hook: `HS_IDLE_SIMULATE_GAMEPAD=<ms>`. Untested
  with a physical controller (none was connected); the evdev path relies on
  the documented uaccess rule.
- 2026-09-05 — Settings app startup halved (todo.txt: "hypr settings take
  longer time to open"). Measured with a new `HS_SETTINGS_TIMING=1` hook
  (phase marks to stderr) and a throwaway SIGPROF sampler: first frame went
  from ~1.2–1.4 s to ~0.7–0.8 s after main(), against a ~0.4–0.6 s floor for
  an empty libadwaita window on this laptop (GTK init, theme CSS parse,
  EGL/Mesa/LLVM loading — shared by every GTK4 app). Four causes: (1) every
  cached wallpaper thumbnail was decoded synchronously by
  `gtk_picture_set_filename()` while building the grid (41 PNGs, ~300 ms) —
  the grid is now built on the Wallpaper page's first `map` and thumbnails
  decode async, four at a time; (2) `populate()` raised its `loading` guard
  ~250 lines in, so the control-center / taskbar writes fired their handlers
  and **saved config.json eight times per launch** (fsync each, the shell
  reloading every time) — the guard now comes first; (3) all fourteen
  sidebar pages were built before the first frame — only the Bar page is
  now; `build_secondary_pages()` builds, populates
  (`PopulateStage::Secondary`) and stacks the rest from an idle after the
  first frame, or earlier from a sidebar click / search
  (`SearchTargets::prepare`) / `HS_SETTINGS_PAGE`; (4) the content
  `GtkStack` and the Bar `AdwNavigationView` were homogeneous, measuring
  every hidden page each layout pass — both non-homogeneous now. The About
  page gathers its facts (pci.ids scan) from a low-priority idle. The
  remaining first-frame cost is the Bar page itself (fourteen module rows +
  fourteen layout rows with GtkDropDowns) plus GTK's fixed init.
