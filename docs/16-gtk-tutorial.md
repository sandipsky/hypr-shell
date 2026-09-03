# 16 — GTK4 tutorial for hypr-shell

GTK is the toolkit that draws the bar, handles clicks, and runs the event
loop. This tutorial teaches GTK4 through gtkmm (the C++ binding) in the way
hypr-shell uses it, from a standalone hello-world up to layer-shell windows,
custom drawing, DBus, and the libadwaita C API used by the settings app.

Official references: docs.gtk.org/gtk4 (C API, with the definitive widget
docs), gtkmm.gnome.org/en/documentation (C++ binding), and the GTK
Inspector (`GTK_DEBUG=interactive`) which shows you the live widget tree.
gtkmm method names are the C names minus the `gtk_widget_` prefix, in
`snake_case`, so you can read the C docs and translate:
`gtk_widget_set_visible(w, TRUE)` → `w.set_visible(true)`.

---

## 1. The mental model

- **Widgets form a tree.** A window contains a box, the box contains labels
  and buttons. Every widget has one parent (except the window). Layout is
  computed by the containers; you don't position things by pixel.
- **The main loop owns the thread.** `app->run()` enters a loop that waits
  for events (input, timers, I/O readiness), dispatches them to your
  callbacks, and redraws what changed. Your code only ever runs *inside*
  a callback. If a callback takes 500 ms, the whole UI freezes for 500 ms.
  That is why all I/O is asynchronous.
- **Signals connect events to code.** `button.signal_clicked().connect(...)`.
- **CSS styles everything.** Colours, fonts, padding, borders live in CSS
  keyed by node names and classes, not in C++.
- **One thread.** GTK is not thread-safe. Never touch a widget from another
  thread. This project has no threads at all.

---

## 2. Hello, layer shell

A minimal standalone program, to see the pieces outside the project. Save as
`hello.cpp`:

```cpp
#include <gtkmm.h>
#include <gtk4-layer-shell.h>

class Hello : public Gtk::Application {
public:
    static Glib::RefPtr<Hello> create() { return Glib::make_refptr_for_instance(new Hello()); }

protected:
    Hello() : Gtk::Application("dev.example.Hello") {}

    void on_activate() override {
        auto* win = new Gtk::Window();                    // owned by the application below
        win->set_decorated(false);
        gtk_layer_init_for_window(GTK_WINDOW(win->gobj()));   // before present()
        gtk_layer_set_anchor(GTK_WINDOW(win->gobj()), GTK_LAYER_SHELL_EDGE_TOP, true);
        gtk_layer_set_anchor(GTK_WINDOW(win->gobj()), GTK_LAYER_SHELL_EDGE_LEFT, true);
        gtk_layer_set_anchor(GTK_WINDOW(win->gobj()), GTK_LAYER_SHELL_EDGE_RIGHT, true);
        gtk_layer_auto_exclusive_zone_enable(GTK_WINDOW(win->gobj()));

        auto* label = Gtk::make_managed<Gtk::Label>("hello from a layer surface");
        label->set_margin(8);
        win->set_child(*label);

        add_window(*win);
        win->present();
    }
};

int main(int argc, char** argv) { return Hello::create()->run(argc, argv); }
```

Compile and run inside Hyprland:

```sh
g++ -std=c++20 hello.cpp -o hello $(pkg-config --cflags --libs gtkmm-4.0 gtk4-layer-shell-0)
./hello
```

A strip appears at the top of the screen and windows tile below it. This is
the whole of `src/main.cpp` + `src/bar/bar.cpp` in miniature.
`Gtk::Application` handles startup and uniqueness; `on_activate()` builds the
window; layer-shell turns it into a bar.

---

## 3. Widgets you will use

### Labels

