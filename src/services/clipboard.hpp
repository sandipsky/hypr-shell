#pragma once

#include <gdkmm.h>
#include <giomm.h>
#include <sigc++/sigc++.h>

#include <deque>
#include <functional>
#include <list>
#include <map>
#include <string>
#include <vector>

namespace hyprshell {

// Clipboard history, Noctalia's ClipboardService: the history itself lives in
// cliphist's database, fed by `wl-paste --type text|image --watch cliphist
// store` watchers that this service runs while `clipboard.enabled` (unless
// another process already runs them — Noctalia does today — so entries are
// never stored twice). Every action shells out to cliphist / wl-copy / wtype
// asynchronously (Gio::Subprocess), like Noctalia's Process objects.
class Clipboard {
public:
    struct Item {
        enum class Kind { Text, Image, Link, File, Code, Color };
        std::string id;      // cliphist entry id
        std::string preview; // cliphist's preview (first ~100 chars, or "[[ binary data … ]]")
        Kind kind = Kind::Text;
        bool is_image = false;
        std::string mime = "text/plain";
        // image meta parsed from the preview: "[[ binary data 1.2 MiB png 1920x1080 ]]"
        std::string size_text;
        std::string format;
        int width = 0;
        int height = 0;
    };

    static Clipboard& get();

    bool available() const { return available_; }             // cliphist + wl-paste + wl-copy
    bool paste_available() const { return paste_available_; } // wtype
    bool enabled() const;                                      // config && available
    bool loading() const { return loading_; }
    const std::vector<Item>& items() const { return items_; }

    void refresh(); // `cliphist list` → items(), then signal_changed
    // raw entry content (text bytes or the image file)
    void decode(const std::string& id, std::function<void(Glib::RefPtr<Glib::Bytes>)> cb);
    // decoded image scaled to fit `size` px, cached (LRU); cb gets a null
    // texture when decoding fails. Misses are decoded ONE AT A TIME from a
    // queue (Noctalia's _b64Queue): spawning a `cliphist decode` per row up
    // front took 600 ms before the window could map.
    void thumbnail(const Item& item, int size,
                   std::function<void(Glib::RefPtr<Gdk::Texture>)> cb);
    void cancel_thumbnail_requests(); // drop queued (not in-flight) decodes
    void copy(const Item& item);  // entry → clipboard
    void paste(const Item& item); // copy, then Ctrl(+Shift)+V via wtype
    void remove(const std::string& id);
    void wipe();

    sigc::signal<void()>& signal_changed() { return changed_; } // items() changed

private:
    Clipboard();
    Clipboard(const Clipboard&) = delete;
    Clipboard& operator=(const Clipboard&) = delete;

    void apply_config();
    void start_watchers();
    void stop_watchers();
    void spawn_watcher(std::size_t index);
    void run_shell(const std::string& command); // fire-and-forget `sh -c`

    bool available_ = false;
    bool paste_available_ = false;
    bool loading_ = false;
    bool want_watchers_ = false;
    bool watchers_checked_ = false; // pgrep for foreign watchers done
    std::vector<Item> items_;
    Glib::RefPtr<Gio::Subprocess> watchers_[2]; // text, image
    sigc::connection restart_timers_[2];
    Glib::RefPtr<Gio::Subprocess> list_proc_;

    // thumbnail LRU + sequential decode queue
    struct ThumbRequest {
        std::string id;
        int size = 0;
        std::function<void(Glib::RefPtr<Gdk::Texture>)> cb;
    };
    void pump_thumbnails();
    std::map<std::string, Glib::RefPtr<Gdk::Texture>> thumbs_;
    std::list<std::string> thumb_order_;
    std::deque<ThumbRequest> thumb_queue_;
    bool thumb_busy_ = false;

    sigc::signal<void()> changed_;
};

} // namespace hyprshell
