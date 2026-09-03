# 08 — Panels and popovers

Clicking the clock, battery, volume or network module opens a *panel*: a
`Gtk::Popover` whose child is a custom widget. This page explains the
pattern and the three Wayland/GTK quirks you must respect.

## Anatomy

```
Module (Gtk::Box or Gtk::Label)
 ├─ Gtk::Popover popover_          value member of the module
 │    └─ Panel* panel_             Gtk::make_managed<Panel>(), the popover's child
 └─ Gtk::GestureClick              opens it
```

`src/bar/modules/volume.cpp` is the shortest complete example:

```cpp
Volume::Volume() : Gtk::Box(Gtk::Orientation::HORIZONTAL, 0) {
    // ...
    panel_ = Gtk::make_managed<AudioPanel>();
    popover_.set_child(*panel_);
    popover_.set_parent(icon_);            // NOT *this — see below
    popover_.set_has_arrow(false);
    popover_.add_css_class("audio-popover");
    set_cursor(Gdk::Cursor::create("pointer"));

    auto click = Gtk::GestureClick::create();
    click->signal_released().connect([this](int, double, double) {
        switch (Config::get().bar_position()) {     // open on the free side
        case Config::BarPosition::Top:    popover_.set_position(Gtk::PositionType::BOTTOM); break;
        case Config::BarPosition::Bottom: popover_.set_position(Gtk::PositionType::TOP);    break;
        case Config::BarPosition::Left:   popover_.set_position(Gtk::PositionType::RIGHT);  break;
        case Config::BarPosition::Right:  popover_.set_position(Gtk::PositionType::LEFT);   break;
        }
        panel_->refresh();
        popover_.popup();
    });
    add_controller(click);
}

Volume::~Volume() {
    popover_.unparent();   // a parented popover must be unparented before its parent dies
}
```

The panel itself is a `Gtk::Box` subclass that builds its widgets in the
constructor, subscribes to services, and exposes `refresh()` for things
that must be re-read on open (sysfs brightness, monitor modes, a Wi-Fi
rescan).

## Quirk 1 — where to anchor the popover

`popover_.set_parent(x)` decides which widget the popover is positioned
against **and** which widget allocates it. A `Gtk::Box` parent will lay the
open popover out *inline* as if it were a child: the module suddenly grows
by the popover's width and slides out of its bar section. A label that is
covered by another overlay child unmaps the popover right after `popup()`.

Rules:

- Anchor to a `Gtk::Label` (clock, volume icon, network icon) or a
  `Gtk::Overlay` (battery). **Never to the module's own `Gtk::Box`.**
- If the module *is* a label (clock), `set_parent(*this)` is fine.

## Quirk 2 — a mapped popover never resizes on Hyprland

Once the popover surface is shown, Hyprland does not honour size changes.
Content that shrinks leaves dead space; content that grows is clipped. So:

- Any panel whose content changes while open (a list that fills in after a
  scan, cards that appear/disappear) must have a **fixed size**. The network
  panel is 330×440 (`set_size_request`), its list scrolls inside a
  `Gtk::ScrolledWindow` that `vexpand`s, and the "Wi-Fi is disabled" card is
  centred in the same fixed box.
- Panels that only change *values* (sliders, labels) can size naturally,
  but reserve space for the widest value (`set_width_chars(4)` on a percent
  label) so the width doesn't want to change.
- Transient status text ("Scanning…") goes into an existing row (the
  network panel puts it at the right of a section header) rather than in a
  new row that appears and disappears.

## Panels that need to know when they are open

Some panels do work only while visible: the Bluetooth panel runs device
discovery, the notification panel rebuilds on changes and re-renders
relative times every 30 s. They expose `set_open(bool)`; the module calls it
from the popover's `signal_show()` / `signal_closed()` (or from
`property_visible()`), so the expensive behaviour stops when the popover
closes. The notification panel also exposes `signal_request_close()` so an
action inside the panel ("Clear All", activating a notification) can ask the
module to `popdown()`.

