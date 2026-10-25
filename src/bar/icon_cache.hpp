#pragma once

#include <giomm.h>
#include <gtkmm.h>

#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace hyprshell {

// Rasterized application icons, shared by the app menu and the launcher.
// A Gtk::Image showing a GIcon renders its SVG (librsvg, ~5 ms each) while
// its surface paints the first frame that shows it, so a panel full of app
// icons lagged ~100 ms on its first open; GTK's own icon cache only helps the
// frames after that. This cache renders every icon once, a few per idle, into
// a texture-backed paintable that an image can show at once. Symbolic icons
// are left to Gtk::Image (they recolour from CSS).
class IconCache {
public:
    static IconCache& get();

    // The rendered paintable for `icon` at `px`, or null when not ready yet.
    Glib::RefPtr<Gdk::Paintable> find(const Glib::RefPtr<Gio::Icon>& icon, int px) const;

    // Queue every icon not yet rendered at `px`; the icon theme, scale and
    // text direction come from `context`. signal_rendered fires after each
    // idle batch so panels can swap their pending images.
    void request(Gtk::Widget& context, const std::vector<Glib::RefPtr<Gio::Icon>>& icons, int px);

    sigc::signal<void()>& signal_rendered() { return rendered_; }

private:
    IconCache() = default;
    struct Job {
        Glib::RefPtr<Gio::Icon> icon;
        int px;
    };
    std::string key(const Glib::RefPtr<Gio::Icon>& icon, int px) const;
    bool work();

    std::unordered_map<std::string, Glib::RefPtr<Gdk::Paintable>> cache_;
    std::unordered_set<std::string> skipped_; // symbolic / unresolvable: Gtk::Image's job
    std::unordered_set<std::string> queued_;
    std::deque<Job> queue_;
    Glib::RefPtr<Gtk::IconTheme> theme_;
    int scale_ = 1;
    Gtk::TextDirection direction_ = Gtk::TextDirection::LTR;
    sigc::connection idle_;
    sigc::signal<void()> rendered_;
};

} // namespace hyprshell
