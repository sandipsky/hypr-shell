# 04 — Config reference

File: `~/.config/hypr-shell/config.json`. Written by `hypr-shell-settings` or
by hand; **the shell only reads it** and reloads on every save.

Rules the parser follows (see `Config::load()` in `src/services/config.cpp`):

- Every key is optional. A missing key means its default.
- A key of the wrong type is ignored (default used); the rest of the file is
  still applied.
- A file that is not valid JSON, or whose top level is not an object, logs a
  warning and the shell runs with all defaults. It never crashes and never
  keeps stale values.
- Unknown keys are ignored by the shell and preserved by the settings app.
- Numbers are clamped to their documented range.

There are three top-level objects: `bar` (the bar and its modules),
`notifications` (the daemon and toast popups), and `launcher` (the app
launcher).

## Full example with defaults

```json
{
  "bar": {
    "position": "top",
    "visibility": "visible",
    "show_on_workspace_switch": true,
    "show_when_workspace_empty": false,
    "background_opacity": 0.88,

    "modules": {
      "launcher": true, "workspaces": true, "active_window": true,
      "network": true, "bluetooth": true, "volume": true,
      "battery": true, "notifications": true, "clock": true
    },

    "layout": {
      "left":   ["launcher", "workspaces"],
      "center": ["active_window"],
      "right":  ["network", "bluetooth", "volume", "battery", "notifications", "clock"]
    },

    "workspaces":    { "mode": "dynamic", "fixed_count": 5, "scroll_wrap": true },
    "active_window": { "hide_mode": "hidden", "show_title": true, "title_mode": "title",
                       "no_window_text": "default", "show_icon": true },
    "clock":         { "first_day_of_week": 0,
                       "format_horizontal": "%H:%M %a, %b %d", "format_vertical": "%H %M",
                       "tooltip_format": "%A, %B %-d, %Y" },
    "battery":       { "show_power_profiles": true, "show_brightness": true, "show_refresh_rate": true },
    "bluetooth":     { "auto_connect": false },
    "notifications": { "show_unread_badge": true, "hide_when_zero": false, "hide_when_zero_unread": false }
  },

  "notifications": {
    "enabled": true,
    "do_not_disturb": false,
    "density": "default",
    "location": "top_right",
    "overlay_layer": true,
    "background_opacity": 1.0,
    "respect_expire_timeout": false,
    "low_urgency_duration": 3,
    "normal_urgency_duration": 8,
    "critical_urgency_duration": 15,
    "clear_dismissed": true,
    "save_to_history": { "low": true, "normal": true, "critical": true },
    "sounds": {
      "enabled": false, "volume": 0.5, "separate_sounds": false,
      "low_sound_file": "", "normal_sound_file": "", "critical_sound_file": "",
      "excluded_apps": "discord,firefox,chrome,chromium,edge"
    },
    "rules": [
      { "pattern": "*spotify*", "action": "mute" }
    ]
  },

  "launcher": {
    "enable_settings_search": true,
    "enable_session_search": true,
    "enable_web_search": false,
    "show_result_count": true,
    "show_all_apps": true
  },

  "lock_screen": {
    "background": "",
    "blur": 0.0
  },

  "wallpaper": {
    "directory": "",
    "current": "",
    "fill_mode": "crop",
    "transitions_enabled": true,
    "transitions": ["fade", "disc", "stripes", "wipe", "pixelate", "honeycomb"],
    "transition_duration_ms": 1500,
    "edge_smoothness": 0.05,
    "slideshow": false,
    "slideshow_interval_s": 300,
    "slideshow_order": "random"
  },

  "night_light": {
    "enabled": false,
    "forced": false,
    "night_temp": 4000,
    "manual_sunrise": "06:30",
    "manual_sunset": "18:30"
  },

  "osd": {
    "enabled": true,
    "location": "top_right",
    "orientation": "auto"
  }
}
```

## `bar.*`

| Key | Type | Default | Meaning |
|-----|------|---------|---------|
| `position` | `"top"` \| `"bottom"` \| `"left"` \| `"right"` | `"top"` | Screen edge. `left`/`right` make a vertical bar. |
| `visibility` | `"visible"` \| `"hidden"` \| `"auto_hide"` | `"visible"` | Always show / never map / overlay and slide away when not hovered. |
| `show_on_workspace_switch` | bool | `true` | Auto-hide only: peek on workspace change. |
| `show_when_workspace_empty` | bool | `false` | Auto-hide only: stay visible while the active workspace is empty. |
| `background_opacity` | number 0..1 | `0.88` | Alpha of the bar background. |