```cpp
Gtk::Label label_("initial text");
label_.set_text("new text");                 // plain
label_.set_markup("<b>bold</b> text");       // Pango markup (escape user text first!)
label_.set_ellipsize(Pango::EllipsizeMode::END);   // "long tit…"
label_.set_max_width_chars(70);
label_.set_width_chars(4);                   // reserve space so the width doesn't jump
label_.set_xalign(1.0);                      // right-align inside its allocation
label_.set_justify(Gtk::Justification::CENTER);    // multi-line alignment
label_.set_tooltip_text("hover text");  /  set_has_tooltip(false)
```

Icons in this project are labels with a single glyph and an `icon` CSS
class that picks the icon font. `Gtk::Image` is for real images/icon names:
`icon_.set_from_icon_name("application-x-executable-symbolic")` or
`icon_.set(gicon)`; `set_pixel_size(16)`.

### Containers

```cpp
Gtk::Box box{Gtk::Orientation::HORIZONTAL, 6};   // spacing 6px
box.append(child);  box.prepend(child);  box.remove(child);
box.set_orientation(Gtk::Orientation::VERTICAL); // flip at runtime (vertical bars)
box.get_first_child(), child->get_next_sibling() // walk children

Gtk::CenterBox cb;                     // three slots, the middle stays centred
cb.set_start_widget(a); cb.set_center_widget(b); cb.set_end_widget(c);

Gtk::Overlay ov;                       // stack widgets on top of each other
ov.set_child(base); ov.add_overlay(top);

Gtk::Grid grid;                        // rows × columns (calendar)
grid.attach(child, column, row, width = 1, height = 1);

Gtk::ScrolledWindow sw;                // scrollable viewport (network list)
sw.set_child(list); sw.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
sw.set_vexpand(true);
```

Removing all children:

```cpp
while (auto* child = box.get_first_child())
    box.remove(*child);
```

Note: `remove()` on a managed widget destroys it; on a value-member widget
it just unparents it (the member lives on). `Bar::apply_config()` relies on
the second behaviour to re-arrange modules.

### Controls

```cpp
Gtk::Button btn("text");   btn.set_child(widget);   btn.signal_clicked().connect(...)
Gtk::Scale scale;  scale.set_range(0, 100);  scale.set_increments(1, 5);
                   scale.set_draw_value(false);  scale.get_value();  scale.set_value(v);
                   scale.signal_value_changed().connect(...)
Gtk::ProgressBar bar;  bar.set_fraction(0.7);
Gtk::Switch sw;  sw.get_active();  sw.set_active(b);  sw.property_active().signal_changed()
Gtk::Entry entry;  entry.get_text();  entry.set_visibility(false) /* password */;
                   entry.signal_activate() /* Enter */
Gtk::DrawingArea area;  area.set_draw_func(...);  area.set_content_width(w); area.queue_draw();
```

### Popover

```cpp
Gtk::Popover popover_;
popover_.set_child(*panel);
popover_.set_parent(anchor_widget);     // a Label or Overlay, never the module Box
popover_.set_has_arrow(false);
popover_.set_position(Gtk::PositionType::BOTTOM);
popover_.popup();  popover_.popdown();  popover_.get_mapped()
// destructor of the owner: popover_.unparent();
```

Full treatment in [panels](08-panels-and-popovers.md).

---

## 4. Layout: how sizes are decided

GTK asks each widget for its *minimum* and *natural* size, then the parent
allocates space. You influence it with:

```cpp
w.set_halign(Gtk::Align::START | CENTER | END | FILL);
w.set_valign(Gtk::Align::CENTER);
w.set_hexpand(true);           // take extra horizontal space
w.set_vexpand(true);
w.set_size_request(330, -1);   // minimum width 330, height natural
w.set_margin_top(4);  w.set_margin(8);   // all sides
```

CSS `padding`, `margin`, `min-width`, `min-height` also feed into the size
request, which is why the bar's thickness is set in CSS rather than code.

A widget with `set_visible(false)` takes no space. `set_opacity(0)` keeps
its space (the active-window "transparent" mode).

---

## 5. Signals and input

### Widget signals

