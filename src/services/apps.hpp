#pragma once

#include <giomm.h>
#include <sigc++/sigc++.h>

#include <string>
#include <vector>

namespace hyprshell {

// Installed applications for the launcher (Noctalia's ApplicationsProvider
// backend): desktop entries via Gio::AppInfo, reloaded when entries change on
// disk, plus the pinned-apps store. Pins are only persisted for now — the
// future taskbar module is their consumer (they also match Noctalia's dock
// pinnedApps semantics: a list of desktop ids).
class Apps {
public:
    struct Entry {
        std::string id;          // desktop id, e.g. "firefox.desktop"
        std::string name;
        std::string description; // generic name, else comment
        std::string exec_name;   // executable basename, for search
        Glib::RefPtr<Gio::AppInfo> info;
        Glib::RefPtr<Gio::Icon> icon;
    };

    static Apps& get();

    Apps(const Apps&) = delete;
    Apps& operator=(const Apps&) = delete;

    const std::vector<Entry>& entries() const { return entries_; }

    // Launches detached via GIO (handles Terminal=true entries etc.).
    void launch(const Entry& entry);

    bool is_pinned(const std::string& id) const;
    void toggle_pinned(const std::string& id); // persists asynchronously

    // App list reloaded or pins changed.
    sigc::signal<void()>& signal_changed() { return changed_; }

private:
    Apps();

    void reload();
    void load_pins();
    void save_pins();

    std::vector<Entry> entries_;
    std::vector<std::string> pinned_;
    std::string pins_path_;
    Glib::RefPtr<Gio::AppInfoMonitor> monitor_;
    sigc::signal<void()> changed_;
};

// Fuzzy match for launcher searches, normalized like Noctalia's FuzzySort
// usage: < 0 means no match, otherwise 0..1 (higher = better). `query_lc`
// must already be lowercased.
double fuzzy_score(const std::string& query_lc, const std::string& text);

} // namespace hyprshell
