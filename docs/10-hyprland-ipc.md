# 10 — Hyprland IPC

Everything the bar knows about workspaces and windows comes from Hyprland's
two Unix sockets. All access goes through the `Hyprland` service
(`src/services/hyprland.cpp`); modules never open the sockets themselves.

## Where the sockets are

```
$XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE/.socket.sock    requests
$XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE/.socket2.sock   events
```

Both environment variables are set for every process Hyprland starts. If
either is missing, `Hyprland::available()` is false and the service does
nothing; modules must cope with that.

## Requests (`.socket.sock`)

One request per connection: connect, write the command, read until EOF.
Prefix `j/` to get JSON:

```
j/workspaces        list of workspaces  [{id, name, monitor, windows, ...}]
j/activeworkspace   the focused one     {id, name, windows, ...}
j/activewindow      {class, title, address, ...} or {} when none
j/monitors          outputs with modes, refresh rates, scale, position
j/clients           all windows
```

In code:

```cpp
Hyprland::get().request("j/activeworkspace", [this](const std::string& reply) {
    auto j = nlohmann::json::parse(reply);
    int windows = j.value("windows", 0);
    // ...
});
```

The callback runs on the main loop later; the request itself returns
immediately. When two refreshes can be in flight at once (an event storm),
guard with a serial counter so an older reply cannot overwrite a newer one.
`Workspaces::refresh()` is the template for that.

You can try any query from a terminal with `hyprctl -j workspaces` etc.

## Actions are Lua (Hyprland 0.56+)

Since Hyprland 0.56 the request socket evaluates commands as Lua.
`dispatch <expr>` is shorthand for `return hl.dispatch(<expr>)`, where
`<expr>` *constructs* a dispatcher object:

```
dispatch hl.dsp.focus({ workspace = 3 })
dispatch hl.dsp.focus({ workspace = "e+1" })
dispatch hl.dsp.window.close()
```

The pre-0.56 text grammar (`dispatch workspace 3`) is a Lua *syntax error*
and silently does nothing except log a warning, which you won't see if stderr
goes to `/dev/null`. This bit the project once already.

Rule: **never hand-write Lua in a module.** Add a typed helper to the
service so the grammar lives in one file:

```cpp
void Hyprland::focus_workspace(int id) {
    dispatch("hl.dsp.focus({ workspace = " + std::to_string(id) + " })");
}
```

`dispatch()` checks that the reply starts with `ok` and logs otherwise.

Typed helpers today: `focus_workspace(int)`, `focus_workspace(selector)`,
`focus_window(address)` (Lua `hl.dsp.focus({ window = "address:0x…" })`,
used by notifications to focus the sender after a fuzzy class match over
`j/clients`), and `set_monitor_mode(...)`. The launcher's logout entry uses
`hl.dsp.exit()`.

Raw Lua runs through `eval <code>`. `eval` only replies `ok` or `error`, so
to introspect values you smuggle them out through an error message:

```sh
hyprctl eval 'local t={} for k in pairs(hl.dsp) do t[#t+1]=k end error(table.concat(t," "))'
```

Dispatcher groups on 0.56.2: `cursor dpms event exec_cmd exec_raw exit focus
force_idle force_renderer_reload global group layout no_op pass
release_input_capture send_key_state send_shortcut submap window workspace`.

Monitor settings also go through Lua: `Hyprland::set_monitor_mode()` sends
`eval hl.monitor({ output = "...", mode = "WxH@R", position = "XxY", scale = S })`.
The old `keyword monitor ...` is rejected on 0.56.

## Events (`.socket2.sock`)

A persistent connection that streams lines of the form `NAME>>DATA`. The
service splits them and emits `signal_event(name, data)` for every line.
Subscribe and filter by name:

```cpp
Hyprland::get().signal_event().connect(
    [this](const std::string& name, const std::string& data) {
        if (name == "activewindow") { /* data = "class,title" */ }
    });
```

Events currently used:

| Event | Data | Used by |
|-------|------|---------|
| `workspace`, `workspacev2` | name / `id,name` | workspaces, bar auto-hide peek |
| `createworkspace(v2)`, `destroyworkspace(v2)`, `renameworkspace` | | workspaces |
| `focusedmon` | `monitor,workspace` | workspaces |
| `activewindow` | `class,title` — split on the **first** comma, titles may contain commas | active_window |
| `openwindow`, `closewindow`, `movewindow(v2)` | | workspaces (occupied dots), bar (empty-workspace check) |

Other events you may want later: `activelayout` (keyboard layout, data
`keyboard,layout`), `monitoradded` / `monitorremoved` (per-monitor bars),
`fullscreen`, `submap`, `urgent`. The full list is in the Hyprland wiki
under IPC.

Watch the live stream to learn what an event looks like:

```sh
socat -u UNIX-CONNECT:$XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE/.socket2.sock -
```

If the event connection drops (Hyprland restarted), the service logs
"event stream closed" and does not reconnect. A reconnect-with-backoff is a
reasonable small improvement if you ever need it.

## Special workspaces

Workspaces with a negative id are *special* (scratchpads such as
`special:magic`). The bar filters them out in `Workspaces::refresh()`.

## Debug checklist

1. `echo $HYPRLAND_INSTANCE_SIGNATURE` in the terminal you launch from. Empty
   means you're not inside the session.
2. `hyprctl -j activeworkspace` shows what the bar should be seeing.
3. Run the bar in the foreground and look for `dispatch '...' failed:` lines.
4. If a click does nothing, check the Lua expression by running it with
   `hyprctl dispatch 'hl.dsp.focus({ workspace = 2 })'`.