```cpp
button.signal_clicked().connect([this] { ... });
scale.signal_value_changed().connect([this] { ... });
entry.signal_activate().connect([this] { submit(); });
```

`connect()` returns a `sigc::connection`. Store it if you'll need to
`disconnect()`.

### Event controllers

GTK4 replaced per-widget event signals with attachable controllers:

```cpp
auto click = Gtk::GestureClick::create();
click->set_button(GDK_BUTTON_SECONDARY);              // omit for primary/left
click->signal_released().connect([this](int n_press, double x, double y) { ... });
add_controller(click);

auto scroll = Gtk::EventControllerScroll::create();
scroll->set_flags(Gtk::EventControllerScroll::Flags::VERTICAL);
scroll->signal_scroll().connect([this](double dx, double dy) { ...; return true; }, false);
add_controller(scroll);

auto motion = Gtk::EventControllerMotion::create();
motion->signal_enter().connect([this](double, double) { hover(true); });
motion->signal_leave().connect([this] { hover(false); });
add_controller(motion);

auto key = Gtk::EventControllerKey::create();        // for future panels/launcher
key->signal_key_pressed().connect([this](guint keyval, guint, Gdk::ModifierType) {
    if (keyval == GDK_KEY_Escape) { close(); return true; }
    return false;
}, false);
add_controller(key);
```

The trailing `false` on `signal_scroll` / `signal_key_pressed` means "run
my handler *before* the default one" so returning `true` can stop it.

Cursor: `set_cursor(Gdk::Cursor::create("pointer"))` gives the hand cursor
over clickable modules.

---

## 6. The main loop: timers, idle, animation

```cpp
// repeat every 1000 ms while the lambda returns true
auto conn = Glib::signal_timeout().connect([this] { update(); return true; }, 1000);
// coarse seconds timer (lets the kernel batch wakeups — better for battery)
Glib::signal_timeout().connect_seconds([this] { tick(); return false; }, 60);
// once, after a delay
Glib::signal_timeout().connect_once([this] { popover_.popup(); }, 800);
// as soon as the loop is idle (defer work out of the current handler)
Glib::signal_idle().connect_once([this] { rebuild(); });
```

Disconnect in destructors. `conn.disconnect()` is safe to call on an
already-finished timer.

Animation should follow the display refresh, not a timer:

```cpp
add_tick_callback([this](const Glib::RefPtr<Gdk::FrameClock>& clock) {
    gint64 now_us = clock->get_frame_time();
    // compute progress 0..1, update, return true to keep animating
    return progress < 1.0;
});
```

`Bar::set_hidden()` is the worked example (with easing functions).

Async I/O is part of the same loop; see [services](06-services-and-async-io.md).
The rule bears repeating: **no blocking calls in callbacks**. No `sleep`,
`system()`, synchronous file reads of anything that could stall, or
synchronous DBus calls.

---

## 7. CSS

### Loading

```cpp
auto provider = Gtk::CssProvider::create();
provider->load_from_resource("/dev/hyprshell/Shell/style.css");   // embedded
provider->load_from_path(path);                                    // file
provider->load_from_data(".bar-inner { background-color: alpha(#11111b, 0.5); }");
Gtk::StyleProvider::add_provider_for_display(Gdk::Display::get_default(), provider,
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
```

Higher priority wins: `APPLICATION` < `APPLICATION + 1` < `USER`. The
project stacks three providers this way (theme, opacity rule, user file).

### Selecting

Selectors combine **node names** (the widget's CSS name: `window`, `label`,
`button`, `popover`, `scale`, `switch`, `entry`) with **classes** you add:

```cpp
add_css_class("module");  remove_css_class("active");  has_css_class("x")
```

```css
.workspaces button.active { background: #e0eaff; }   /* class on a button node */
popover.calendar-popover > contents { border-radius: 16px; }  /* sub-node */
scale.bp-slider trough { min-height: 6px; }
window.bar.left .module { padding: 8px 0; }           /* state class on the window */
```

