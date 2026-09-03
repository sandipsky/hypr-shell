# 09 — Styling and icons

## The three CSS layers

Styles come from three providers stacked by priority (higher wins):

| Priority | Source | When it loads |
|----------|--------|---------------|
| `APPLICATION` | `data/style.css`, compiled into the binary as a GResource. It only `@import`s `data/css/bar.css`, `calendar.css`, `panels.css`, `notifications.css`, `launcher.css` (also bundled; list new files in `hypr-shell.gresource.xml`) | at startup |
| `APPLICATION + 1` | One generated rule: `.bar-inner { background-color: alpha(#11111b, X); }` from `bar.background_opacity` | every config reload |
| `USER` | `~/.config/hypr-shell/style.css` | at startup and on every save (FileMonitor) |

Practical consequences:

- To change the default look, edit the relevant file in `data/css/` and
  rebuild (the files are embedded, so a rebuild is required). A new area
  gets its own file: add it to `data/css/`, to the `@import` list in
  `style.css`, and to `data/hypr-shell.gresource.xml`.
- To experiment without rebuilding, put rules in
  `~/.config/hypr-shell/style.css`. They apply within a second of saving,
  and they beat everything else at equal specificity.
- A bad user stylesheet logs `user css reload failed:` and is skipped; the
  previous state stays.

## GTK CSS in one minute

GTK4 CSS looks like web CSS but the selectors are widget *node names*
(`label`, `button`, `popover`, `scale`, `trough`) plus the classes you add
with `add_css_class()`. Useful differences from the web:

- `alpha(#rrggbb, 0.5)` makes a colour translucent. `rgba()` also works.
- Sizes: use `min-width` / `min-height`, not `width`/`height`.
- No `display`, no flexbox, no `position`. Layout is done by widgets.
- Compound widgets have sub-nodes, e.g. `scale trough highlight slider`,
  `progressbar trough progress`, `popover > contents`. The inspector
  (`GTK_DEBUG=interactive`) shows the exact tree.
- Rounded `scale`/`progressbar` parts need an explicit `min-width` and
  `min-height` on the inner nodes or GTK warns about negative sizes.

## Class map

Window and layout:

| Selector | Meaning |
|----------|---------|
| `window.bar` | The bar window. Must stay `background: transparent`. |
| `window.bar.bottom`, `.left`, `.right` | Added by `Bar::apply_config()` for non-top positions; the theme moves the hairline border and rotates paddings. |
| `window.bar-trigger` | The 1px auto-hide hover strip. Painted `rgba(0,0,0,0.01)` on purpose; see [gotchas](12-gotchas.md). |
| `.bar-inner` | The `Gtk::CenterBox` that has the visible background, border and padding. |
| `.module` | Every module. Padding and base font. |

Modules:

| Selector | Notes |
|----------|-------|
| `.workspaces`, `.workspaces button`, `button.active`, `button.occupied` | Round pills. |
| `.active-window` | Title text. |
| `.icon` | Any icon label: sets the Tabler font. |
| `.launcher`, `.network`, `.bluetooth`, `.volume`, `.battery` | Status/action icons. |
| `.battery .icon`, `.battery.charging .icon-fill`, `.battery.saver .icon-fill` | Segoe font + green/amber fill tints. |
| `.notifications .notif-badge` | Unread dot on the bell. |
| `.clock` | Bold time. |

Popovers (each has `popover.<name>-popover > contents` for the frame):

