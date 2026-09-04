#pragma once

#include <giomm.h>
#include <giomm/unixsocketaddress.h>
#include <sigc++/sigc++.h>

#include <functional>
#include <memory>
#include <string>

namespace hyprshell {

// Hyprland IPC.
//
// Sockets live in $XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE/:
//   .socket.sock   one request per connection; "j/<cmd>" replies with JSON
//   .socket2.sock  newline-separated "EVENT>>DATA" stream
class Hyprland {
public:
    using ReplyHandler = std::function<void(const std::string& reply)>;

    static Hyprland& get();

    Hyprland(const Hyprland&) = delete;
    Hyprland& operator=(const Hyprland&) = delete;

    bool available() const { return available_; }

    // Send one command; on_reply runs on the main loop with the raw reply.
    // Prefix "j/" for JSON output.
    void request(const std::string& command, ReplyHandler on_reply);

    // Fire-and-forget dispatcher. Hyprland >= 0.56 evaluates socket commands as
    // Lua: "dispatch X" is shorthand for "return hl.dispatch(X)", so `args` must
    // be a Lua expression constructing a dispatcher, e.g.
    //   dispatch("hl.dsp.focus({ workspace = 3 })")
    // The old text grammar ("workspace 3") is rejected by the Lua parser.
    void dispatch(const std::string& args);

    // Focus a workspace by id or by selector string ("e+1", "e-1", "previous",
    // ...). Selector must not contain quotes (it is spliced into Lua verbatim).
    void focus_workspace(int id);
    void focus_workspace(const std::string& selector);

    // Focus a window by its j/clients address ("0x…") and raise it (Noctalia's
    // focusWindow: focus + alter_zorder top).
    void focus_window(const std::string& address);

    // Turn every monitor's DPMS on or off (Noctalia's turnOn/OffMonitors).
    // Hyprland accepts ANY argument table for dpms without error — only this
    // exact form is known to do what it says; never probe it blind.
    void set_dpms(bool on);

    // Apply a monitor mode via the Lua config API (`eval hl.monitor{...}`) —
    // Hyprland >= 0.56 rejects the old `keyword monitor` grammar. `output`
    // must not contain quotes. on_done(true) only on an explicit "ok" reply.
    void set_monitor_mode(const std::string& output, int width, int height, int rate,
                          double scale, int transform, int x, int y,
                          std::function<void(bool)> on_done);

    // Raw event from the event socket, e.g. ("workspace", "3").
    sigc::signal<void(const std::string&, const std::string&)>& signal_event() {
        return event_signal_;
    }

private:
    Hyprland();

    void connect_event_stream();
    void read_events();
    void handle_event_line(const std::string& line);
    void read_reply(const Glib::RefPtr<Gio::SocketConnection>& conn,
                    const std::shared_ptr<std::string>& accumulated,
                    const ReplyHandler& on_reply);

    bool available_ = false;
    std::string socket_dir_;
    Glib::RefPtr<Gio::SocketConnection> event_conn_;
    std::string event_buffer_;
    sigc::signal<void(const std::string&, const std::string&)> event_signal_;
};

} // namespace hyprshell
