# 11 — gtkmm primer for this codebase

You do not need to know all of GTK to work on hypr-shell. This page covers
the handful of concepts that appear on nearly every page of the source.
Official references: gtkmm 4 docs at gtkmm.gnome.org, GTK4 docs at
docs.gtk.org/gtk4, and Waybar's source (same stack) for larger patterns.

## Namespaces

| Namespace | Library | What it gives us |
|-----------|---------|------------------|
| `Gtk::` | gtkmm | widgets: `Label`, `Box`, `Button`, `Popover`, `Scale`, `DrawingArea`, event controllers |
| `Gdk::` | gdkmm | display, cursors, frame clock |
| `Glib::` | glibmm | `RefPtr`, `ustring`, `DateTime`, `signal_timeout()`, `Variant`, file helpers |
| `Gio::` | giomm | async file I/O, sockets, DBus, `Subprocess`, `FileMonitor`, `DesktopAppInfo` |
| `sigc::` | libsigc++ | signals and slots (`signal`, `mem_fun`, `connection`) |
| `Cairo::`, `Pango::` | cairomm, pangomm | drawing and text layout in `DrawingArea` |
| raw `g_*`, `gtk_layer_*`, `pa_*`, `adw_*` | C libraries | logging, layer-shell, libpulse, libadwaita |

Including `<gtkmm.h>` pulls in Gtk/Gdk/Glib/Gio. Headers include only what
they need; sources may include the big header.

## Widgets as value members

gtkmm lets a widget be a plain member of your class:

```cpp
class Volume : public Gtk::Box {
    Gtk::Label icon_;      // constructed with the module, destroyed with it
    Gtk::Popover popover_;
};
```

This is the project's default. The alternative is `Gtk::make_managed<T>()`,
which creates a widget whose lifetime is owned by its parent container; use
it for children you create in a loop or at runtime (workspace buttons,
panel rows). Never `new` a widget without either.

Composition: `Gtk::Box` (`append`, `remove`, `set_orientation`),
`Gtk::CenterBox` (start/center/end), `Gtk::Overlay` (stacked children),
`Gtk::Grid`, `Gtk::ScrolledWindow`. Alignment via `set_halign/valign`,
growth via `set_hexpand/vexpand`.

Removing all children of a box:

```cpp
while (auto* child = get_first_child())
    remove(*child);
```

## RefPtr

Objects that are not widgets (`Gio::File`, `Gio::DBus::Proxy`,
`Gtk::CssProvider`, event controllers, `Gio::SocketConnection`) are
reference-counted and handled through `Glib::RefPtr<T>`, a smart pointer
like `std::shared_ptr`. Create with `T::create(...)`, copy freely, use with
`->`. Test for null with `if (ptr)`.

## Signals and slots (sigc++)

A signal is a list of callbacks. Connect a member function:

```cpp
Pulse::get().signal_changed().connect(sigc::mem_fun(*this, &Volume::update));
```

or a lambda:

```cpp
button->signal_clicked().connect([id = entry.id] { Hyprland::get().focus_workspace(id); });
```

`connect()` returns a `sigc::connection`; keep it if you need to
`disconnect()` later (timers, anything whose target may die before the
source). Widgets inherit `sigc::trackable`, so slots bound with `mem_fun`
to a widget are auto-disconnected when that widget is destroyed. Lambdas
capturing `this` are **not** tracked; that's why the destructors disconnect
timers by hand.

Declaring your own signal in a service:

```cpp
sigc::signal<void()>& signal_changed() { return changed_; }
// ...
sigc::signal<void()> changed_;
// fire it:
changed_.emit();
```

## Timers

```cpp
Glib::signal_timeout().connect([this] { update(); return true; }, 1000);   // ms, repeat while true
Glib::signal_timeout().connect_seconds(slot, 60);                          // coarser, battery-friendlier
Glib::signal_timeout().connect_once(slot, 800);                            // fire once
Glib::signal_idle().connect_once(slot);                                    // next main-loop iteration
```

Return `true` from a repeating timer to keep it, `false` to stop.

For animation, use the frame clock instead of a timer:
`add_tick_callback([](const Glib::RefPtr<Gdk::FrameClock>& clock) { ...; return true; })`.
See `Bar::set_hidden()`.

## Async I/O shape