| Prefix | Panel |
|--------|-------|
| `cal-*` | Calendar (`cal-header`, `cal-day-big`, `cal-month`, `cal-year`, `cal-time`, `cal-body`, `cal-weekday`, `cal-day`, `cal-day.today`, `cal-day.dim`, `button.cal-icon-btn`) |
| `bp-*` | Battery panel, also the shared "card" look (`bp-card`, `bp-title`, `bp-value`, `bp-percent`, `bp-icon`, `scale.bp-slider`, `progressbar.bp-level`, `button.bp-rate-btn`) |
| `ap-*` | Audio panel (`ap-kind`, `button.ap-mute-btn`) |
| `np-*` | Network panel (`np-title`, `np-section`, `np-connected*`, `np-net-row`, `np-ssid`, `np-security`, `button.np-connect`, `button.np-disconnect`, `np-disabled*`, `.network-panel switch`, `.network-panel entry`) |
| `bt-*` | Bluetooth panel (`popover.bluetooth-popover`, `bt-sub`; reuses the `np-*` list rows and the `.network-panel switch` pill toggle) |
| `notif-*` | Notification history panel and toasts (`popover.notification-popover`, `notif-header`, `button.notif-clear`, `notif-card`, `notif-icon`, `notif-dot.{low,normal,critical}`, `notif-app`, `notif-time`, `notif-summary`, `notif-body`, `button.notif-action`, `window.notification-popups`, `notif-popup*`) — `data/css/notifications.css` |
| `launcher-*` | Launcher window (`window.launcher`, `launcher-panel`, `entry.launcher-search`, `launcher-row(.selected)`, `launcher-name`, `launcher-desc`, `launcher-glyph`, `button.launcher-pin`, `launcher-count`, `separator.launcher-divider`, slim `scrollbar` styling) — `data/css/launcher.css` |

When you write a new module or panel, follow the pattern: one class for the
module named after it, a `<prefix>-` family for a panel, and reuse `bp-card`
for card backgrounds so panels look alike. The pill toggle used by the
Wi-Fi and Bluetooth panels (`.network-panel switch`) is a 1:1 port of
Noctalia's NToggle; reuse it for any on/off switch in a panel.

GTK box-model note: borders sit **outside** `min-width`/`min-height`, so a
34×20 track with a 1px border renders 36×22. Size accordingly.

## Colours

The palette is currently hardcoded in `style.css`. The bar uses Catppuccin
Mocha-ish values (`#11111b` background, `#cdd6f4` text, `#a6adc8` dim text).
The popovers use a snapshot of the user's Noctalia `colors.json`
(`mPrimary #bfc2ff`, `mOnPrimary #202578`, ...). The plan is to make these
config-driven theme tokens later; until then, keep new colours consistent
with the existing ones and put them in `style.css`, not in C++.

## Fonts

Text: `"Fira Sans"` (Noctalia's default). If it is not installed, GTK falls
back to the system sans; the bar still works.

Icons come from two icon fonts installed to
`~/.local/share/fonts/hypr-shell/` by `meson install`:

| Font | CSS `font-family` | Used for |
|------|-------------------|----------|
| `noctalia-tabler-icons.ttf` (MIT) | `"noctalia-tabler-icons"` | wifi, ethernet, volume, mic, calendar arrows, panel icons |
| `SegoeIcons.ttf` (Microsoft, proprietary) | `"Segoe Fluent Icons"` | battery glyphs (Windows 11 look) |

An icon is just a `Gtk::Label` whose text is a single Private Use Area
character and whose CSS class selects the font:

```cpp
constexpr const char* kIconMic = "\uEAF0";   // tabler "microphone"
icon_.add_css_class("icon");
icon_.set_text(kIconMic);
```

**Always write glyphs as `\uXXXX` escapes** in C++ source. Literal PUA
characters look like blank squares in most editors and get mangled by some
tools. Some older files still have literals; convert them if you touch them.

Finding a codepoint: open the Tabler icon set (tabler.io/icons), pick an
icon, and look up its codepoint in the font (a font viewer, or
`fc-query --format='%{charset}\n' noctalia-tabler-icons.ttf`). The Noctalia
QML sources are also a handy index since the fork used the same font.

After adding a font file, list it in `meson.build` under `install_data`, and
run `fc-cache -f ~/.local/share/fonts/hypr-shell` (install.sh does this).

## Checking a style change

1. Edit the file in `data/css/`, run `meson compile -C build` (the GResource
   step re-runs automatically), restart the bar.
2. Or paste the rule into `~/.config/hypr-shell/style.css` first to see it
   live, then move it into the right `data/css/` file once it's right.
3. Watch the terminal: GTK prints CSS parse errors with line numbers.