## `bar.modules.<name>`

Booleans keyed by module name. **Absent means enabled.** Known names:
`launcher`, `workspaces`, `active_window`, `network`, `bluetooth`, `volume`, `clipboard`,
`battery`, `notifications`, `clock`. A disabled module is not parented into
the bar at all.

## `bar.layout`

Three ordered arrays `left`, `center`, `right`. Resolution (identical in the
shell and the settings app): unknown names dropped, duplicates keep their
first placement, unmentioned modules append to their default section
(`kKnownModules` in `config.cpp`). `layout` decides *where*, `modules`
decides *whether*.

## `bar.workspaces`

| Key | Type | Default | Meaning |
|-----|------|---------|---------|
| `mode` | `"dynamic"` \| `"fixed"` | `"dynamic"` | Fixed shows 1..`fixed_count` with placeholders, plus real workspaces above the range. |
| `fixed_count` | int 1..50 | `5` | |
| `scroll_wrap` | bool | `true` | Scrolling past the end wraps. |

## `bar.active_window`

| Key | Type | Default | Meaning |
|-----|------|---------|---------|
| `hide_mode` | `"visible"` \| `"hidden"` \| `"transparent"` | `"hidden"` | Behaviour with no focused window. |
| `show_title` | bool | `true` | When false the two rows below are irrelevant. |
| `title_mode` | `"title"` \| `"appname"` | `"title"` | Window title or desktop-entry display name. |
| `no_window_text` | `"default"` \| `"desktop"` \| `"none"` | `"default"` | "No active window" / "Desktop" / nothing. |
| `show_icon` | bool | `true` | |

## `bar.clock`

| Key | Type | Default | Meaning |
|-----|------|---------|---------|
| `first_day_of_week` | `0` (Sun) \| `1` (Mon) | `0` | Calendar grid start. |
| `format_horizontal` | strftime | `"%H:%M %a, %b %d"` | Top/bottom bars. |
| `format_vertical` | strftime | `"%H %M"` | Left/right bars; space-separated tokens stack. |
| `tooltip_format` | strftime | `"%A, %B %-d, %Y"` | Tooltip while hovering the clock; empty string = no tooltip. |

Invalid formats fall back to `%H:%M`.

## `bar.battery`

Cards shown in the battery panel (each also needs its backend).

| Key | Default |
|-----|---------|
| `show_power_profiles` | `true` |
| `show_brightness` | `true` |
| `show_refresh_rate` | `true` |

## `bar.bluetooth`

| Key | Type | Default | Meaning |
|-----|------|---------|---------|
| `auto_connect` | bool | `false` | On the adapter's power-on edge (startup included), connect every paired device, staggered 500 ms, starting 1.5 s after the edge. Global, not per device. |

## `bar.taskbar`

Noctalia's Taskbar widget: one icon per running window plus the pinned apps
(`~/.cache/hypr-shell/pinned_apps.json`). Left click
focuses (or launches a pinned app), wheel cycles focus, dragging reorders
(the pinned order is saved). Pinning happens in the app menu (right-click a
tile → "Pin to taskbar" / "Unpin from taskbar"); the icons sit
directly on the bar without Noctalia's capsule background.

| Key | Type | Default | Meaning |
|-----|------|---------|---------|
| `hide_mode` | `visible` / `hidden` / `transparent` | `hidden` | Behaviour with no matching windows: keep showing, hide, or keep the space at opacity 0. |
| `only_same_monitor` | bool | `true` | Only windows on the bar's monitor. |
| `only_active_workspaces` | bool | `true` | Only windows on a monitor's active (or active special) workspace. |
| `show_pinned_apps` | bool | `true` | Show pinned apps that are not running (or filtered out) as launchers. |
| `show_title` | bool | `false` | Icon + window title per running app (horizontal bars only). Config-only. |
| `title_width` | int px | `120` | Title label width. Config-only. |
| `smart_width` | bool | `true` | Shrink titles so the widget fits `max_width_percent` of the screen (min 20px). Config-only. |
| `max_width_percent` | int 10..100 | `40` | Screen share for `smart_width`. Config-only. |
| `icon_scale` | 0.5..1 | `0.8` | Icon size as a share of the 25px capsule (odd pixel sizes, like Noctalia). Config-only. |
| `item_gap` | int px 0..24 | `6` | Space between items (Noctalia's default is 2). Config-only. |

The settings subpage exposes the first four rows only (per user).

## `bar.control_center`

The control center bar button (Noctalia's ControlCenter widget: the noctalia
glyph) opens a 440px panel: Noctalia's profile row (avatar from `~/.face`,
real name, "Uptime: 1d 2h 3m", Settings and Session menu buttons — no close
button, per user) always on top, then these four cards, each a switch on the
module's cog subpage (Noctalia's shortcuts and weather rows were not ported).

