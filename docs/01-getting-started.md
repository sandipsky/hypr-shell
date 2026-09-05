# 01 — Getting started

## What you need

- **Arch Linux** running **Hyprland 0.56 or newer**. The install script uses
  `pacman` and refuses to run elsewhere. The shell itself needs a Wayland
  compositor that speaks `wlr-layer-shell` (Hyprland does).
- A terminal, `git`, and a text editor. VS Code with the `clangd` extension
  gives you completion and inline errors; run `meson setup build` once and
  clangd picks up `build/compile_commands.json` automatically.
- Optional but useful: `socat` (watch Hyprland events), `hyprctl` (ships with
  Hyprland), `GTK_DEBUG=interactive` (GTK's inspector, see below).

## First build and install

```sh
git clone <your fork url> hypr-shell
cd hypr-shell
./install.sh
```

`install.sh` does four things:

1. Installs missing packages with `sudo pacman -S --needed ...`. The list is
   the `DEPS` array at the top of the script.
2. Runs `meson setup build --prefix=$HOME/.local --buildtype=release` the
   first time (skipped if `build/` already exists).
3. Runs `meson compile -C build`, then `meson install -C build`.
4. Refreshes the font cache for the bundled icon fonts.

You end up with two binaries, `~/.local/bin/hypr-shell` (the bar) and
`~/.local/bin/hypr-shell-settings` (the settings window), plus a desktop
entry so the settings app shows up in launchers.

To start the bar with Hyprland, add this to `~/.config/hypr/hyprland.conf`:

```
exec-once = ~/.local/bin/hypr-shell
```

If you previously ran another bar or shell (Waybar, Noctalia, ...), remove
or comment out its `exec-once` line first. Two bars fight over the same
screen edge.

hypr-shell is also a **notification daemon**. Only one process can own the
`org.freedesktop.Notifications` bus name, so disable mako, dunst, or
Noctalia's daemon. If another daemon is still running, hypr-shell waits in
the bus queue and takes over the moment it exits; until then the bell module
shows an empty history and its tooltip says so.

For the **app launcher**, bind the toggle in `hyprland.conf`:

```
bind = SUPER, SPACE, exec, hypr-shell --launcher
```

`hypr-shell --launcher` forwards the request to the running instance and
exits; it never starts a second shell.

## The everyday dev loop

Edit code, then:

```sh
meson compile -C build && pkill -x hypr-shell; ./build/hypr-shell
```

This compiles, kills the running bar, and starts the freshly built one in
the foreground so you see its log output. Ctrl+C stops it. Do this from a
terminal inside your Hyprland session; the bar cannot run over SSH or in a
plain TTY because it needs the compositor.

When you are happy, `./install.sh --restart` installs the new build and
restarts the installed copy.

The settings app is run the same way:

```sh
meson compile -C build && ./build/hypr-shell-settings
```

It only edits `~/.config/hypr-shell/config.json`. The running bar watches
that file and applies changes immediately, so you can keep both open side by
side.

## Where things live at runtime

| Path | Purpose |
|------|---------|
| `~/.config/hypr-shell/config.json` | All user settings. Absent = defaults. See [config reference](04-config-reference.md). |
| `~/.config/hypr-shell/style.css` | Optional user CSS override, hot-reloaded. See [styling](09-styling-and-icons.md). |
| `~/.cache/hypr-shell/notifications.json` + `notifications/` | Notification history (100 entries) and cached notification images. Runtime state, never config. |
| `~/.cache/hypr-shell/pinned_apps.json` | Apps pinned from the app menu's right-click menu; the taskbar shows them in this order (drag to reorder). |
| `~/.local/bin/hypr-shell`, `~/.local/bin/hypr-shell-settings` | Installed binaries. |
| `~/.local/share/fonts/hypr-shell/` | Bundled icon fonts. |
| `~/.local/share/hypr-shell/sounds/` | Default notification sound. |
| `~/.local/share/applications/hypr-shell-settings.desktop` | Launcher entry. Shows as plain "Settings" with GNOME Settings' icon, bundled as `dev.hyprshell.Settings.svg` (installed under `~/.local/share/icons/hicolor`, see `data/icons/NOTICE.txt`). |
| `build/` | Meson build directory (git-ignored). Safe to delete and recreate. |

Rule worth knowing early: **the shell never writes `config.json`**. Only the
settings app (or you) writes it. Anything the shell must remember at runtime
(history, pins, do-not-disturb toggled from the bell) goes to
`~/.cache/hypr-shell/` or stays in memory.

## Seeing log output

The shell logs with GLib (`g_message`, `g_warning`, `g_debug`). Warnings
always print to stderr; debug lines only when you ask for them:

```sh
G_MESSAGES_DEBUG=all ./build/hypr-shell
```

Note that `install.sh --restart` sends stderr to `/dev/null`. If something
"silently does nothing" after an install, run the binary from a terminal
instead and read the warnings.

## Inspecting the widget tree

GTK ships an inspector. Start the bar with

```sh
GTK_DEBUG=interactive ./build/hypr-shell
```

and a second window opens where you can click any widget, see its CSS
classes, and edit CSS live. This is the fastest way to find out which class
a widget has and why a style rule is not applying.

## Dev hooks (environment variables)

Some popovers are awkward to open by hand while debugging, so the code has
opt-in hooks that open them about 0.8 s after startup:

| Variable | Effect |
|----------|--------|
| `HS_OPEN_CALENDAR=1` | Opens the calendar popover (every `HS_OPEN_*` value above 1 = delay in ms, e.g. `3000`) |
| `HS_OPEN_BATTERY=1` | Opens the battery panel |
| `HS_OPEN_AUDIO=1` | Opens the audio panel |
| `HS_OPEN_NETWORK=1` | Opens the Wi-Fi panel |
| `HS_OPEN_BLUETOOTH=1` | Opens the Bluetooth panel |
| `HS_POPOVER_DEBUG=1` | Logs every module popover's anchor, size, alignment and final position |
| `HS_OPEN_NOTIFICATIONS=1` | Opens the notification history panel |
| `HS_OPEN_LAUNCHER=1` | Opens the app launcher |
| `HS_LAUNCHER_QUERY=<text>` | Pre-fills the launcher search every time it opens |
| `HS_LOCK_PREVIEW=1` | Shows the lock screen UI as a plain overlay window (no real session lock; Escape on the cover closes it). `=2` also opens its session menu |
| `HS_LOCK_AVATAR=<path>` | Overrides the `~/.face` avatar (empty = person glyph fallback) |
| `HS_PAM_SERVICE=<name>` | PAM service for the lock screen instead of the auto-detected one |
| `HS_SETTINGS_PAGE=<tag>` | Settings app: jump to a Bar module subpage (`workspaces`, `clock`, `active_window`, `battery`, `bluetooth`, `notifications`, ...) or a sidebar page (`wallpaper_page`, `night_light_page`, `launcher_page`, `session_page`, `lock_page`, `idle_page`, `osd_page`, `notifications_page`, `about_page`) |
| `HS_SETTINGS_SEARCH=<query>` | Settings app: open the sidebar search with that query; add `HS_SETTINGS_SEARCH_OPEN=1` to activate the first result after 1.5 s |
| `HS_HOTSPOT_SAVE=1` | Settings app: write the Hotspot page's shown settings to the NetworkManager profile 2 s after startup (creates it; never activates) |

Example: `HS_OPEN_BATTERY=1 ./build/hypr-shell`. To test notifications, run
`notify-send "Title" "Body"` from another terminal.

## Running outside Hyprland

The code is written to degrade gracefully: without
`HYPRLAND_INSTANCE_SIGNATURE` the Hyprland service reports
`available() == false` and the workspace / active-window modules stay
empty; without UPower the battery hides itself, and so on. The bar still
needs a layer-shell capable compositor to appear at all.

## Uninstall

```sh
./uninstall.sh          # stop + remove binaries and fonts
./uninstall.sh --purge  # also delete ~/.config/hypr-shell
```

Remove the `exec-once` line from `hyprland.conf` yourself.

## If the build fails

- **"Dependency X not found"**: a package is missing or the pkg-config name
  changed. The names meson looks for are in `meson.build` (`gtkmm-4.0`,
  `gtk4-layer-shell-0`, `nlohmann_json`, `libpulse`,
  `libpulse-mainloop-glib`, `libadwaita-1`). Check with
  `pkg-config --modversion gtkmm-4.0`.
- **Strange errors after pulling**: `rm -rf build && ./install.sh`. A stale
  build directory after a dependency upgrade is the usual cause.
- **"No such file" for a new .cpp you added**: every source file must be
  listed in `meson.build` by hand. See the [tutorial](05-tutorial-new-module.md).
