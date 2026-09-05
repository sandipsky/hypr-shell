#include "services/clipboard.hpp"

#include "services/config.hpp"
#include "services/hyprland.hpp"

#include <nlohmann/json.hpp>

#include <glibmm.h>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <signal.h>
#include <sys/prctl.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <regex>

namespace hyprshell {

namespace {

// Noctalia's default clipboardWatchTextCommand / clipboardWatchImageCommand
const std::vector<std::string> kWatchCommands[2] = {
    {"wl-paste", "--type", "text", "--watch", "cliphist", "store"},
    {"wl-paste", "--type", "image", "--watch", "cliphist", "store"},
};
// what a foreign watcher (Noctalia, the user's exec-once) looks like in ps
constexpr const char* kWatcherPattern = "wl-paste .*--watch cliphist store";
constexpr std::size_t kThumbCacheSize = 100; // 128px textures are small; Noctalia keeps 20
constexpr int kPreviewWidth = 100;          // Noctalia's list(100)

bool in_path(const char* program) {
    return !Glib::find_program_in_path(program).empty();
}

std::string lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return text;
}

std::string trimmed(const std::string& text) {
    const auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
        return "";
    const auto end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

// Noctalia's "smart type detection" (ClipboardService list parsing)
Clipboard::Item::Kind classify(const std::string& preview) {
    using Kind = Clipboard::Item::Kind;
    static const std::regex color(R"(^#([a-f0-9]{3}|[a-f0-9]{6}|[a-f0-9]{8})$)");
    static const std::regex link(R"(^https?://)", std::regex::icase);
    static const std::regex file(R"(^(/|~/|file://))", std::regex::icase);
    static const std::regex code_start(
        R"(^(const|let|var|function|class|struct|interface|type|enum|import|export|func|fn|pub|def|using|namespace|property|public|private|protected)\b)",
        std::regex::icase);
    static const std::regex code_prefix(R"(^(#include|#define|#\[|@|//|/\*|<\?|<html|<body|<!DOCTYPE))",
                                        std::regex::icase);
    static const std::regex code_node(R"(\b(require\(|module\.exports)\b)");
    const std::string t = trimmed(preview);
    const std::string lower = lowercase(t);
    if (std::regex_search(lower, color))
        return Kind::Color;
    if (std::regex_search(t, link))
        return Kind::Link;
    if (std::regex_search(t, file) && t.rfind("//", 0) != 0 && t.find('\n') == std::string::npos)
        return Kind::File;
    auto has = [&t](const char* needle) { return t.find(needle) != std::string::npos; };
    if ((has("{") && has("}") && (has(";") || has("="))) || has("</") || has("/>") || has("=>") ||
        has("===") || has("!==") || has("::") || has("->") || std::regex_search(t, code_start) ||
        std::regex_search(t, code_prefix) || std::regex_search(t, code_node))
        return Kind::Code;
    return Kind::Text;
}

// "[[ binary data 1.2 MiB png 1920x1080 ]]" (Noctalia's parseImageMeta)
void parse_image_meta(Clipboard::Item& item) {
    static const std::regex meta(
        R"(\[\[\s*binary data\s+([\d\.]+\s*(?:KiB|MiB|GiB|B))\s+(\w+)\s+(\d+)x(\d+)\s*\]\])",
        std::regex::icase);
    std::smatch m;
    if (!std::regex_search(item.preview, m, meta))
        return;
    item.size_text = m[1];
    item.format = lowercase(m[2]);
    item.width = std::atoi(m[3].str().c_str());
    item.height = std::atoi(m[4].str().c_str());
    std::transform(item.format.begin(), item.format.end(), item.format.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    const std::string fmt = lowercase(item.format);
    item.mime = fmt == "png"                    ? "image/png"
                : fmt == "jpg" || fmt == "jpeg" ? "image/jpeg"
                : fmt == "webp"                 ? "image/webp"
                : fmt == "gif"                  ? "image/gif"
                                                : "image/*";
}

} // namespace

Clipboard& Clipboard::get() {
    static Clipboard instance;
    return instance;
}

Clipboard::Clipboard() {
    available_ = in_path("cliphist") && in_path("wl-paste") && in_path("wl-copy");
    paste_available_ = in_path("wtype");
    if (!available_)
        g_message("clipboard: cliphist / wl-clipboard not found — history unavailable");
    Config::get().signal_changed().connect(sigc::mem_fun(*this, &Clipboard::apply_config));
    apply_config();
}

bool Clipboard::enabled() const {
    return available_ && Config::get().clipboard().enabled;
}

void Clipboard::apply_config() {
    const bool want = enabled();
    if (want == want_watchers_)
        return;
    want_watchers_ = want;
    if (want)
        start_watchers();
    else
        stop_watchers();
    if (!want) {
        items_.clear();
        changed_.emit();
    }
}

// Only one set of watchers should feed cliphist: if another process already
// runs them (Noctalia's ClipboardService, an exec-once line), ours stay off.
void Clipboard::start_watchers() {
    try {
        auto pgrep = Gio::Subprocess::create({"pgrep", "-f", kWatcherPattern},
                                             Gio::Subprocess::Flags::STDOUT_SILENCE |
                                                 Gio::Subprocess::Flags::STDERR_SILENCE);
        pgrep->wait_async([this, pgrep](Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                pgrep->wait_finish(result);
            } catch (const Glib::Error&) {
            }
            watchers_checked_ = true;
            if (!want_watchers_)
                return;
            if (pgrep->get_successful()) {
                g_message("clipboard: wl-paste watchers already running elsewhere — not starting ours");
                return;
            }
            for (std::size_t i = 0; i < 2; ++i)
                spawn_watcher(i);
        });
    } catch (const Glib::Error& e) {
        g_warning("clipboard: pgrep failed (%s) — starting watchers anyway", e.what());
        for (std::size_t i = 0; i < 2; ++i)
            spawn_watcher(i);
    }
}

void Clipboard::spawn_watcher(std::size_t index) {
    if (watchers_[index])
        return;
    try {
        auto launcher = Gio::SubprocessLauncher::create(Gio::Subprocess::Flags::STDOUT_SILENCE |
                                                        Gio::Subprocess::Flags::STDERR_SILENCE);
        // die with the shell instead of lingering as orphans across restarts
        launcher->set_child_setup([] { prctl(PR_SET_PDEATHSIG, SIGTERM); });
        auto proc = launcher->spawn(kWatchCommands[index]);
        watchers_[index] = proc;
        proc->wait_async([this, index, proc](Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                proc->wait_finish(result);
            } catch (const Glib::Error&) {
            }
            if (watchers_[index] != proc)
                return;
            watchers_[index].reset();
            if (!want_watchers_)
                return;
            // Noctalia restarts a watcher 1s after it exits
            restart_timers_[index] = Glib::signal_timeout().connect(
                [this, index] {
                    if (want_watchers_)
                        spawn_watcher(index);
                    return false;
                },
                1000);
        });
    } catch (const Glib::Error& e) {
        g_warning("clipboard: starting %s watcher failed: %s",
                  index == 0 ? "text" : "image", e.what());
    }
}

