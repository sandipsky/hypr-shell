# 14 — Workflow checklist

Print this, or keep it open. Every change, small or large, goes through it.

## Before you start

- [ ] Read the relevant roadmap item in `CLAUDE.md` and the matching section
      of the [roadmap guide](13-roadmap-guide.md).
- [ ] Find the closest existing code to copy (a module, a service, a panel,
      a settings subpage). Consistency beats cleverness here.
- [ ] Decide the config keys and the settings controls up front.
- [ ] `git status` clean, `git pull`, `meson compile -C build` succeeds
      before you touch anything.

## While you work

- [ ] One feature per branch/commit series. Don't mix a refactor with a
      feature.
- [ ] New `.cpp` → `meson.build`. New dependency → `meson.build` and
      `install.sh` `DEPS`.
- [ ] All I/O async. No threads, no `system()`, no blocking reads
      (config first-read excepted).
- [ ] Degrade gracefully: module hides when its service is unavailable;
      bad config values fall back to defaults.
- [ ] The shell never writes `config.json`; runtime state goes to
      `~/.cache/hypr-shell/`.
- [ ] New CSS goes in the right `data/css/*.css` file (or a new one listed in
      `style.css` and the gresource XML), never in C++.
- [ ] Icons as `\uXXXX`; colours and fonts in CSS, not C++.
- [ ] Compile often. Fix warnings (`warning_level=2` is on for a reason).

## Config + settings parity

- [ ] Default + parse in `Config::load()`, getter in `config.hpp`.
- [ ] Consumer re-reads on `Config::signal_changed()`.
- [ ] Settings app: `struct Settings` pointer, handler with `loading`
      guard, `populate()` read, row in `on_activate()`, signal connected
      after `populate()`.
- [ ] Layout resolver changed? Update both copies.

## Test in the live session

- [ ] `meson compile -C build && pkill -x hypr-shell; ./build/hypr-shell`
      from a terminal, read the warnings.
- [ ] Try the feature on a **top** bar and a **left** bar.
- [ ] Change every new setting from the settings app and watch it apply
      live; `cat` the config to confirm the JSON shape.
- [ ] Delete the new keys from the file → defaults come back.
- [ ] Kill the backend if you can (`systemctl stop bluetooth`, unplug
      power, toggle Wi-Fi) → nothing crashes, the module hides or degrades.
- [ ] Popovers: open, close, open again; move the bar to another edge while
      one is open.
- [ ] `./install.sh --restart` and confirm the installed binary behaves the
      same as the dev build.

## Document

- [ ] `CLAUDE.md`: tick the roadmap box or add a "pulled forward" note;
      update the Layout block for new files; add a decision-log line
      (dated, one paragraph) for every non-obvious choice or gotcha.
- [ ] `docs/04-config-reference.md` for new keys; `docs/03-code-tour.md`
      for new files; `docs/12-gotchas.md` if you hit a new trap.

## Commit

- [ ] Message: short imperative summary line, e.g. `bluetooth module + panel`,
      matching the existing history.
- [ ] `git diff --stat` shows only files you meant to change.
- [ ] Tree compiles and the bar runs from the committed state.

## Definition of done

The feature works on the installed binary in a real Hyprland session, it can
be configured from the settings app, the config reference documents it, and
`CLAUDE.md` knows about it. Anything less is "in progress", and the last
commit of the session must still compile.
