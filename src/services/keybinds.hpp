#pragma once

#include <functional>
#include <string>
#include <vector>

namespace hyprshell {

// Hyprland's key bindings for the keybindings overlay. What is bound comes
// from `j/binds`; what each bind *does* cannot — Hyprland >= 0.56 configs are
// Lua and every bind reports dispatcher "__lua" with a callback id — so the
// user's config is replayed in a `lua` subprocess with a recording stand-in
// for the `hl` API (data/keybinds-introspect.lua), which yields the dispatcher
// path + arguments, the bind options and the comment header above the call.
// Both sources are merged by (modifiers, key, submap) and humanized:
// "Super + Q — Close window", grouped under the config's own section comments.
struct Keybind {
    std::vector<std::string> mods; // display names in a fixed order: Super, Ctrl, Alt, Shift, …
    std::string key;               // display name: "Enter", "←", "Volume up", "Left click"
    std::string description;       // humanized action ("Open Files", "Switch to workspace 3")
    std::string group;             // section comment above the bind, "Submap: x", or "Other"
    std::string submap;
    bool mouse = false;
    bool locked = false;           // also works while the session is locked
    std::string search_text;       // lowercased haystack for the overlay's filter
};

// Fetch everything asynchronously; `on_done` runs once on the main loop, with
// whatever arrived within a few seconds (an unreachable Hyprland or a missing
// `lua` degrade to the other source, never to an error).
void fetch_keybinds(std::function<void(std::vector<Keybind>)> on_done);

// Exposed for the overlay's empty state: whether a Lua config replay is
// possible at all (`lua` on PATH and ~/.config/hypr/hyprland.lua present).
bool keybinds_introspection_available();

} // namespace hyprshell
