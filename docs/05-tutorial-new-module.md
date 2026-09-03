# 05 — Tutorial: add a bar module end to end

We will add a **system load** module that shows the 1-minute load average
from `/proc/loadavg`, refreshes every few seconds, has one setting (the
refresh interval), and gets a row in the settings app. It is small enough to
finish in one sitting and touches every file a real module touches.

Files we will create or edit:

```
src/bar/modules/load.hpp        new
src/bar/modules/load.cpp        new
meson.build                     add the .cpp
src/services/config.hpp         getter + default for bar.load.interval_seconds
src/services/config.cpp         parse it; register "load" as a known module
src/bar/bar.hpp                 Load load_; member
src/bar/bar.cpp                 module_widget() branch
data/style.css                  a .load rule
src/settings/main.cpp           module row + subpage with a spin row
docs/04-config-reference.md     document the key
CLAUDE.md                       roadmap tick / decision log line
```

Build after every step (`meson compile -C build`). Compiler errors are
easier to fix one at a time.

## Step 1 — the widget

A module is a GTK widget. Simple text modules subclass `Gtk::Label`; modules
with several children subclass `Gtk::Box`. Ours is a label.

`src/bar/modules/load.hpp`:

```cpp
#pragma once

#include <gtkmm.h>

namespace hyprshell {

// 1-minute load average from /proc/loadavg, refreshed on a timer.
class Load : public Gtk::Label {
public:
    Load();
    ~Load() override;

private:
    void update();    // read the file (async) and set the text
    void schedule();  // (re)start the timer from the configured interval

    sigc::connection timer_;
};

} // namespace hyprshell
```

`src/bar/modules/load.cpp`:

```cpp
#include "bar/modules/load.hpp"

#include "services/config.hpp"

#include <giomm.h>

namespace hyprshell {

Load::Load() {
    add_css_class("module");
    add_css_class("load");
    set_tooltip_text("System load (1 min average)");

    // re-read the interval whenever config.json changes
    Config::get().signal_changed().connect(sigc::mem_fun(*this, &Load::schedule));
    schedule();
}

Load::~Load() {
    timer_.disconnect(); // never let a timer fire into a destroyed widget
}

void Load::schedule() {
    timer_.disconnect();
    update();
    timer_ = Glib::signal_timeout().connect_seconds(
        [this] {
            update();
            return true; // keep firing
        },
        Config::get().load_interval_seconds());
}

void Load::update() {
    // Async read: never block the main loop, even for a tiny file.
    auto file = Gio::File::create_for_path("/proc/loadavg");
    file->load_contents_async([this, file](Glib::RefPtr<Gio::AsyncResult>& result) {
        try {
            char* contents = nullptr;
            gsize length = 0;
            file->load_contents_finish(result, contents, length);
            std::string text(contents, length);
            g_free(contents);
            // "0.52 0.48 0.40 1/812 12345" — first token is the 1-min average
            set_text(text.substr(0, text.find(' ')));
            set_visible(true);
        } catch (const Glib::Error& e) {
            g_warning("load: %s", e.what());
            set_visible(false); // degrade gracefully
        }
    });
}

} // namespace hyprshell
```

Things to notice, because every module does them:

- `add_css_class("module")` plus one class named after the module.
- Subscribe to `Config::get().signal_changed()` if any setting affects you.
- I/O is asynchronous: `load_contents_async` returns immediately and the
  lambda runs later on the main loop. `[this, file]` keeps the `Gio::File`
  alive until the callback runs.
- Hide yourself (`set_visible(false)`) if your data source is missing rather
  than showing garbage.
- Disconnect timers in the destructor.

## Step 2 — tell meson about the file

In `meson.build`, add to the `sources = files(...)` list:

```meson
  'src/bar/modules/load.cpp',
```

Meson does not glob; a forgotten entry gives "undefined reference" link
errors for `hyprshell::Load::Load()`.

## Step 3 — the config key

We want `bar.load.interval_seconds` (default 5, range 1..60).

`src/services/config.hpp`, in the public section next to the clock getters:

```cpp
    // bar.load.interval_seconds: how often the load module re-reads /proc/loadavg
    int load_interval_seconds() const { return load_interval_seconds_; }
```

and in the private section:

```cpp
    int load_interval_seconds_ = 5;
```

`src/services/config.cpp`, three edits:

1. Register the module and its default section in `kKnownModules` so the
   layout resolver knows it. Order in this table is the fallback order:

   ```cpp
   constexpr std::pair<const char*, Config::BarSection> kKnownModules[] = {
       {"launcher", Config::BarSection::Left},
       {"workspaces", Config::BarSection::Left},
       {"active_window", Config::BarSection::Center},
       {"network", Config::BarSection::Right},
       {"bluetooth", Config::BarSection::Right},
       {"volume", Config::BarSection::Right},
       {"battery", Config::BarSection::Right},
       {"load", Config::BarSection::Right},      // new
       {"notifications", Config::BarSection::Right},
       {"clock", Config::BarSection::Right},
   };
   ```

2. Reset the default at the top of `Config::load()` with the others:

   ```cpp
   load_interval_seconds_ = 5;
   ```

   This matters: `load()` runs on every reload, and a key that was removed
   from the file must go back to its default.

3. Parse it, next to the `bar.find("clock")` block:

   ```cpp
   if (auto it = bar.find("load"); it != bar.end() && it->is_object())
       load_interval_seconds_ = std::clamp(it->value("interval_seconds", 5), 1, 60);
   ```

   `it->value(key, default)` returns the default when the key is missing or
   has the wrong type. `std::clamp` enforces the range.

## Step 4 — put it in the bar

`src/bar/bar.hpp`: include the header and add a member after `battery_`
(members are constructed in declaration order; order doesn't matter here but
keep it tidy):

```cpp
#include "bar/modules/load.hpp"
// ...
    Battery battery_;
    Load load_;
    Clock clock_;
```

`src/bar/bar.cpp`, in `Bar::module_widget()`:

```cpp
    if (name == "load")
        return &load_;
```

That is all the bar needs. `apply_config()` already iterates `bar.layout`
and asks `module_widget()` for each name, so the module now appears at the
end of the right section for everyone (unless they disable it).

## Step 5 — style

`data/style.css`:

```css
/* system load */
.load {
  font-family: monospace;
  font-size: 13px;
  color: #a6adc8;
}
```

## Step 6 — the settings app

`src/settings/main.cpp`. First the module row. Add to `kModules[]` in the
position you want it listed (the order of this table is the order of the
switches in the Modules group; keep it matching `kKnownModules`):

```cpp
    {"load",          "System load",   "1-minute load average",        2},
```

The enable/disable switch, the layout up/down rows and the section dropdown
are all table-driven, so the new module already gets them. Cog buttons find
their row with `module_index("key")`, so inserting anywhere is safe.

Now the interval setting. Follow the clock subpage as a template:

1. Add a pointer to `struct Settings`:

   ```cpp
   AdwSpinRow* load_interval = nullptr;
   ```

2. Add a sub-object helper and a handler next to `clock_object` /
   `on_clock_fdow_changed`:

   ```cpp
   json& load_object(Settings* s) {
       json& bar = bar_object(s);
       if (!bar["load"].is_object())
           bar["load"] = json::object();
       return bar["load"];
   }

   void on_load_interval_changed(GObject*, GParamSpec*, gpointer data) {
       auto* s = static_cast<Settings*>(data);
       if (s->loading)
           return;
       load_object(s)["interval_seconds"] =
           static_cast<int>(adw_spin_row_get_value(s->load_interval));
       save(s);
   }
   ```

3. In `populate()`, read the current value and set the widget (inside the
   part of the function where `s->loading` is true):

   ```cpp
   int load_interval = 5;
   try {
       const json load = s->root.value("bar", json::object()).value("load", json::object());
       load_interval = std::clamp(load.value("interval_seconds", 5), 1, 60);
   } catch (const json::exception&) {
   }
   adw_spin_row_set_value(s->load_interval, load_interval);
   ```