| Key | Type | Default | Meaning |
|-----|------|---------|---------|
| `show_media` | bool | `true` | Media player card (220px): blurred cover art, title / artist / album, seek bar, previous / play-pause / next, player picker when several MPRIS players run. |
| `show_audio` | bool | `true` | Audio card (60px): output and input columns, mute button + device name over a volume slider; wheel steps 5%. |
| `show_brightness` | bool | `false` | Brightness card (60px): icon, "Brightness NN%", slider; hidden without a backlight. Noctalia's default is off too. |
| `show_sysmon` | bool | `true` | System monitor card (84px): CPU usage, CPU temperature, memory and disk gauges (Noctalia's NCircleStat), amber at 80, red at 90. |

Dev hook: `HS_OPEN_CONTROL_CENTER=1`.

## `bar.notifications` (the bell module)

| Key | Type | Default | Meaning |
|-----|------|---------|---------|
| `show_unread_badge` | bool | `true` | Dot for entries newer than the last panel open. |
| `hide_when_zero` | bool | `false` | Hide the module when history is empty. |
| `hide_when_zero_unread` | bool | `false` | Hide the module when nothing is unread. |

## `notifications` (top level: daemon + popups)

Only `do_not_disturb`, `overlay_layer` and `location` have controls in
hypr-shell-settings (since 2026-09-05); every other key below is config-only.

Exposed as `Config::get().notifications()` (struct `Config::Notifications`).

| Key | Type | Default | Meaning |
|-----|------|---------|---------|
| `enabled` | bool | `true` | `false` releases the `org.freedesktop.Notifications` bus name so another daemon can take it. |
| `battery_alerts` | bool | `true` | Low (≤20 %) and critical (≤5 %) battery notifications while discharging, one per threshold per discharge cycle (Noctalia's enableBatteryToast). |
| `do_not_disturb` | bool | `false` | Baseline DND. The shell adopts it only when the value *changes*; the bell's right click toggles DND at runtime without writing the file. DND suppresses popups only; history still records. |
| `density` | `"default"` \| `"compact"` | `"default"` | Compact = small single-line toast cards. |
| `location` | `"top"` \| `"top_left"` \| `"top_right"` \| `"bottom"` \| `"bottom_left"` \| `"bottom_right"` | `"top_right"` | Toast anchor. |
| `overlay_layer` | bool | `true` | Toasts on the overlay layer (above fullscreen) vs the top layer. |
| `background_opacity` | number 0..1 | `1.0` | Toast card alpha. |
| `respect_expire_timeout` | bool | `false` | Honour the sender's expire timeout instead of the per-urgency durations. |
| `low_urgency_duration` | int 1..30 s | `3` | |
| `normal_urgency_duration` | int 1..30 s | `8` | |
| `critical_urgency_duration` | int 1..30 s | `15` | |
| `clear_dismissed` | bool | `true` | Dismissing a toast also deletes its history entry. |
| `save_to_history.low` / `.normal` / `.critical` | bool | `true` | Per-urgency history recording. |
| `sounds.enabled` | bool | `false` | Play a sound via `paplay`. Skipped while the output is muted; 100 ms rate limit. |
| `sounds.volume` | number 0..1 | `0.5` | |
| `sounds.separate_sounds` | bool | `false` | Per-urgency files instead of `normal_sound_file` for all. |
| `sounds.low_sound_file` / `normal_sound_file` / `critical_sound_file` | path | `""` | Empty = bundled `notification-generic.wav`. |
| `sounds.excluded_apps` | comma list | `"discord,firefox,chrome,chromium,edge"` | Apps that never play a sound (matched on prettified app names). |
| `rules` | array of `{pattern, action}` | `[]` | First match wins, tested against `"app summary body"`. `pattern` is `/regex/`, `*glob*` (case-insensitive) or a case-insensitive substring. `action`: `block` (drop entirely), `hide` (no popup/sound, keep in history), `mute` (no sound). |

Runtime state that is **not** config: history and image cache in
`~/.cache/hypr-shell/notifications.json` and `notifications/`.

## `launcher` (top level)

Exposed as `Config::get().launcher()` (struct `Config::Launcher`).
Application search and the calculator are always on.

| Key | Type | Default | Meaning |
|-----|------|---------|---------|
| `enable_settings_search` | bool | `true` | Match hypr-shell-settings rows; opens the app on that page. |
| `enable_session_search` | bool | `true` | lock / suspend / reboot / logout / shutdown entries. |
| `enable_web_search` | bool | `false` | "Search the web" fallback (Google in the default browser). |
| `show_result_count` | bool | `true` | Footer under the list. |
| `show_all_apps` | bool | `true` | `true`: fixed centred panel listing all apps when the query is empty. `false`: Spotlight mode, content-sized panel that grows downward with results. |

Pinned apps (`~/.cache/hypr-shell/pinned_apps.json`) are set from the app
menu's right-click menu and shown by the `taskbar` module, which also
reorders them by drag. The launcher has no pin button any more.

## `clipboard` (top level)

Exposed as `Config::get().clipboard()` (struct `Config::Clipboard`). The
clipboard history window (`hypr-shell --clipboard`, the `clipboard` bar
module). History is stored by cliphist; the shell only runs the
`wl-paste --watch cliphist store` watchers when no other process does.

| Key | Type | Default | Meaning |
|-----|------|---------|---------|
| `enabled` | bool | `false` | Record history and show the bar module. Needs `cliphist` and `wl-clipboard`. |
| `show_images` | bool | `true` | List copied images (with thumbnails); `false` hides them. |
| `paste_on_click` | bool | `false` | Enter/click pastes into the focused window (Hyprland's `send_shortcut`; `wtype` outside Hyprland) instead of only copying. Ctrl+Shift+V in terminals, Ctrl+V elsewhere. |
| `position` | `center` / `top_left` / `top` / `top_right` / `bottom_left` / `bottom` / `bottom_right` | `center` | Where the window's panel appears. |

## `lock_screen` (top level)

Exposed as `Config::get().lock_screen()` (struct `Config::LockScreen`). The
only two lock screen options (Noctalia's `general.lockScreenWallpaper` and
`lockScreenBlur`); everything else about the lock screen is fixed.

| Key | Type | Default | Meaning |
|-----|------|---------|---------|
| `background` | path | `""` | Image shown behind the lock screen, scaled to cover each monitor (`~` is expanded). Empty = plain dark background. |
| `blur` | 0..1 | `0.0` | Blur strength of that image (1 = 48px radius, Noctalia's `blurMax`). |

Not config: the avatar is `~/.face` when it exists (else a person glyph), the
name is the account's real name (GECOS), the PAM service is auto-detected
(`login` / `system-auth` / `common-auth`, override with `HS_PAM_SERVICE`).

## `wallpaper` (top level)

Exposed as `Config::get().wallpaper()` (struct `Config::Wallpaper`). The
desktop wallpaper drawn by the shell itself (Noctalia's
`Settings.data.wallpaper`, single folder, same image on every monitor).
The settings app's "Wallpaper" page edits all of it except `edge_smoothness`;
the image grid there writes `current`. Always on: with no folder or image the
shell simply maps no wallpaper window.

| Key | Type | Default | Meaning |
|-----|------|---------|---------|
| `directory` | path | `""` | Folder listed in the settings grid and used by the slideshow (`~` expanded; not recursive; hidden files skipped). |
| `current` | path | `""` | The image picked in the settings app. The shell adopts it when the value *changes*; the image actually on screen (slideshow picks included) is persisted by the shell in `~/.cache/hypr-shell/wallpaper.json`. With neither set, the first scan picks one from the folder. |
| `fill_mode` | `center` / `crop` / `fit` / `stretch` / `repeat` | `crop` | Noctalia's fill modes: 1:1 centred, cover, contain (black bars), stretched, tiled from the top-left. |
| `transitions_enabled` | bool | `true` | Animate changes; off = instant swap. |
| `transitions` | list of `fade` / `disc` / `stripes` / `wipe` / `pixelate` / `honeycomb` | all six | Noctalia's multi-select: one of the listed types is picked at random per change. `pixelate` and `honeycomb` are accepted but not rendered (shader-only in Noctalia); an empty list means instant. |
| `transition_duration_ms` | 500..10000 | `1500` | Animation length (InOutCubic). |
| `edge_smoothness` | 0..1 | `0.05` | Feathering of the wipe / disc / stripes edge (Noctalia's quadratic mapping). Config only — no settings row, per user. |
| `slideshow` | bool | `false` | Change the wallpaper automatically. Turning it on changes it immediately, like Noctalia. |
| `slideshow_interval_s` | 60..86400 | `300` | Seconds between changes (the settings app edits minutes). A manual pick restarts the timer. |
| `slideshow_order` | `random` / `alphabetical` | `random` | `random` is a shuffle bag (every image once before repeats, never the same twice in a row), `alphabetical` steps through the sorted folder. |

## `ui` (top level)

Theme of the shell and of the settings window. The whole palette derives
from these three values (see [styling](09-styling-and-icons.md#colours)).

| Key | Type | Default | Meaning |
|-----|------|---------|---------|
| `dark_mode` | bool | `true` | Dark surfaces with light text; `false` gives the light palette (Material tone-40 accent, near-white surfaces). Also forces the settings window's libadwaita colour scheme. |
| `accent` | `"#rrggbb"` | `"#bfc2ff"` | Accent colour (the settings page offers ten swatches; any hex works by hand). Dark mode uses it as `mPrimary` verbatim, with the on-accent text chosen for contrast (dark shade on light accents, white on dark ones); light mode darkens it so white text reads on it. Invalid strings fall back to the default. |
| `font` | string | `"Fira Sans"` | Text font family for every window and popover (icon fonts are unaffected). Sizes are not configurable. |

## `night_light` (top level)

Exposed as `Config::get().night_light()` (struct `Config::NightLight`).
Noctalia's `Settings.data.nightLight` driven through **hyprsunset** (must be
installed; the settings page greys out when it is not). The day temperature
is fixed at a neutral 6500 K, so during the day no filter process runs at
all (per user: no day temperature option).

| Key | Type | Default | Meaning |
|-----|------|---------|---------|
| `enabled` | bool | `false` | Master switch. Turning it off also clears `forced`. |
| `forced` | bool | `false` | Apply the night temperature now, ignoring the schedule. |
| `night_temp` | 1000..6000 | `4000` | Kelvin passed as `hyprsunset -t`. |
| `manual_sunrise` / `manual_sunset` | `"HH:MM"` | `06:30` / `18:30` | Night is `[sunset, sunrise)`, also when the pair is inverted. The settings app offers 30-minute steps and greys them out while `forced` is on. |

Noctalia's automatic (location-based) scheduling was dropped per user: only
the two times exist. Only one hyprsunset may run per compositor, so before
every start the shell kills a stale `hyprsunset` / `wlsunset` (a previous
shell's, or another shell's daemon), waits until it is gone plus 300 ms, then
spawns its own; crashes restart it (2 s, 5 attempts) and a system resume
re-applies it (logind `PrepareForSleep`). `HS_NIGHT_LIGHT_DRY_RUN=1` logs the
command instead of spawning it.

## `osd` (top level)

Exposed as `Config::get().osd()` (struct `Config::Osd`). The on-screen display
for output volume, microphone volume, brightness and Caps/Num/Scroll Lock
changes (Noctalia's `Settings.data.osd`). Only the two options the settings
app shows are configurable; the auto-hide delay (2 s), the overlay layer and
the opaque card are fixed, and all four event types are always on.

| Key | Type | Default | Meaning |
|-----|------|---------|---------|
| `enabled` | bool | `true` | Master switch (also stops the lock-key LED polling). |
| `location` | `top` / `top_left` / `top_right` / `bottom` / `bottom_left` / `bottom_right` / `left` / `right` | `top_right` | Screen anchor. `left` / `right` are vertically centred. |
| `orientation` | `auto` / `landscape` / `portrait` | `auto` | `landscape` = horizontal bar, `portrait` = vertical column; `auto` follows the position (portrait at `left` / `right`, landscape elsewhere), like Noctalia. |

## Adding a key

The full recipe is in [the settings app](07-settings-app.md#adding-a-setting-end-to-end).
In short: default + parse in `Config::load()` (per-field getter for `bar.*`,
struct field for top-level objects), read it in the consumer, a row +
handler in `settings/main.cpp`, and a line in this document.