void Clipboard::stop_watchers() {
    for (std::size_t i = 0; i < 2; ++i) {
        restart_timers_[i].disconnect();
        if (watchers_[i]) {
            auto proc = watchers_[i];
            watchers_[i].reset();
            proc->send_signal(SIGTERM);
        }
    }
}

void Clipboard::refresh() {
    if (!enabled() || list_proc_)
        return;
    loading_ = true;
    try {
        list_proc_ = Gio::Subprocess::create(
            {"cliphist", "list", "-preview-width", std::to_string(kPreviewWidth)},
            Gio::Subprocess::Flags::STDOUT_PIPE | Gio::Subprocess::Flags::STDERR_SILENCE);
    } catch (const Glib::Error& e) {
        g_warning("clipboard: cliphist list failed: %s", e.what());
        loading_ = false;
        return;
    }
    auto proc = list_proc_;
    proc->communicate_async({}, [this, proc](Glib::RefPtr<Gio::AsyncResult>& result) {
        std::string out;
        try {
            auto [stdout_bytes, stderr_bytes] = proc->communicate_finish(result);
            if (stdout_bytes) {
                gsize size = 0;
                const auto* data = static_cast<const char*>(stdout_bytes->get_data(size));
                out.assign(data, size);
            }
        } catch (const Glib::Error& e) {
            g_warning("clipboard: cliphist list: %s", e.what());
        }
        list_proc_.reset();
        loading_ = false;

        std::vector<Item> items;
        std::size_t start = 0;
        while (start < out.size()) {
            auto end = out.find('\n', start);
            if (end == std::string::npos)
                end = out.size();
            const std::string line = out.substr(start, end - start);
            start = end + 1;
            if (line.empty())
                continue;
            // cliphist prints "<id>\t<preview>"
            const auto tab = line.find('\t');
            Item item;
            item.id = line.substr(0, tab);
            if (tab != std::string::npos) {
                char* valid = g_utf8_make_valid(line.c_str() + tab + 1, -1);
                item.preview = valid;
                g_free(valid);
            }
            if (item.id.empty() || !std::all_of(item.id.begin(), item.id.end(), ::isdigit))
                continue;
            const std::string lower = lowercase(item.preview);
            item.is_image = lower.rfind("[[ binary data", 0) == 0 || lower.rfind("[image]", 0) == 0 ||
                            lower.find(" binary data ") != std::string::npos;
            if (item.is_image) {
                item.kind = Item::Kind::Image;
                item.mime = "image/*";
                parse_image_meta(item);
            } else {
                // Noctalia's browser-junk filters: UTF-16 text (null bytes),
                // Firefox's HTML wrapper
                const auto nulls = std::count(item.preview.begin(), item.preview.end(), '\0');
                if (nulls > static_cast<long>(item.preview.size() / 5))
                    continue;
                if (lower.rfind("<meta http-equiv=", 0) == 0)
                    continue;
                item.kind = classify(item.preview);
            }
            items.push_back(std::move(item));
        }
        items_ = std::move(items);

        // drop thumbnails of entries that fell off the history
        for (auto it = thumbs_.begin(); it != thumbs_.end();) {
            const bool live = std::any_of(items_.begin(), items_.end(),
                                          [&](const Item& i) { return i.id == it->first; });
            if (live) {
                ++it;
            } else {
                thumb_order_.remove(it->first);
                it = thumbs_.erase(it);
            }
        }
        changed_.emit();
    });
}

