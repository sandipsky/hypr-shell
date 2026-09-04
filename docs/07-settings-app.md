# 07 — The settings app

`hypr-shell-settings` is a GNOME-Settings-style window that edits
`~/.config/hypr-shell/config.json`. Every widget change is written to disk
immediately; the shell hot-reloads the file. There is no Apply button and no
IPC.

Source: `src/settings/main.cpp`, one file. It is plain C++ over the
**libadwaita C API** (no gtkmm), because libadwaita has no official C++
bindings. If you know GTK from C or Python the code will look familiar.

## How it is organised

```
kModules[]            table: key, title, subtitle, default section
module_index(key)     row index for a module key (used to attach cog buttons)
struct Settings       parsed JSON root + pointer to every row widget + loading flag
load() / save()       file I/O; save() dumps root with 2-space indent
bar_object(s)         returns root["bar"], creating it as an object if needed
<sub>_object(s)       same for bar.workspaces, bar.clock, bar.battery, bar.active_window,
                      bar.bluetooth, bar.notifications; nd_object / launcher_object /
                      wp_object ... for the top-level notifications / launcher /
                      wallpaper ... objects
populate(s)           loading = true; read root → set every widget; loading = false
on_*_changed(...)     handlers: if (loading) return; write key; save()
resolve_layout /      the bar.layout editor (up/down buttons, section dropdown)
rebuild_layout_rows
rebuild_rule_rows     notification rules list (add/edit/delete via AdwAlertDialog)
on_activate()         builds every page, calls populate(), connects signals, wires cogs,
                      assembles the sidebar split view
```

### Window layout

```
AdwNavigationSplitView
 ├─ sidebar: AdwToolbarView → GtkListBox (.navigation-sidebar): Bar / Launcher / Notifications
 └─ content: GtkStack
     ├─ "bar" page: AdwNavigationView
     │    ├─ main Bar page (Bar group, Modules group, Layout groups)
     │    └─ module subpages by tag: workspaces, clock, active_window, battery,
     │       bluetooth, notifications  (opened by the cog on each module row)
     ├─ "launcher_page": AdwPreferencesPage for the top-level launcher object
     └─ "notifications_page": AdwPreferencesPage for the top-level notifications object
```

Selecting a sidebar row switches the stack child. `HS_SETTINGS_PAGE` accepts
either a module subpage tag or `launcher_page` / `notifications_page`.

The `loading` flag is the one subtle part. Setting a switch's state
programmatically fires the same `notify::active` signal as a user click, so
`populate()` sets `s->loading = true` first and every handler returns early
while it's set. Forgetting the check makes the app rewrite the file on
startup with whatever defaults the widgets happen to have.

Signals are connected *after* `populate()` for the same reason, with two
exceptions (module toggles and battery cards) that are connected in their
build loops and rely on the flag instead. Either works; the flag is what
protects you.

## libadwaita widgets used

| Widget | Purpose | Read | Signal |
|--------|---------|------|--------|
| `AdwPreferencesPage` + `AdwPreferencesGroup` | The page scaffolding; groups are the titled cards | | |
| `AdwSwitchRow` | On/off | `adw_switch_row_get_active()` | `notify::active` |
| `AdwComboRow` | Pick one of N strings (`GtkStringList` model) | `adw_combo_row_get_selected()` → index into a `k*Keys[]` table | `notify::selected` |
| `AdwSpinRow` | Integer with range | `adw_spin_row_get_value()` | `notify::value` |
| `AdwEntryRow` | Free text | `gtk_editable_get_text()` | `changed` |
| `AdwActionRow` + suffix widget | Anything else, e.g. the opacity `GtkScale` | | widget-specific |
| `AdwNavigationView` + `AdwNavigationPage` | Main page + subpages, pushed by tag | | |
| `AdwNavigationSplitView` + `GtkListBox` + `GtkStack` | Sidebar with one stack child per section | | `row-selected` |
| `AdwToolbarView` + `AdwHeaderBar` | Title bar for each page | | |
| `AdwAlertDialog` | Modal editor (notification rules) | | `response` |
| `GtkFileDialog` | Pick a sound file into an entry row | `gtk_file_dialog_open` (async) | |

Needs libadwaita ≥ 1.4 (`AdwSwitchRow`, `AdwSpinRow`). Avoid the deprecated
`AdwPreferencesWindow`.

Passing data to C callbacks: store it on the widget with
`g_object_set_data(G_OBJECT(w), "module-key", ptr)` and read it back in the
handler. The `Settings*` goes in the `gpointer data` argument of
`g_signal_connect`.

## Adding a setting, end to end

Say you want `bar.clock.show_seconds` (bool, default false). The checklist,
in this order, all in one change:

### Shell side

1. `src/services/config.hpp`: getter `bool clock_show_seconds() const` and
   member `bool clock_show_seconds_ = false;`.
2. `src/services/config.cpp`: reset the default at the top of `load()`; in
   the `bar.find("clock")` block add
   `clock_show_seconds_ = it->value("show_seconds", false);`.