4. In `on_activate()`, build a subpage after the Battery subpage:

   ```cpp
   GtkWidget* load_page = adw_preferences_page_new();
   GtkWidget* load_group = adw_preferences_group_new();
   adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(load_group), "System load");
   GtkWidget* load_row = adw_spin_row_new_with_range(1, 60, 1);
   adw_preferences_row_set_title(ADW_PREFERENCES_ROW(load_row), "Refresh interval");
   adw_action_row_set_subtitle(ADW_ACTION_ROW(load_row), "Seconds between updates");
   s->load_interval = ADW_SPIN_ROW(load_row);
   adw_preferences_group_add(ADW_PREFERENCES_GROUP(load_group), load_row);
   adw_preferences_page_add(ADW_PREFERENCES_PAGE(load_page),
                            ADW_PREFERENCES_GROUP(load_group));
   ```

   then connect the signal **after** `populate(s)` with the other
   `g_signal_connect` calls:

   ```cpp
   g_signal_connect(load_row, "notify::value", G_CALLBACK(on_load_interval_changed), s);
   ```

   and register the page in the navigation view with the others:

   ```cpp
   GtkWidget* load_view = adw_toolbar_view_new();
   adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(load_view), adw_header_bar_new());
   adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(load_view), load_page);
   adw_navigation_view_add(ADW_NAVIGATION_VIEW(nav),
                           adw_navigation_page_new_with_tag(load_view, "System load", "load"));
   ```

   and finally a cog on the module row, looked up by key:

   ```cpp
   GtkWidget* load_cog = gtk_button_new_from_icon_name("emblem-system-symbolic");
   gtk_widget_add_css_class(load_cog, "flat");
   gtk_widget_set_valign(load_cog, GTK_ALIGN_CENTER);
   gtk_widget_set_tooltip_text(load_cog, "System load settings");
   g_signal_connect(load_cog, "clicked",
                    G_CALLBACK(+[](GtkButton*, gpointer nav_ptr) {
                        adw_navigation_view_push_by_tag(ADW_NAVIGATION_VIEW(nav_ptr), "load");
                    }),
                    nav);
   adw_action_row_add_suffix(ADW_ACTION_ROW(s->modules[module_index("load")]), load_cog);
   ```

Build, run `./build/hypr-shell-settings`, and `HS_SETTINGS_PAGE=load` should
open your page directly.

## Step 7 — test

```sh
meson compile -C build && pkill -x hypr-shell; ./build/hypr-shell
```

- The number appears at the right of the bar. Run `yes > /dev/null &` for a
  few seconds and watch it climb (then `kill %1`).
- Open the settings app, change the interval, confirm the bar keeps working
  (the timer restarts on config change).
- Disable the module in settings: it disappears. Move it to the center: it
  moves.
- `cat ~/.config/hypr-shell/config.json` shows `"load": {"interval_seconds": N}`
  under `bar`, and the other keys are untouched.
- Write nonsense into the config (`"interval_seconds": "abc"`) and check the
  bar falls back to 5 seconds without complaint.

## Step 8 — document

- `docs/04-config-reference.md`: add a `bar.load` table and the module name
  to the `bar.modules` list.
- `CLAUDE.md`: mention `load` in the module list of the Layout section; if
  it belongs to a roadmap item (system stats is Phase 2), note the progress
  there. Add a decision-log line only if you made a non-obvious choice.
- `docs/03-code-tour.md`: add a row to the modules table.

## Variations

- **Module with an icon and text**: subclass `Gtk::Box`, `append()` a
  `Gtk::Label icon_` with class `icon` and a text label. See `network.cpp`.
- **Module driven by a backend**: write a service first
  ([services](06-services-and-async-io.md)), then the module is just
  `Service::get().signal_changed().connect(...)` + `update()`.
- **Module with a click panel**: see [panels](08-panels-and-popovers.md).
- **Vertical bars**: if your module lays children out horizontally, flip
  the orientation in `update()` using `Config::get().bar_vertical()`, like
  `Workspaces::rebuild()` does.