void Clipboard::decode(const std::string& id,
                       std::function<void(Glib::RefPtr<Glib::Bytes>)> cb) {
    if (!available_) {
        cb({});
        return;
    }
    Glib::RefPtr<Gio::Subprocess> proc;
    try {
        proc = Gio::Subprocess::create({"cliphist", "decode", id},
                                       Gio::Subprocess::Flags::STDOUT_PIPE |
                                           Gio::Subprocess::Flags::STDERR_SILENCE);
    } catch (const Glib::Error& e) {
        g_warning("clipboard: cliphist decode failed: %s", e.what());
        cb({});
        return;
    }
    proc->communicate_async({}, [proc, cb](Glib::RefPtr<Gio::AsyncResult>& result) {
        Glib::RefPtr<Glib::Bytes> out;
        try {
            auto [stdout_bytes, stderr_bytes] = proc->communicate_finish(result);
            out = stdout_bytes;
        } catch (const Glib::Error& e) {
            g_warning("clipboard: cliphist decode: %s", e.what());
        }
        cb(out);
    });
}

void Clipboard::thumbnail(const Item& item, int size,
                          std::function<void(Glib::RefPtr<Gdk::Texture>)> cb) {
    if (auto it = thumbs_.find(item.id); it != thumbs_.end()) {
        cb(it->second);
        return;
    }
    thumb_queue_.push_back({item.id, size, std::move(cb)});
    pump_thumbnails();
}

void Clipboard::cancel_thumbnail_requests() {
    thumb_queue_.clear();
}