3. The consumer (`clock.cpp`): read the getter in `update()`. The module
   already connects to `signal_changed()`, so live apply is automatic.

### Settings side

4. `struct Settings`: `AdwSwitchRow* clock_seconds = nullptr;`
5. Handler:

   ```cpp
   void on_clock_seconds_toggled(GObject*, GParamSpec*, gpointer data) {
       auto* s = static_cast<Settings*>(data);
       if (s->loading)
           return;
       clock_object(s)["show_seconds"] = adw_switch_row_get_active(s->clock_seconds) != FALSE;
       save(s);
   }
   ```

6. `populate()`: read the value in the existing clock `try` block and
   `adw_switch_row_set_active(s->clock_seconds, value);`.
7. `on_activate()`: create the row in the Clock subpage's group, store the
   pointer, and `g_signal_connect(row, "notify::active", G_CALLBACK(on_clock_seconds_toggled), s);`
   after `populate(s)`.

### Documentation

8. `docs/04-config-reference.md`: the new key.
9. `CLAUDE.md`: schema note in the decision log if it's a notable option.

Test: toggle the switch, `cat` the config, watch the bar. Then delete the
key from the file by hand and confirm both programs fall back to the default.

## Adding a subpage for a module

Modules with several options get their own page, opened by a cog button on
their row in the Modules group. Copy the Clock subpage block in
`on_activate()`: a `preferences_page` with groups, wrapped in an
`AdwToolbarView`, added to `nav` with `adw_navigation_page_new_with_tag(view,
"Title", "tag")`. Then add the cog:

```cpp
GtkWidget* cog = gtk_button_new_from_icon_name("emblem-system-symbolic");
gtk_widget_add_css_class(cog, "flat");
gtk_widget_set_valign(cog, GTK_ALIGN_CENTER);
g_signal_connect(cog, "clicked",
                 G_CALLBACK(+[](GtkButton*, gpointer nav_ptr) {
                     adw_navigation_view_push_by_tag(ADW_NAVIGATION_VIEW(nav_ptr), "tag");
                 }),
                 nav);
adw_action_row_add_suffix(ADW_ACTION_ROW(s->modules[module_index("key")]), cog);
```

## Adding a sidebar page

For options that are not about the bar, copy the Launcher or Lock screen
page block in `on_activate()`: build an
`AdwPreferencesPage`, wrap it in an `AdwToolbarView`, `gtk_stack_add_named()`
it with a `<name>_page` tag, append a label row to the sidebar list box, and
extend the `row-selected` handler's index → page mapping. Its config lives in
a new top-level object with a `<name>_object(s)` helper, mirrored by a struct
in `Config` (see `Config::Launcher` for the smallest example).

`HS_SETTINGS_PAGE=tag ./build/hypr-shell-settings` opens the page straight
away for testing and screenshots.

The Wallpaper page (`wp_*` in `main.cpp`) is the richest example: besides
rows it holds a `GtkFlowBox` grid of thumbnail tiles (`wp_make_tile`,
`wp_rebuild_grid`), decodes thumbnails asynchronously into a PNG cache
(`wp_load_thumbnail`), watches the wallpaper folder and the shell's state
file with `GFileMonitor`s, and styles its tiles through a small
`GtkCssProvider` loaded in `on_activate()` — the only custom CSS in the
settings binary. Anything a preferences group can hold is fair game; a group
accepts arbitrary widgets, not just rows.

The `+[](...)` in front of the lambda forces conversion to a plain function
pointer, which `G_CALLBACK` needs; only capture-less lambdas can do that.

## Conditional rows

Some rows only make sense given another row's value (auto-hide toggles only
in auto-hide mode; the two title rows only when the title is shown). Pattern:
a small `update_*_visibility(s)` function that calls
`gtk_widget_set_visible()` on the dependent rows, invoked from the parent
row's handler *before* the `loading` check (so it also runs during
`populate()`).

## Layout editor

Each section group lists its modules as `AdwActionRow`s with three suffix
widgets: up, down, and a Left/Center/Right dropdown. Moving rewrites
`bar.layout` fully (all three arrays) and rebuilds the rows. The rebuild is
deferred with `g_idle_add_once()` because destroying the widget whose signal
handler is currently running would crash. `resolve_layout()` must stay in
sync with the shell's `Config::load()` rules; if you change one, change both.

## Style and behaviour conventions

- Titles in sentence case, subtitles are one short sentence ending with a
  period only when they are sentences.
- Store enum-like options as lowercase snake_case strings (`"auto_hide"`,
  `"appname"`), never as indices, so hand-edited configs stay readable.
- Numbers go in as numbers (`0.88`, not `"0.88"`); percents are stored as
  fractions 0..1 like Noctalia.
- Every change saves. If a control fires very often (slider drag), that is
  still fine: the shell coalesces reloads via `CHANGES_DONE_HINT`.
