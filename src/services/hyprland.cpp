#include "services/hyprland.hpp"

#include <glibmm.h>

#include <algorithm>

namespace hyprshell {

Hyprland& Hyprland::get() {
    static Hyprland instance;
    return instance;
}

Hyprland::Hyprland() {
    const char* signature = g_getenv("HYPRLAND_INSTANCE_SIGNATURE");
    const char* runtime_dir = g_getenv("XDG_RUNTIME_DIR");
    if (signature == nullptr || runtime_dir == nullptr) {
        g_warning("Not in a Hyprland session; bar modules stay inactive");
        return;
    }
    socket_dir_ = std::string(runtime_dir) + "/hypr/" + signature;
    available_ = true;
    connect_event_stream();
}

void Hyprland::request(const std::string& command, ReplyHandler on_reply) {
    if (!available_) {
        return;
    }
    auto client = Gio::SocketClient::create();
    auto address = Gio::UnixSocketAddress::create(socket_dir_ + "/.socket.sock");
    client->connect_async(address, [this, client, command, on_reply](Glib::RefPtr<Gio::AsyncResult>& result) {
        try {
            auto conn = client->connect_finish(result);
            // keep the payload alive until the write completes
            auto payload = std::make_shared<std::string>(command);
            conn->get_output_stream()->write_all_async(
                payload->data(), payload->size(),
                [this, conn, payload, on_reply](Glib::RefPtr<Gio::AsyncResult>& write_result) {
                    try {
                        gsize written = 0;
                        conn->get_output_stream()->write_all_finish(write_result, written);
                        read_reply(conn, std::make_shared<std::string>(), on_reply);
                    } catch (const Glib::Error& e) {
                        g_warning("Hyprland request write failed: %s", e.what());
                    }
                });
        } catch (const Glib::Error& e) {
            g_warning("Hyprland request connect failed: %s", e.what());
        }
    });
}

void Hyprland::read_reply(const Glib::RefPtr<Gio::SocketConnection>& conn,
                          const std::shared_ptr<std::string>& accumulated,
                          const ReplyHandler& on_reply) {
    conn->get_input_stream()->read_bytes_async(8192, [this, conn, accumulated, on_reply](Glib::RefPtr<Gio::AsyncResult>& result) {
        try {
            auto bytes = conn->get_input_stream()->read_bytes_finish(result);
            gsize size = 0;
            const auto* data = static_cast<const char*>(bytes->get_data(size));
            if (size > 0) {
                accumulated->append(data, size);
                read_reply(conn, accumulated, on_reply);
                return;
            }
            conn->close();
            if (on_reply) {
                on_reply(*accumulated);
            }
        } catch (const Glib::Error& e) {
            g_warning("Hyprland request read failed: %s", e.what());
        }
    });
}

void Hyprland::dispatch(const std::string& args) {
    request("dispatch " + args, [args](const std::string& reply) {
        if (reply.rfind("ok", 0) != 0) {
            g_warning("dispatch '%s' failed: %s", args.c_str(), reply.c_str());
        }
    });
}

void Hyprland::focus_workspace(int id) {
    dispatch("hl.dsp.focus({ workspace = " + std::to_string(id) + " })");
}

void Hyprland::focus_workspace(const std::string& selector) {
    dispatch("hl.dsp.focus({ workspace = \"" + selector + "\" })");
}

void Hyprland::focus_window(const std::string& address) {
    // address comes from j/clients ("0x…"); hex-only guard against Lua injection
    if (address.empty() ||
        !std::all_of(address.begin(), address.end(),
                     [](char c) { return g_ascii_isalnum(c); }))
        return;
    dispatch("hl.dsp.focus({ window = \"address:" + address + "\" })");
}

void Hyprland::set_dpms(bool on) {
    dispatch(std::string("hl.dsp.dpms({ action = \"") + (on ? "on" : "off") + "\" })");
}

void Hyprland::set_monitor_mode(const std::string& output, int width, int height,
                                int rate, double scale, int transform, int x, int y,
                                std::function<void(bool)> on_done) {
    char scale_buf[G_ASCII_DTOSTR_BUF_SIZE];
    g_ascii_dtostr(scale_buf, sizeof scale_buf, scale); // locale-proof "1.25"
    std::string lua = "hl.monitor({ output = \"" + output + "\", mode = \"" +
                      std::to_string(width) + "x" + std::to_string(height) + "@" +
                      std::to_string(rate) + "\", position = \"" + std::to_string(x) +
                      "x" + std::to_string(y) + "\", scale = " + scale_buf;
    if (transform != 0)
        lua += ", transform = " + std::to_string(transform);
    lua += " })";
    request("eval " + lua, [lua, on_done](const std::string& reply) {
        const bool ok = reply.rfind("ok", 0) == 0;
        if (!ok)
            g_warning("eval '%s' failed: %s", lua.c_str(), reply.c_str());
        if (on_done)
            on_done(ok);
    });
}

void Hyprland::connect_event_stream() {
    auto client = Gio::SocketClient::create();
    auto address = Gio::UnixSocketAddress::create(socket_dir_ + "/.socket2.sock");
    client->connect_async(address, [this, client](Glib::RefPtr<Gio::AsyncResult>& result) {
        try {
            event_conn_ = client->connect_finish(result);
            read_events();
        } catch (const Glib::Error& e) {
            g_warning("Hyprland event stream failed: %s", e.what());
        }
    });
}

void Hyprland::read_events() {
    event_conn_->get_input_stream()->read_bytes_async(4096, [this](Glib::RefPtr<Gio::AsyncResult>& result) {
        try {
            auto bytes = event_conn_->get_input_stream()->read_bytes_finish(result);
            gsize size = 0;
            const auto* data = static_cast<const char*>(bytes->get_data(size));
            if (size == 0) {
                g_warning("Hyprland event stream closed");
                return;
            }
            event_buffer_.append(data, size);
            size_t newline;
            while ((newline = event_buffer_.find('\n')) != std::string::npos) {
                handle_event_line(event_buffer_.substr(0, newline));
                event_buffer_.erase(0, newline + 1);
            }
            read_events();
        } catch (const Glib::Error& e) {
            g_warning("Hyprland event read failed: %s", e.what());
        }
    });
}

void Hyprland::handle_event_line(const std::string& line) {
    auto sep = line.find(">>");
    if (sep == std::string::npos) {
        return;
    }
    event_signal_.emit(line.substr(0, sep), line.substr(sep + 2));
}

} // namespace hyprshell
