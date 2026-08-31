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
data/style.css                 default theme (embedded via GResource)
data/hypr-shell.gresource.xml
data/hypr-shell-settings.desktop
data/fonts/                    noctalia-tabler-icons.ttf (installed to
                               ~/.local/share/fonts/hypr-shell, MIT license alongside)
src/main.cpp                   App (Gtk::Application), CSS loading + user-CSS hot reload
src/bar/bar.{hpp,cpp}          Bar window (layer-shell setup)
src/bar/modules/*.{hpp,cpp}    one widget per bar module (workspaces, active_window,
                               clock, network, volume, battery)
src/services/config.{hpp,cpp}           config.json load + hot reload (Gio::FileMonitor)
src/services/hyprland.{hpp,cpp}         Hyprland IPC singleton
src/services/upower.{hpp,cpp}           battery via UPower DisplayDevice (Gio::DBus)
src/services/network_manager.{hpp,cpp}  NM primary connection + wifi strength (Gio::DBus)
src/services/power_profiles.{hpp,cpp}   active profile from power-profiles-daemon (Gio::DBus)
src/services/pulse.{hpp,cpp}            default-sink volume/mute (libpulse-glib)
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
      *(pulled forward 2026-08-31: config.json + hot reload, bar position/height,
      module toggles landed; module order + per-monitor bars remain)*
- [ ] **Phase 2 — More bar modules**: battery (UPower), network (NetworkManager),
      bluetooth (BlueZ), audio (PipeWire), system tray (StatusNotifierItem + DBusMenu),
      keyboard layout, system stats.
      *(pulled forward 2026-08-30: battery + network + audio status icons landed;
      bluetooth, tray, keyboard layout, stats remain)*
- [ ] **Phase 3 — Notifications**: own `org.freedesktop.Notifications` daemon —
      popups, history/notification center, do-not-disturb. Only one daemon can own the
      bus name: mako/dunst/Noctalia's daemon must be disabled when this ships.
- [ ] **Phase 4 — Panels & OSD**: volume/brightness OSD, calendar popover,
      control-center panel.
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
- 2026-08-30 — Workspace scroll uses Hyprland's relative selector `"e+1"`/`"e-1"`
  (existing workspaces, all monitors, wraps — same as common `mouse_down` binds)
  rather than local next/prev math; revisit `m+1`-style per-monitor cycling when
  phase 1 brings per-monitor bars. Smooth deltas are accumulated to one switch per
  wheel notch.
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
- 2026-08-31 — Config's initial load is a synchronous read (tiny local file, needed
  before the first frame so the bar doesn't flash defaults) — accepted deviation from
  the async-I/O rule; reloads go through Gio::FileMonitor. Invalid JSON warns and falls
  back to defaults rather than crashing or keeping stale state. Bottom-positioned bar
  gets a `bottom` CSS class on the window so the theme can flip the hairline border.
