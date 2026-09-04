# 12 — Gotchas

Each of these cost real time once. The decision log in `CLAUDE.md` records
when; this page records what to do.

## Layer shell and Wayland

**Call `gtk_layer_init_for_window()` before the window is realized or
presented.** Afterwards it is silently ignored and you get a normal floating
window. Also `set_decorated(false)`, or GTK draws a client-side title bar.

**Keep `window.bar { background: transparent; }`** in CSS and put the visible
background on `.bar-inner`. Otherwise the window's own background paints
over rounded corners and margins.

**A fully transparent GTK4 window never commits a real buffer.** The layer
surface then falls back to GtkWindow's 200px default size instead of the
1px you asked for. The auto-hide trigger strip is therefore painted
`rgba(0,0,0,0.01)`: invisible, but a real buffer. `background: transparent`,
`opacity: 0` and `set_opacity(0)` all break it.

**Hiding every window quits the app.** GTK's application exits when the last
window is unmapped. `App::on_activate()` calls `hold()` so
`bar.visibility = "hidden"` works.

**A mapped popover never resizes on Hyprland.** Panels whose content changes
while open need a fixed size. See [panels](08-panels-and-popovers.md).

**Popup grabs from the 1px trigger window are denied**, so a popover opened
from the trigger dismisses instantly. Users click the bar, not the strip.

**Off-screen surfaces receive no input**, which is what makes the negative
margin auto-hide trick safe: the hidden bar can't be clicked by accident.

**A layer window with no anchored width grows to its natural width**, and a
wrapping label's natural width is the *whole* text on one line. Notification
popup labels use `set_max_width_chars(1)` + `set_hexpand(true)` so the
stack's requested width wins.

**A fullscreen layer surface's first allocation is the screen size.** The
launcher derives its panel geometry from that instead of querying monitors.

**Session lock windows belong to gtk4-session-lock.** Create each surface
unrealized, `assign_window_to_monitor`, then present; the library destroys
them on unlock, so use `Gtk::make_managed` and drop your pointers from
`signal_destroy()`. Never hide a lock surface (protocol error).

**`GetSessionByPID` fails for a shell started from a terminal** (the process
is in a different logind scope). Fall back to `$XDG_SESSION_ID` →
`Manager.GetSession`, or `loginctl lock-session` will never reach you.

## GTK

**A `Gtk::Picture` grows to the image's natural size** — `set_size_request`
is a minimum. For a fixed avatar, pre-scale and crop the pixbuf to the target
size and hand the texture to `set_paintable()`. Expand flags on a child also
leak into the parent, so a centred 130px ring stretches if its picture
`hexpand`s.

**Anchor popovers to a label or overlay, never to the module's `Gtk::Box`.**
A box parent allocates the open popover inline and the module balloons in
width. Anchoring to a label that sits under another overlay child unmaps the
popover immediately after `popup()`.

**Programmatic widget changes fire the same signals as user input.** Guard
with an `updating_` flag (shell panels) or the `loading` flag (settings app)
or you'll write the value straight back to the backend/file.

**Don't destroy a widget from inside its own signal handler.** Defer with
`g_idle_add_once` / `Glib::signal_idle().connect_once` (settings layout
editor).

**Rounded `scale` / `progressbar` need explicit `min-width` and
`min-height`** on `highlight` / `progress` / `slider` nodes, or GTK warns
about -2 sizes and draws nothing.

**GTK4 labels cannot rotate.** Use a `Gtk::DrawingArea` with PangoCairo
(`ActiveWindow::on_vertical_draw`).

**Lambdas capturing `this` are not auto-disconnected.** Disconnect timers in
destructors; `unparent()` popovers in destructors.

**`Gtk::Box` in a vertical bar**: modules that lay children out horizontally
must flip orientation themselves (`set_orientation`) when
`Config::get().bar_vertical()`.

**A `Gtk::Entry` draws its own blue focus ring** on top of your themed
border. Add `outline: none` to the entry's CSS (launcher search box).

**Borders are outside `min-width`/`min-height`** in GTK's box model. A pill
toggle that should be 36×22 is written as 34×20 + 1px border.

**Pango layouts are only valid after allocation.** Measuring whether a label
is ellipsized (to decide whether to show an expand chevron) must happen in a
tick callback, not in the constructor.

**Transitions without shaders: mask nodes.** `GtkGLShader` is gone since
GTK 4.16, so the wallpaper transitions are built from
`gtk_snapshot_push_mask(ALPHA)`: record the mask (linear / radial gradients
for soft edges), `pop()` once to switch to the source, draw the new image,
`pop()` again. Gradients clamp their end colours beyond the endpoints, which
is exactly the "everything past the edge is revealed" behaviour a wipe needs.
`push_cross_fade(progress)` does the fade. Keep the widget's own `snapshot_vfunc`
callable from outside (`Gtk::Snapshot::create()` + `gtk_snapshot_to_node` +
`gsk_renderer_render_texture`) — that is how frames get verified when the
desktop is covered by windows.

**A view that should animate its first image in must start empty.** The
wallpaper windows are created before the startup transition fires; handing
them the image immediately made the later animated `show_image()` a no-op
(same path, nothing to do), so the first image simply appeared.