void Clipboard::pump_thumbnails() {
    if (thumb_busy_ || thumb_queue_.empty())
        return;
    ThumbRequest request = std::move(thumb_queue_.front());
    thumb_queue_.pop_front();
    if (auto it = thumbs_.find(request.id); it != thumbs_.end()) {
        request.cb(it->second); // decoded meanwhile by an earlier request
        pump_thumbnails();
        return;
    }
    thumb_busy_ = true;
    const std::string id = request.id;
    const int size = request.size;
    auto cb = std::move(request.cb);
    auto done = [this, cb](Glib::RefPtr<Gdk::Texture> texture) {
        thumb_busy_ = false;
        cb(texture);
        pump_thumbnails();
    };
    decode(id, [this, id, size, cb = done](Glib::RefPtr<Glib::Bytes> bytes) {
        if (!bytes || bytes->get_size() == 0) {
            cb({});
            return;
        }
        // decode scaled — a copied 4K screenshot must not become a 4K texture
        auto stream = Gio::MemoryInputStream::create();
        stream->add_bytes(bytes);
        struct Pending {
            std::string id;
            std::function<void(Glib::RefPtr<Gdk::Texture>)> cb;
            Clipboard* self;
        };
        auto* pending = new Pending{id, cb, this};
        gdk_pixbuf_new_from_stream_at_scale_async(
            G_INPUT_STREAM(stream->gobj()), size, size, TRUE, nullptr,
            [](GObject*, GAsyncResult* async_result, gpointer data) {
                std::unique_ptr<Pending> pending(static_cast<Pending*>(data));
                GError* error = nullptr;
                GdkPixbuf* pixbuf = gdk_pixbuf_new_from_stream_finish(async_result, &error);
                Glib::RefPtr<Gdk::Texture> texture;
                if (pixbuf != nullptr) {
                    texture = Gdk::Texture::create_for_pixbuf(Glib::wrap(pixbuf));
                } else {
                    g_debug("clipboard: thumbnail decode failed: %s",
                            error != nullptr ? error->message : "?");
                }
                g_clear_error(&error);
                auto* self = pending->self;
                if (texture) {
                    self->thumbs_[pending->id] = texture;
                    self->thumb_order_.push_back(pending->id);
                    while (self->thumb_order_.size() > kThumbCacheSize) {
                        self->thumbs_.erase(self->thumb_order_.front());
                        self->thumb_order_.pop_front();
                    }
                }
                pending->cb(texture);
            },
            pending);
    });
}

bool Clipboard::paste_available() const {
    return Hyprland::get().available() || paste_available_;
}

void Clipboard::run_shell(const std::string& command, std::function<void()> on_done) {
    try {
        auto proc = Gio::Subprocess::create({"sh", "-c", command},
                                            Gio::Subprocess::Flags::STDOUT_SILENCE |
                                                Gio::Subprocess::Flags::STDERR_SILENCE);
        proc->wait_async([proc, on_done](Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                proc->wait_finish(result);
            } catch (const Glib::Error&) {
            }
            if (on_done)
                on_done();
        });
    } catch (const Glib::Error& e) {
        g_warning("clipboard: `%s` failed: %s", command.c_str(), e.what());
    }
}

// ids are digits only (checked when parsing), so they are shell-safe
void Clipboard::copy(const Item& item) {
    if (!available_)
        return;
    const std::string type = item.is_image && item.mime != "image/*" ? " --type " + item.mime : "";
    run_shell("cliphist decode " + item.id + " | wl-copy" + type);
}