Every giomm async call comes as a pair: `foo_async(args..., callback)` and
`foo_finish(result)` called *inside* the callback, which throws
`Glib::Error` on failure:

```cpp
file->load_contents_async([file](Glib::RefPtr<Gio::AsyncResult>& result) {
    try {
        char* data = nullptr; gsize len = 0;
        file->load_contents_finish(result, data, len);
        // use data; g_free(data);
    } catch (const Glib::Error& e) {
        g_warning("read failed: %s", e.what());
    }
});
```

Always wrap the `_finish` in try/catch. Never call the synchronous variant
(`load_contents`, `Glib::file_get_contents`, `system()`) from the shell
except for the config's first read.

## Event controllers (input)

GTK4 has no `button-press-event`; you attach controllers:

```cpp
auto click = Gtk::GestureClick::create();
click->set_button(GDK_BUTTON_SECONDARY);          // right click only (omit for left)
click->signal_released().connect([this](int n_press, double x, double y) { ... });
add_controller(click);

auto scroll = Gtk::EventControllerScroll::create();
scroll->set_flags(Gtk::EventControllerScroll::Flags::VERTICAL);
scroll->signal_scroll().connect(sigc::mem_fun(*this, &Foo::on_scroll), false);  // false = run before default handler
add_controller(scroll);

auto motion = Gtk::EventControllerMotion::create();
motion->signal_enter().connect([this](double, double) { ... });
motion->signal_leave().connect([this] { ... });
add_controller(motion);
```

Touchpads deliver many small scroll deltas; accumulate to ±1.0 before
acting (`Workspaces::on_scroll`).

## CSS from code

```cpp
add_css_class("module");
remove_css_class("active");
if (cond) add_css_class("charging"); else remove_css_class("charging");
```

Toggling classes and letting `style.css` do the rest is the preferred way to
change appearance. Avoid setting colours or fonts from C++.

## Strings

`Glib::ustring` is a UTF-8 string that GTK APIs expect; it converts to and
from `std::string` implicitly. `Glib::ustring::compose("%1%%", pct)` is a
type-safe `printf` (`%%` for a literal percent). For locale-independent
number formatting (CSS, Lua) use `g_ascii_dtostr`, never `std::to_string`
on doubles.

## Calling C libraries from gtkmm

Every gtkmm object wraps a C object reachable with `gobj()`. Cast it to the
C type the library wants:

```cpp
gtk_layer_init_for_window(GTK_WINDOW(gobj()));
```

Look-ups like `gtk_layer_set_margin(window, edge, px)` are then plain C
calls. Memory rules: if a C function returns something you own
(`gchar*`), free it with `g_free`.

## Logging

```cpp
g_message("...");   // always shown
g_warning("...");   // always shown, use for real problems
g_debug("...");     // only with G_MESSAGES_DEBUG=all
```

All take printf-style format strings; pass `std::string` as `.c_str()`.

## JSON (nlohmann)

```cpp
using json = nlohmann::json;
const json j = json::parse(text, nullptr, /*allow_exceptions=*/false);  // returns discarded on error
if (!j.is_object()) ...
int n = j.value("windows", 0);                       // default if missing / wrong type
if (auto it = j.find("clock"); it != j.end() && it->is_object()) ...
for (const auto& ws : j) ...                         // arrays
for (const auto& [key, val] : j.items()) ...         // objects
std::string out = j.dump(2);                          // pretty print
```

Wrap parsing of untrusted input (Hyprland replies) in `try/catch (const std::exception&)`.

## The libadwaita side (settings app)

No gtkmm there: it's the GObject C API. The idioms:

```cpp
GtkWidget* row = adw_switch_row_new();
adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), "Title");   // cast macros do runtime type checks
g_signal_connect(row, "notify::active", G_CALLBACK(handler), user_data);
g_object_set_data(G_OBJECT(row), "key", ptr);                        // attach arbitrary data
auto* p = static_cast<T*>(g_object_get_data(G_OBJECT(row), "key"));  // read it back
gtk_widget_set_visible(row, FALSE);
```

A handler is a free function with the signature the signal specifies
(`notify::*` handlers get `(GObject*, GParamSpec*, gpointer)`). Lambdas work
only if they capture nothing, with a leading `+` to make them function
pointers.