**A scrolled window nested in a scrolling page collapses.** A viewport
allocates its child at the child's *minimum* height (GTK's default
`GTK_SCROLL_MINIMUM` policy), and a `GtkScrolledWindow`'s minimum is a
sliver, so the moment the page overflowed, the settings app's wallpaper grid
shrank to one label row. `propagate_natural_height` only raises the natural
size. Fix: measure the content (`gtk_widget_measure`, exact before mapping
when the columns are fixed) and pin `min_content_height` to
`min(natural, cap)`.

**The default overlay scrollbar only appears while scrolling.** For a
persistent one call `set_overlay_scrolling(false)` on the
`Gtk::ScrolledWindow` and style `scrollbar slider`.

## Hyprland

**Actions are Lua since 0.56.** `dispatch workspace 3` is a syntax error.
Write `hl.dsp.focus({ workspace = 3 })`. `keyword monitor ...` is likewise
rejected; use `eval hl.monitor({...})`. Only an `ok` reply means success.
All Lua lives in `services/hyprland.cpp`.

**`activewindow` data is `class,title`; split on the first comma only.**
Titles contain commas.

**Negative workspace ids are special workspaces.** Filter them out.

**Replies can arrive out of order** relative to the events that triggered
them. Serial-guard every refresh that can be re-triggered.

**`install.sh --restart` sends stderr to /dev/null.** A dispatch that fails
silently is a warning you never saw. Run from a terminal when debugging.

## Config and settings

**Absent module = enabled.** New modules default on. Don't change this
without thinking about existing users' files.

**The settings app may need shell state, not just config.** The wallpaper
grid highlights the image on screen, which after a slideshow pick is *not*
`wallpaper.current` — the shell never writes config.json. The settings app
therefore watches `~/.cache/hypr-shell/wallpaper.json` (a `GFileMonitor`) and
falls back to the config value; keep such state files tiny and JSON.

**Reset every default at the top of `Config::load()`.** A key deleted from
the file must revert; the parser only overwrites keys it finds.

**Layout resolution is duplicated** in `Config::load()` and the settings
app's `resolve_layout()`. Change both.

**Format numbers for CSS/Lua with `g_ascii_dtostr`.** `std::to_string(0.88)`
gives `0,88` in a comma-decimal locale and breaks the parser silently.

**Desktop entries need an absolute `Exec` path.** Launchers don't have
`~/.local/bin` in their PATH; that's why `.desktop.in` gets `@bindir@`
substituted at build time.

**Half-typed values arrive live.** The settings app saves on every
keystroke, so the shell sees `%H:` before `%H:%M`. Parsers must tolerate
garbage (clock falls back to `%H:%M`).

**The shell never writes `config.json`.** Runtime state goes to
`~/.cache/hypr-shell/`. A value that is both a config default and a runtime
toggle (do-not-disturb) is adopted from config only on value-change
*edges*, so unrelated saves from the settings app don't clobber the runtime
state.

**Desktop entry icon**: the settings entry uses `Icon=org.gnome.Settings`,
which hypr-shell does not ship. Without the svg installed the launcher shows
a generic icon; that is expected, not a bug.

## Services

**`Gio::DBus::Proxy` property casts must match the DBus type exactly**
(`guint32` for `u`, `gint64` for `x`, ...). A wrong `Glib::Variant<T>` throws
`std::bad_cast` at runtime, not compile time. Check with `busctl introspect`.

**sysfs has no inotify.** Provide `refresh()` and call it when the UI opens.

**`nmcli` can exit 0 on failure.** Grep the output for `Error`. Use `-t`
terse mode with `--escape yes` and unescape `\:`.

**PulseAudio context**: create with `PA_CONTEXT_NOFAIL` so a PipeWire
restart reconnects instead of leaving a dead context.

**BlueZ and NM emit other `PropertiesChanged` before the property you set
flips** (`Powered`, `WirelessEnabled`). Re-reading on each one bounces the
switch. Keep a pending target value and prefer it until confirmed or timed
out (`pending_power_`, `pending_wireless_`).

**A persisted rfkill block makes `Adapter1.Powered = true` a silent no-op.**
Run `rfkill unblock bluetooth` first, like `Bluez::set_enabled` does.

**Re-registering a DBus object on the same connection errors.** When the
notification daemon releases its bus name (`notifications.enabled = false`),
keep the object registration and only unown/re-own the name.

**Pairing needs a BlueZ agent.** We have none; `bluetoothctl pair` registers
its own, so pairing shells out to it. PIN-entry devices (keyboards) won't
pair from the panel.

**Optimistic updates**: writers set local state and emit *before* the
round-trip; otherwise sliders bounce.

**PAM blocks and sleeps.** `pam_authenticate` runs the conversation
synchronously and `pam_unix` delays a failed attempt by ~2s, so it must not
run on the main loop: `PamAuth` runs it on a thread and posts events through
a `Glib::Dispatcher`. Test the stack outside the shell with a tiny ctypes
script before blaming the shell — and never test a real lock without a
second way in.

## Source hygiene

**Icon glyphs as `\uXXXX` escapes**, never literal Private Use Area
characters (they render as boxes and get mangled).

**Every `.cpp` must be listed in `meson.build`.** Meson does not glob.

**The Segoe font is proprietary.** Strip `data/fonts/SegoeIcons.ttf` (and
its install line) before publishing the repository.
