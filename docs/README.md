# hypr-shell developer documentation

These pages exist so that anyone, including someone new to C++ and GTK, can
keep building hypr-shell by hand. They explain how the project is put
together, how to make the common kinds of change, and which mistakes have
already been made once so you don't repeat them.

`CLAUDE.md` in the repository root is the short, authoritative project
charter (stack, rules, roadmap, decision log). These docs are the long-form
companion. When the two disagree, `CLAUDE.md` wins; fix the docs.

## Reading order

If you are new to the project, read in this order:

| # | Page | What you get |
|---|------|--------------|
| 1 | [Getting started](01-getting-started.md) | Install, build, run, the edit/compile/test loop, debugging |
| 2 | [Architecture](02-architecture.md) | The big picture: App, Bar, modules, services, config, CSS |
| 3 | [Code tour](03-code-tour.md) | What every file in `src/` and `data/` does |
| 4 | [Config reference](04-config-reference.md) | Every `config.json` key, its default, and where it is read |
| 5 | [Tutorial: add a bar module](05-tutorial-new-module.md) | End-to-end worked example (a system-load module), every file touched |
| 6 | [Services and async I/O](06-services-and-async-io.md) | How backends (DBus, sockets, sysfs, subprocesses) are written |
| 7 | [The settings app](07-settings-app.md) | How `hypr-shell-settings` works and how to add a control |
| 8 | [Panels and popovers](08-panels-and-popovers.md) | Click panels like the calendar and battery panel |
| 9 | [Styling and icons](09-styling-and-icons.md) | CSS classes, theme layers, icon fonts |
| 10 | [Hyprland IPC](10-hyprland-ipc.md) | Sockets, JSON queries, Lua dispatchers, events |
| 11 | [gtkmm primer](11-gtkmm-primer.md) | The minimum GTK/glibmm/sigc++ you need for this codebase |
| 12 | [Gotchas](12-gotchas.md) | Hard-won pitfalls, with the fix for each |
| 13 | [Roadmap guide](13-roadmap-guide.md) | What is left to build and how to approach each phase |
| 14 | [Workflow checklist](14-workflow-checklist.md) | The steps every change should go through before committing |
| 15 | [C++ course](15-cpp-tutorial.md) | 22 lessons from "what is a compiler" to the project's own idioms; each has a runnable example, the matching hypr-shell code, and an exercise |
| 16 | [GTK tutorial](16-gtk-tutorial.md) | GTK4/gtkmm from hello-world to layer shell, drawing, Gio and libadwaita, with exercises |

New to C++ or GTK? Read pages 15 and 16 first (they are self-contained
tutorials), then come back to page 1. If you already know GTK and just want
to add something, jump to page 5, 6 or 7 and keep page 12 open in another
tab.

## Ground rules in one paragraph

C++20 with gtkmm-4.0 for the shell, libadwaita C API for the settings app,
meson + ninja to build, Arch Linux + Hyprland as the only target. Build one
roadmap phase at a time and leave the tree compiling at the end of every
session. Every user-facing option goes into `config.json` **and** gets a
control in the settings app in the same change. Never block the GTK main
loop. When you make a non-obvious decision, add a line to the decision log
at the bottom of `CLAUDE.md`.