// Paste = copy, then press the paste shortcut once the caller's window is
// gone and focus is back on the previous window. The shortcut is sent by
// Hyprland itself (`hl.dsp.send_shortcut`, the seat's real keyboard): wtype's
// virtual keyboard, which Noctalia uses, carries its own keymap and Chromium/
// Electron apps (VS Code) dropped its Ctrl+V while GTK apps took it. wtype
// remains the fallback outside Hyprland, with every modifier released
// explicitly (-m) — a wtype run exiting with modifiers held left Ctrl+Shift
// stuck in Hyprland's seat, which blocked touchpad workspace gestures.
// Noctalia sends Ctrl+Shift+V for all text; that is the paste key in
// terminals but means something else elsewhere (VS Code: Markdown preview),
// so the focused window's class decides: Ctrl+Shift+V in terminals, Ctrl+V
// elsewhere and for images.
void Clipboard::paste(const Item& item) {
    if (!available_)
        return;
    if (!paste_available()) {
        copy(item);
        return;
    }
    const std::string type = item.is_image && item.mime != "image/*" ? " --type " + item.mime : "";
    const std::string copy_cmd = "cliphist decode " + item.id + " | wl-copy" + type;
    auto press = [](bool shift) {
        if (Hyprland::get().available()) {
            Hyprland::get().send_shortcut(shift ? "CTRL SHIFT" : "CTRL", "V");
        } else {
            Clipboard::get().run_shell(shift ? "wtype -M ctrl -M shift -k v -m shift -m ctrl"
                                             : "wtype -M ctrl -k v -m ctrl");
        }
    };
    if (item.is_image) {
        // wl-copy forks and serves the selection from its child: give it a
        // moment before the target window asks for the data
        run_shell(copy_cmd, [press] {
            Glib::signal_timeout().connect_once([press] { press(false); }, 60);
        });
        return;
    }
    Hyprland::get().request("j/activewindow", [this, copy_cmd, press](const std::string& reply) {
        std::string klass;
        try {
            klass = lowercase(nlohmann::json::parse(reply).value("class", ""));
        } catch (const std::exception&) {
            // no active window — plain Ctrl+V
        }
        static const char* kTerminals[] = {"kitty",     "foot",       "alacritty", "wezterm",
                                           "ghostty",   "konsole",    "gnome-terminal",
                                           "terminal",  "xterm",      "urxvt",     "rxvt",
                                           "tilix",     "terminator", "st-256color", "tmux",
                                           "warp",      "rio",        "contour",   "ptyxis"};
        bool terminal = false;
        for (const char* name : kTerminals)
            if (klass.find(name) != std::string::npos)
                terminal = true;
        run_shell(copy_cmd, [press, terminal] {
            Glib::signal_timeout().connect_once([press, terminal] { press(terminal); }, 60);
        });
    });
}

void Clipboard::remove(const std::string& id) {
    if (!available_ || !std::all_of(id.begin(), id.end(), ::isdigit))
        return;
    thumbs_.erase(id);
    thumb_order_.remove(id);
    // optimistic: drop the row right away, the list refresh confirms
    items_.erase(std::remove_if(items_.begin(), items_.end(),
                                [&id](const Item& i) { return i.id == id; }),
                 items_.end());
    changed_.emit();
    try {
        auto proc = Gio::Subprocess::create({"sh", "-c", "printf '%s\\n' " + id + " | cliphist delete"},
                                            Gio::Subprocess::Flags::STDOUT_SILENCE |
                                                Gio::Subprocess::Flags::STDERR_SILENCE);
        proc->wait_async([this, proc](Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                proc->wait_finish(result);
            } catch (const Glib::Error&) {
            }
            refresh();
        });
    } catch (const Glib::Error& e) {
        g_warning("clipboard: cliphist delete failed: %s", e.what());
    }
}

void Clipboard::wipe() {
    if (!available_)
        return;
    thumbs_.clear();
    thumb_order_.clear();
    items_.clear();
    changed_.emit();
    try {
        auto proc = Gio::Subprocess::create({"cliphist", "wipe"},
                                            Gio::Subprocess::Flags::STDOUT_SILENCE |
                                                Gio::Subprocess::Flags::STDERR_SILENCE);
        proc->wait_async([this, proc](Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                proc->wait_finish(result);
            } catch (const Glib::Error&) {
            }
            refresh();
        });
    } catch (const Glib::Error& e) {
        g_warning("clipboard: cliphist wipe failed: %s", e.what());
    }
}

} // namespace hyprshell
