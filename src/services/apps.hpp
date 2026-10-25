#pragma once

#include <giomm.h>
#include <sigc++/sigc++.h>

#include <map>
#include <string>
#include <vector>

namespace hyprshell {

// Installed applications for the launcher (Noctalia's ApplicationsProvider
// backend): desktop entries via Gio::AppInfo, reloaded when entries change on
// disk, plus the pinned-apps store shared with the taskbar (Noctalia's
// dock.pinnedApps semantics: an ordered list of desktop ids).
class Apps {
public:
    struct Entry {
        std::string id;          // desktop id, e.g. "firefox.desktop"
        std::string name;
        std::string description; // generic name, else comment
        std::string exec_name;   // executable basename, for search
        std::string icon_name;   // the Icon= key, for the taskbar's fuzzy lookup
        std::string wm_class;    // StartupWMClass=, for window → entry matching
        Glib::RefPtr<Gio::AppInfo> info;
        Glib::RefPtr<Gio::Icon> icon;
    };

    static Apps& get();

    Apps(const Apps&) = delete;
    Apps& operator=(const Apps&) = delete;

    const std::vector<Entry>& entries() const { return entries_; }

    // Entry by desktop id (case-insensitive, ".desktop" optional), else null.
    const Entry* find_by_id(const std::string& id) const;

    // Entry for a window class / app id, Noctalia's ThemeIcons.findAppEntry:
    // heuristic id / StartupWMClass match, manual and regex substitutions,
    // simple transforms (reverse-domain tail, -/_ variants), fuzzy search over
    // ids, icons and names, and finally a separator-stripped containment
    // match. Cached until the entry list reloads. Null when nothing fits.
    const Entry* lookup_for_class(const std::string& app_id);

    // Launches detached via GIO (handles Terminal=true entries etc.).
    void launch(const Entry& entry);

    // Pins compare normalized (lowercase, ".desktop" stripped) so Noctalia-
    // style ids ("google-chrome") and ours ("google-chrome.desktop") match.
    const std::vector<std::string>& pinned() const { return pinned_; }
    bool is_pinned(const std::string& id) const;
    void toggle_pinned(const std::string& id); // persists asynchronously
    void set_pinned(std::vector<std::string> ids); // reorder (taskbar drag)

    // App list reloaded or pins changed.
    sigc::signal<void()>& signal_changed() { return changed_; }

private:
    Apps();

    void reload();
    void load_pins();
    void save_pins();
    const Entry* find_app_entry(const std::string& str, int depth);

    std::vector<Entry> entries_;
    std::vector<std::string> pinned_;
    std::string pins_path_;
    std::map<std::string, const Entry*> lookup_cache_;
    Glib::RefPtr<Gio::AppInfoMonitor> monitor_;
    sigc::signal<void()> changed_;
};

// Lowercase, trimmed, trailing ".desktop" removed — the key both pin storage
// and the taskbar compare by (Noctalia's normalizeAppId plus the suffix).
std::string normalize_app_id(const std::string& id);

// Fuzzy match for launcher searches, normalized like Noctalia's FuzzySort
// usage: < 0 means no match, otherwise 0..1 (higher = better). `query_lc`
// must already be lowercased.
double fuzzy_score(const std::string& query_lc, const std::string& text);

} // namespace hyprshell
