// GNOME-Settings-style search for hypr-shell-settings: a search toggle at the
// start of the sidebar header, a GtkSearchBar under it, and a results list
// that replaces the page list while a query is typed. The index is not a
// hand-written table — every AdwPreferencesRow reachable from the content
// stack is found by walking the widget tree at query time, so new rows are
// searchable the moment they exist. Activating a result selects the sidebar
// page, pushes the module subpage when the row lives in one, scrolls the row
// into view and flashes it (`.search-hit`).
#pragma once

#include <adwaita.h>

namespace hyprshell::settings {

// One sidebar entry: GtkStack child name, label, icon. `icon` is a symbolic
// icon-theme name, or "glyph:<codepoint>" for a tabler glyph from the
// bundled noctalia-tabler-icons font (the launcher's rocket has no Adwaita
// equivalent) — build it with make_page_icon().
struct SidebarPage {
    const char* name;
    const char* title;
    const char* icon;
};

// 16px icon widget for a sidebar row or search result.
GtkWidget* make_page_icon(const SidebarPage& page);

struct SearchTargets {
    GtkWindow* window;        // key capture + Ctrl+F action
    GtkWidget* sidebar_header; // AdwHeaderBar the toggle goes into
    GtkWidget* sidebar_view;   // AdwToolbarView whose content becomes pages/results
    GtkWidget* sidebar_list;   // GtkListBox of the pages (already built)
    GtkWidget* stack;          // content GtkStack
    GtkWidget* nav;            // the Bar page's AdwNavigationView (stack child "bar")
    const SidebarPage* pages;
    int page_count;
};

void install_search(const SearchTargets& targets);

} // namespace hyprshell::settings