Sub-nodes (`contents`, `trough`, `highlight`, `slider`, `progress`) are in
each widget's C documentation under "CSS nodes", or visible in the
Inspector.

Useful functions: `alpha(color, 0.5)`, `mix(c1, c2, 0.5)`, `shade(c, 1.2)`.
Sizes: `min-width`/`min-height`, `padding`, `margin`, `border`,
`border-radius`, `font-family`, `font-size`, `font-weight`, `color`,
`background`, `box-shadow`, `opacity`, `transition`.

### Debugging CSS

Run with `GTK_DEBUG=interactive`, pick the widget with the crosshair, open
the "CSS nodes" tab to see the node tree and which rules match; the "CSS"
tab lets you type rules live. Parse errors print to the terminal with
`file:line:col`.

---

## 8. Drawing with cairo and Pango

For anything widgets can't do (the seconds ring, rotated text):

```cpp
Gtk::DrawingArea ring_;
ring_.set_content_width(48); ring_.set_content_height(48);
ring_.set_draw_func([this](const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
    cr->set_line_width(3);
    cr->set_source_rgba(0.75, 0.76, 1.0, 1.0);
    cr->arc(w / 2.0, h / 2.0, w / 2.0 - 2, -G_PI / 2, -G_PI / 2 + 2 * G_PI * fraction_);
    cr->stroke();
});
// when data changes:
ring_.queue_draw();
```

Text:

```cpp
auto layout = area.create_pango_layout("text");
layout->set_ellipsize(Pango::EllipsizeMode::END);
layout->set_width(pixels * PANGO_SCALE);
int tw, th; layout->get_pixel_size(tw, th);
cr->move_to(x, y);
layout->show_in_cairo_context(cr);
```

Transforms: `cr->translate(x, y); cr->rotate(G_PI / 2);` before drawing
rotates everything after it (the vertical window title). Use
`area.get_color()` to draw in the CSS foreground colour so themes apply.

---

## 9. Layer shell in detail

`gtk4-layer-shell` is a C library; call it on `GTK_WINDOW(gobj())`. Order
matters: init **before** the window is realized/presented.

```cpp
gtk_layer_init_for_window(w);
gtk_layer_set_namespace(w, "hypr-shell");          // for hyprland layerrules
gtk_layer_set_layer(w, GTK_LAYER_SHELL_LAYER_TOP);  // BACKGROUND | BOTTOM | TOP | OVERLAY
gtk_layer_set_anchor(w, GTK_LAYER_SHELL_EDGE_TOP, true);   // one edge = bar; opposite edges = stretch
gtk_layer_auto_exclusive_zone_enable(w);            // reserve space = window size
gtk_layer_set_exclusive_zone(w, 0);                 // or: reserve nothing (overlay)
gtk_layer_set_margin(w, GTK_LAYER_SHELL_EDGE_TOP, -30);   // negative = slide off-screen
gtk_layer_set_keyboard_mode(w, GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);  // for launchers/lock
gtk_layer_set_monitor(w, gdk_monitor);              // pin to an output (per-monitor bars)
```

Anchoring: a bar anchors to one edge plus the two adjacent edges (top +
left + right = full-width top bar). A centred OSD anchors to nothing, or to
one edge with a margin. Anchors can change at runtime (`Bar::apply_config()`
does this when `bar.position` changes) and the exclusive zone follows.

The same library provides `gtk4-session-lock.h` for the lock screen phase.

---

## 10. GApplication

```cpp
class App : public Gtk::Application {
    App() : Gtk::Application("dev.hyprshell.Shell") {}     // unique app id
    void on_activate() override { ... hold(); ... add_window(*bar_); bar_->present(); }
};
int main(int argc, char* argv[]) { return App::create()->run(argc, argv); }
```

- The app id gives single-instance behaviour: a second launch sends
  `activate` to the running one; `get_windows().empty()` tells you which
  case you're in.
- The application quits when its last window is unmapped **unless** you
  `hold()`. A shell must hold.