## Not everything is a popover

Two surfaces are plain layer-shell windows, not popovers, because they are
not attached to a bar module:

- **Notification toasts** (`NotificationPopups`, `src/bar/notification_popup.cpp`):
  a `Gtk::Window` on the overlay (or top) layer anchored per
  `notifications.location`, exclusive zone 0, presented while popups exist.
- **The launcher** (`LauncherWindow`, `src/bar/launcher_window.cpp`): a
  fullscreen overlay-layer window with exclusive keyboard focus and a
  dimming backdrop; the centred panel sizes itself from the window's first
  allocation because a fullscreen layer surface *is* the screen size.

Both are owned by `App` and toggled via services/actions, not by clicks on
the bar. A future OSD or control center should follow the same shape.

**Layer-window width gotcha**: a wrapping `Gtk::Label` reports the *full*
text as its natural width, and a layer surface with no anchored width grows
to its natural size. Popup labels therefore get `set_max_width_chars(1)` plus
`set_hexpand(true)` so the stack's `set_size_request` width wins.

## Quirk 3 — popup grabs and the auto-hide trigger

Popovers with autohide take a Wayland popup grab. That works on the bar's
layer surface, but not from the 1px auto-hide trigger window: a popup
opened from the trigger's input serial is denied and dismisses instantly.
Users have to hover until the bar slides in, then click the bar itself.
Nothing to code; just don't try to open popovers from `trigger_`.

Also: `Bar::popover_open()` walks the widget tree looking for any mapped
`Gtk::Popover` and blocks auto-hide while one is open. New panels get this
for free as long as they are real `Gtk::Popover`s inside the bar.

## Design language

Panels share the look of Noctalia's panels: cards (`bp-card`) on a dark
rounded popover, small uppercase-ish titles (`bp-title`), primary-colour
accents. Reuse the existing classes before inventing new ones; see the class
map in [styling](09-styling-and-icons.md).

Sliders (`Gtk::Scale` with class `bp-slider`) need this pattern so a
programmatic update doesn't write back to the backend:

```cpp
updating_ = true;
scale.set_value(pct);
updating_ = false;

scale.signal_value_changed().connect([this] {
    if (!updating_)
        Pulse::get().set_volume(scale.get_value() / 100.0);
});
```

Icons inside panels are `Gtk::Label`s with class `bp-icon` and a `\uXXXX`
Tabler glyph.

## Drawing with cairo

The calendar's seconds ring and the rotated window title use a
`Gtk::DrawingArea` with `set_draw_func()`. The draw function receives a
`Cairo::RefPtr<Cairo::Context>`, the width and the height; call
`queue_draw()` when the data changes. Text goes through Pango:
`create_pango_layout(text)` then `layout->show_in_cairo_context(cr)`. This
is the escape hatch for anything GTK's widgets can't do (GTK4 labels cannot
rotate, for example).

## Dev hooks

Every panel has an environment-variable hook that pops it about 0.8 s after
startup (`HS_OPEN_CALENDAR`, `HS_OPEN_BATTERY`, `HS_OPEN_AUDIO`,
`HS_OPEN_NETWORK`, `HS_OPEN_BLUETOOTH`, `HS_OPEN_NOTIFICATIONS`,
`HS_OPEN_LAUNCHER`). Add one for a new panel: it's four lines in the module
constructor and saves a lot of clicking.

## Checklist for a new panel

1. `src/bar/<name>_panel.{hpp,cpp}`, a `Gtk::Box` subclass; list in `meson.build`.
2. Popover + gesture in the module, anchored to a label/overlay, positioned
   by bar side, `unparent()` in the destructor.
3. Decide: fixed size (content changes) or natural size (values only).
4. CSS: `popover.<name>-popover > contents` frame + a `<xx>-` class family.
5. Any user-facing toggles → `config.json` + settings app (see
   [settings](07-settings-app.md)); the battery panel's `show_*` cards are
   the precedent.
6. Dev hook env var.
7. Test on top *and* left bar positions; test with the backend absent.
