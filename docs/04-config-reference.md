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
                       "format_horizontal": "%H:%M %a, %b %d", "format_vertical": "%H %M" },
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
`launcher`, `workspaces`, `active_window`, `network`, `bluetooth`, `volume`,
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

## `bar.notifications` (the bell module)

| Key | Type | Default | Meaning |
|-----|------|---------|---------|
| `show_unread_badge` | bool | `true` | Dot for entries newer than the last panel open. |
| `hide_when_zero` | bool | `false` | Hide the module when history is empty. |
| `hide_when_zero_unread` | bool | `false` | Hide the module when nothing is unread. |

## `notifications` (top level: daemon + popups)

Exposed as `Config::get().notifications()` (struct `Config::Notifications`).

| Key | Type | Default | Meaning |
|-----|------|---------|---------|
| `enabled` | bool | `true` | `false` releases the `org.freedesktop.Notifications` bus name so another daemon can take it. |
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

Pinned apps are runtime state in `~/.cache/hypr-shell/pinned_apps.json`.

## Adding a key

The full recipe is in [the settings app](07-settings-app.md#adding-a-setting-end-to-end).
In short: default + parse in `Config::load()` (per-field getter for `bar.*`,
struct field for top-level objects), read it in the consumer, a row +
handler in `settings/main.cpp`, and a line in this document.