- `add_window()` ties the window's lifetime to the app.
- `quit()` exits the loop.

### Actions and command-line options

A `Gtk::Application` can expose named **actions** that any code in the
process (or another process with the same app id) can trigger:

```cpp
add_action("launcher", [this] { launcher_window_->toggle(); });   // in the constructor
// anywhere in the process:
Gio::Application::get_default()->activate_action("launcher");
```

Command-line flags are declared with `add_main_option_entry()` and handled
in `on_handle_local_options()`, which runs in the *invoking* process before
`activate`. `register_application()` then tells you (`is_remote()`) whether
another instance already owns the app id; if so, `activate_action()` is
delivered to it over DBus and you return `0` to exit. That is exactly how
`hypr-shell --launcher` works as a keybind target without starting a second
shell. Return `-1` to continue normal startup.

---

## 11. Gio: files, monitors, DBus, subprocesses

Gio is GLib's I/O library; gtkmm wraps it as `Gio::`. Everything has an
`_async` + `_finish` pair (see the [C++ tutorial](15-cpp-tutorial.md#part-7--errors-and-exceptions-intermediate)
for the try/catch shape).

```cpp
// file read
auto file = Gio::File::create_for_path(path);
file->load_contents_async([file](Glib::RefPtr<Gio::AsyncResult>& r) {
    char* data; gsize len; file->load_contents_finish(r, data, len); /* ... */ g_free(data);
});

// watch a file
auto monitor = file->monitor_file();
monitor->signal_changed().connect([](const Glib::RefPtr<Gio::File>&, const Glib::RefPtr<Gio::File>&,
                                     Gio::FileMonitor::Event e) {
    if (e == Gio::FileMonitor::Event::CHANGES_DONE_HINT) reload();
});   // keep `monitor` alive as a member!

// DBus proxy (properties cached, change signal)
Gio::DBus::Proxy::create_for_bus(Gio::DBus::BusType::SYSTEM, name, path, iface,
    [this](Glib::RefPtr<Gio::AsyncResult>& r) { proxy_ = Gio::DBus::Proxy::create_for_bus_finish(r); ... });
proxy_->signal_properties_changed().connect(...);
proxy_->get_cached_property(variant, "Name");

// method call on a connection
proxy_->get_connection()->call(path, iface, "Method", params, callback, bus_name);

// subprocess
auto proc = Gio::Subprocess::create({"nmcli", "-t", "device", "wifi"},
                                    Gio::Subprocess::Flags::STDOUT_PIPE | Gio::Subprocess::Flags::STDERR_MERGE);
proc->communicate_utf8_async("", [proc](Glib::RefPtr<Gio::AsyncResult>& r) {
    Glib::ustring out, err; proc->communicate_utf8_finish(r, out, err); /* parse */
});

// desktop entries → app name / icon
auto info = Gio::DesktopAppInfo::create("firefox.desktop");
info->get_display_name(); info->get_icon();
```

A `Glib::RefPtr` you drop is destroyed; monitors and proxies must be stored
as members to keep working.

Owning a DBus name and exporting an object (needed for notifications and
the tray watcher) uses `Gio::DBus::own_name()` plus
`Gio::DBus::NodeInfo::create_for_xml()` and
`connection->register_object()`. The giomm "DBus server" example in the
gtkmm docs is the reference.

---

## 12. libadwaita via the C API (settings app)

The settings app has no gtkmm; it is GObject C from C++. The translation
table:

| gtkmm | C |
|-------|---|
| `Gtk::Label l; l.set_text("x")` | `GtkWidget* l = gtk_label_new("x"); gtk_label_set_text(GTK_LABEL(l), "x")` |
| `box.append(w)` | `gtk_box_append(GTK_BOX(box), w)` |
| `w.set_visible(false)` | `gtk_widget_set_visible(w, FALSE)` |
| `w.add_css_class("flat")` | `gtk_widget_add_css_class(w, "flat")` |
| `btn.signal_clicked().connect(slot)` | `g_signal_connect(btn, "clicked", G_CALLBACK(fn), user_data)` |
| property change signal | `"notify::active"`, `"notify::selected"`, `"notify::value"` |
| lambda capture | `g_object_set_data(G_OBJECT(w), "key", ptr)` / `g_object_get_data` |
| destructor | `g_object_set_data_full(obj, "state", ptr, free_fn)` frees with the widget |

libadwaita widgets used and their C constructors: `adw_application_new`,
`adw_application_window_new`, `adw_toolbar_view_new`, `adw_header_bar_new`,
`adw_navigation_split_view_new` (sidebar + content), `adw_navigation_view_new`,
`adw_navigation_page_new_with_tag`, `adw_preferences_page_new`,
`adw_preferences_group_new`, `adw_action_row_new`, `adw_switch_row_new`,
`adw_combo_row_new`, `adw_spin_row_new_with_range`, `adw_entry_row_new`,
`adw_alert_dialog_new` (modal editor). Each has `adw_*_set_title`, and rows
accept `adw_action_row_add_suffix(row, widget)` for trailing controls. The
sidebar itself is a plain `GtkListBox` with the `navigation-sidebar` CSS
class, switching a `GtkStack` on `row-selected`.

The C docs are at gnome.pages.gitlab.gnome.org/libadwaita/doc. Memory: a
`GtkStringList` you create with `gtk_string_list_new` must be `g_object_unref`'d
after handing it to the combo row (which takes its own reference).

---

## 13. Debugging GTK

| Tool | Use |
|------|-----|
| `GTK_DEBUG=interactive ./build/hypr-shell` | Inspector: widget tree, CSS, properties, live CSS editing |
| `G_MESSAGES_DEBUG=all` | show `g_debug` output |
| `GDK_DEBUG=...` | low-level display/Wayland tracing (rarely needed) |
| `Gtk-WARNING **: ... min size -2` | a CSS rule left a node with no size: add `min-width/min-height` |
| `Gtk-CRITICAL **: gtk_widget_... assertion failed` | wrong cast, or a widget already destroyed/unparented |
| `Finalizing ... but it still has children left` | a popover or managed child was not unparented in a destructor |
| Bar doesn't appear | layer-shell init after realize; or not inside a Wayland session |
| Popover opens then vanishes | anchored to a covered widget, or opened from the trigger window |

`WAYLAND_DEBUG=1` prints every Wayland protocol message; grep for
`zwlr_layer_surface_v1` when a size or anchor looks wrong.

---

## 14. Exercises

1. Build and run the hello program in section 2. Change the anchor to the
   bottom edge, then make it a left-side strip.
2. In the hello program, add a `Gtk::Button` that toggles a label between
   two texts. Then style the button with a `Gtk::CssProvider` loaded from a
   string.
3. Give the workspace buttons a hover tooltip showing the window count
   (`Entry::windows`). One line in `Workspaces::rebuild()`.
4. Add a `Gtk::EventControllerScroll` to the volume module that changes the
   volume by 5% per wheel notch (accumulate deltas like `Workspaces`). This
   is a real Noctalia feature.
5. Draw something: add a tiny `Gtk::DrawingArea` to the battery panel's
   charge card that draws the level as a filled arc. Compare with the
   calendar ring code.
6. Open the Inspector, find the `.bar-inner` node, and change its
   `border-bottom` colour live. Then put the rule into
   `~/.config/hypr-shell/style.css` and watch it hot-reload.
7. Write the OSD window from the [roadmap guide](13-roadmap-guide.md): a
   layer-shell window on the `OVERLAY` layer, anchored bottom with a margin,
   containing an icon label and a `Gtk::ProgressBar`, shown for 1.5 s on
   `Pulse::signal_changed()`. This exercise touches every section above.

After exercise 7 you have used every GTK facility the remaining roadmap
needs, except the session-lock API (same library as layer-shell, same
calling style) and PAM (not GTK at all).
