#include "settings/search.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace hyprshell::settings {
namespace {

constexpr int kMaxResults = 40;
constexpr guint kHighlightMs = 1800;

struct Hit {
    GtkWidget* row = nullptr; // nullptr = the page itself
    std::string title;
    std::string crumb;       // "Bar › Workspaces › Behaviour"
    const SidebarPage* page = nullptr;
    std::string nav_tag;     // module subpage tag (Bar page only), "" = root
    int score = 0;
};

struct Search {
    SearchTargets t;
    GtkWidget* bar = nullptr;       // GtkSearchBar
    GtkWidget* entry = nullptr;     // GtkSearchEntry
    GtkWidget* side_stack = nullptr; // "pages" / "results" / "empty"
    GtkWidget* results = nullptr;   // GtkListBox
    std::vector<std::unique_ptr<Hit>> hits;
};

std::string lowercase(const char* text) {
    if (text == nullptr)
        return "";
    gchar* lower = g_utf8_strdown(text, -1);
    std::string result(lower);
    g_free(lower);
    return result;
}

// Titles are Pango markup on AdwPreferencesRow by default (use-markup); ours
// are plain text, but strip tags defensively for display/matching.
std::string plain_title(const char* markup) {
    if (markup == nullptr)
        return "";
    gchar* text = nullptr;
    if (pango_parse_markup(markup, -1, 0, nullptr, &text, nullptr, nullptr) && text != nullptr) {
        std::string result(text);
        g_free(text);
        return result;
    }
    return markup;
}

// 0 = no match; higher is better.
int match_score(const std::string& query, const std::string& title_lc,
                const std::string& subtitle_lc, const std::string& crumb_lc) {
    if (title_lc.rfind(query, 0) == 0)
        return 5;
    if (title_lc.find(query) != std::string::npos)
        return 4;
    // every whitespace-separated word of the query somewhere in the title
    bool all_words = true;
    gchar** words = g_strsplit(query.c_str(), " ", -1);
    int word_count = 0;
    for (gchar** w = words; *w != nullptr; ++w) {
        if (**w == '\0')
            continue;
        ++word_count;
        if (title_lc.find(*w) == std::string::npos)
            all_words = false;
    }
    g_strfreev(words);
    if (word_count > 1 && all_words)
        return 3;
    if (subtitle_lc.find(query) != std::string::npos)
        return 2;
    if (crumb_lc.find(query) != std::string::npos)
        return 1;
    return 0;
}

struct WalkContext {
    const SidebarPage* page = nullptr;
    std::string sub_title; // AdwNavigationPage title when not the root
    std::string nav_tag;
    std::string group;     // AdwPreferencesGroup title
    std::string expander;  // enclosing AdwExpanderRow title
};

// Row hidden by state (e.g. a dependent row while its switch is off)? Checks
// the row and its ancestors up to the preferences page — never above it,
// since stack/navigation children are legitimately not the visible child.
bool row_effectively_visible(GtkWidget* row) {
    for (GtkWidget* w = row; w != nullptr && !ADW_IS_PREFERENCES_PAGE(w);
         w = gtk_widget_get_parent(w))
        if (!gtk_widget_get_visible(w))
            return false;
    return true;
}

std::string join_crumb(const WalkContext& ctx) {
    std::string crumb = ctx.page->title;
    if (!ctx.sub_title.empty() && ctx.sub_title != ctx.page->title)
        crumb += " › " + ctx.sub_title;
    if (!ctx.group.empty())
        crumb += " › " + ctx.group;
    if (!ctx.expander.empty())
        crumb += " › " + ctx.expander;
    return crumb;
}

void walk(GtkWidget* widget, WalkContext ctx, const std::string& query,
          std::vector<std::unique_ptr<Hit>>& out) {
    if (ADW_IS_NAVIGATION_PAGE(widget)) {
        const char* tag = adw_navigation_page_get_tag(ADW_NAVIGATION_PAGE(widget));
        ctx.nav_tag = tag != nullptr ? tag : "";
        const char* title = adw_navigation_page_get_title(ADW_NAVIGATION_PAGE(widget));
        ctx.sub_title = title != nullptr ? title : "";
    } else if (ADW_IS_PREFERENCES_GROUP(widget)) {
        const char* title = adw_preferences_group_get_title(ADW_PREFERENCES_GROUP(widget));
        ctx.group = plain_title(title);
    } else if (ADW_IS_PREFERENCES_ROW(widget)) {
        const std::string title =
            plain_title(adw_preferences_row_get_title(ADW_PREFERENCES_ROW(widget)));
        if (!title.empty() && row_effectively_visible(widget)) {
            std::string subtitle;
            if (ADW_IS_ACTION_ROW(widget))
                subtitle = plain_title(adw_action_row_get_subtitle(ADW_ACTION_ROW(widget)));
            else if (ADW_IS_EXPANDER_ROW(widget))
                subtitle = plain_title(adw_expander_row_get_subtitle(ADW_EXPANDER_ROW(widget)));
            const std::string crumb = join_crumb(ctx);
            const int score = match_score(query, lowercase(title.c_str()),
                                          lowercase(subtitle.c_str()), lowercase(crumb.c_str()));
            if (score > 0) {
                auto hit = std::make_unique<Hit>();
                hit->row = widget;
                hit->title = title;
                hit->crumb = crumb;
                hit->page = ctx.page;
                hit->nav_tag = ctx.nav_tag;
                hit->score = score;
                out.push_back(std::move(hit));
            }
        }
        if (ADW_IS_EXPANDER_ROW(widget))
            ctx.expander = title;
        else
            return; // action/entry/combo rows hold no further rows
    }
    for (GtkWidget* child = gtk_widget_get_first_child(widget); child != nullptr;
         child = gtk_widget_get_next_sibling(child))
        walk(child, ctx, query, out);
}

void collect(Search* s, const std::string& query) {
    s->hits.clear();
    if (s->t.prepare != nullptr)
        s->t.prepare(s->t.prepare_data);
    for (int i = 0; i < s->t.page_count; ++i) {
        const SidebarPage& page = s->t.pages[i];
        // the page itself
        const std::string title_lc = lowercase(page.title);
        const int page_score = match_score(query, title_lc, "", "");
        if (page_score > 0) {
            auto hit = std::make_unique<Hit>();
            hit->title = page.title;
            hit->crumb = "Settings page";
            hit->page = &page;
            hit->score = page_score + 1; // pages float above their rows
            s->hits.push_back(std::move(hit));
        }
        GtkWidget* child = gtk_stack_get_child_by_name(GTK_STACK(s->t.stack), page.name);
        if (child == nullptr)
            continue;
        WalkContext ctx;
        ctx.page = &page;
        walk(child, ctx, query, s->hits);
    }
    std::stable_sort(s->hits.begin(), s->hits.end(),
                     [](const auto& a, const auto& b) { return a->score > b->score; });
    if (s->hits.size() > static_cast<size_t>(kMaxResults))
        s->hits.resize(kMaxResults);
}

void clear_results(Search* s) {
    while (GtkWidget* child = gtk_widget_get_first_child(s->results))
        gtk_list_box_remove(GTK_LIST_BOX(s->results), child);
}

void show_side(Search* s, const char* name) {
    gtk_stack_set_visible_child_name(GTK_STACK(s->side_stack), name);
}

int sidebar_index(Search* s, const SidebarPage* page) {
    for (int i = 0; i < s->t.page_count; ++i)
        if (&s->t.pages[i] == page)
            return i;
    return -1;
}

gboolean unhighlight(gpointer row_ptr) {
    GtkWidget* row = static_cast<GtkWidget*>(row_ptr);
    gtk_widget_remove_css_class(row, "search-hit");
    g_object_unref(row);
    return G_SOURCE_REMOVE;
}

struct ScrollPending {
    GtkWidget* row;
    int frames;
};

gboolean scroll_when_allocated(GtkWidget* viewport, GdkFrameClock*, gpointer data) {
    auto* p = static_cast<ScrollPending*>(data);
    if (++p->frames > 120) // gave up: page never showed
        return G_SOURCE_REMOVE;
    if (gtk_widget_get_height(p->row) <= 0 || !gtk_widget_is_ancestor(p->row, viewport))
        return G_SOURCE_CONTINUE;
    gtk_viewport_scroll_to(GTK_VIEWPORT(viewport), p->row, nullptr);
    return G_SOURCE_REMOVE;
}

void activate_hit(Search* s, const Hit& hit) {
    if (s->t.prepare != nullptr)
        s->t.prepare(s->t.prepare_data);
    const int index = sidebar_index(s, hit.page);
    if (index >= 0)
        gtk_list_box_select_row(GTK_LIST_BOX(s->t.sidebar_list),
                                gtk_list_box_get_row_at_index(GTK_LIST_BOX(s->t.sidebar_list), index));

    if (g_strcmp0(hit.page->name, "bar") == 0) {
        // back to the root, then into the module subpage the row lives in
        AdwNavigationView* nav = ADW_NAVIGATION_VIEW(s->t.nav);
        while (adw_navigation_view_pop(nav)) {
        }
        if (!hit.nav_tag.empty())
            adw_navigation_view_push_by_tag(nav, hit.nav_tag.c_str());
    }

    if (hit.row == nullptr)
        return;

    // a row inside a collapsed expander must be revealed first
    GtkWidget* viewport = nullptr;
    for (GtkWidget* w = gtk_widget_get_parent(hit.row); w != nullptr; w = gtk_widget_get_parent(w)) {
        if (ADW_IS_EXPANDER_ROW(w))
            adw_expander_row_set_expanded(ADW_EXPANDER_ROW(w), TRUE);
        if (viewport == nullptr && GTK_IS_VIEWPORT(w))
            viewport = w;
    }
    if (viewport != nullptr) {
        // GtkViewport computes scroll_to from the row's PREVIOUS allocation
        // (it runs before the child is laid out), so a page shown for the
        // first time has nothing to measure yet and the request is dropped.
        // Wait until the row has been allocated once, then ask.
        auto* pending = new ScrollPending{g_object_ref(hit.row), 0};
        gtk_widget_add_tick_callback(viewport, scroll_when_allocated, pending,
                                     +[](gpointer data) {
                                         auto* p = static_cast<ScrollPending*>(data);
                                         g_object_unref(p->row);
                                         delete p;
                                     });
    }

    gtk_widget_add_css_class(hit.row, "search-hit");
    g_timeout_add(kHighlightMs, unhighlight, g_object_ref(hit.row));
}

GtkWidget* make_result_row(const Hit& hit) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_top(box, 6);
    gtk_widget_set_margin_bottom(box, 6);
    gtk_widget_set_margin_start(box, 4);
    gtk_widget_set_margin_end(box, 4);

    GtkWidget* icon = make_page_icon(*hit.page);
    gtk_box_append(GTK_BOX(box), icon);

    GtkWidget* text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(text, TRUE);
    GtkWidget* title = gtk_label_new(hit.title.c_str());
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(text), title);
    GtkWidget* crumb = gtk_label_new(hit.crumb.c_str());
    gtk_label_set_xalign(GTK_LABEL(crumb), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(crumb), PANGO_ELLIPSIZE_END);
    gtk_widget_add_css_class(crumb, "caption");
    gtk_widget_add_css_class(crumb, "dim-label");
    gtk_box_append(GTK_BOX(text), crumb);
    gtk_box_append(GTK_BOX(box), text);

    GtkWidget* row = gtk_list_box_row_new();
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
    return row;
}

void refresh(Search* s) {
    const bool mode = gtk_search_bar_get_search_mode(GTK_SEARCH_BAR(s->bar));
    const std::string query = lowercase(gtk_editable_get_text(GTK_EDITABLE(s->entry)));
    const std::string trimmed = query.find_first_not_of(' ') == std::string::npos
                                    ? ""
                                    : query.substr(query.find_first_not_of(' '));
    clear_results(s);
    if (!mode || trimmed.empty()) {
        s->hits.clear();
        show_side(s, "pages");
        return;
    }
    collect(s, trimmed);
    if (s->hits.empty()) {
        show_side(s, "empty");
        return;
    }
    for (size_t i = 0; i < s->hits.size(); ++i) {
        GtkWidget* row = make_result_row(*s->hits[i]);
        g_object_set_data(G_OBJECT(row), "hit-index", GINT_TO_POINTER(static_cast<int>(i)));
        gtk_list_box_append(GTK_LIST_BOX(s->results), row);
    }
    show_side(s, "results");
}

void on_result_activated(GtkListBox*, GtkListBoxRow* row, gpointer data) {
    Search* s = static_cast<Search*>(data);
    const int index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "hit-index"));
    if (index >= 0 && static_cast<size_t>(index) < s->hits.size())
        activate_hit(s, *s->hits[index]);
}

void on_entry_activate(GtkSearchEntry*, gpointer data) {
    Search* s = static_cast<Search*>(data);
    if (!s->hits.empty())
        activate_hit(s, *s->hits.front());
}

gboolean on_window_key(GtkEventControllerKey* controller, guint keyval, guint,
                       GdkModifierType state, gpointer data) {
    Search* s = static_cast<Search*>(data);
    if (state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK))
        return GDK_EVENT_PROPAGATE;
    if (gtk_search_bar_get_search_mode(GTK_SEARCH_BAR(s->bar)))
        return GDK_EVENT_PROPAGATE; // the entry already has the keys
    GtkWidget* focus = gtk_root_get_focus(GTK_ROOT(s->t.window));
    for (GtkWidget* w = focus; w != nullptr; w = gtk_widget_get_parent(w))
        if (GTK_IS_EDITABLE(w) || GTK_IS_TEXT(w) || GTK_IS_TEXT_VIEW(w))
            return GDK_EVENT_PROPAGATE;
    const gunichar ch = gdk_keyval_to_unicode(keyval);
    if (ch == 0 || !g_unichar_isgraph(ch)) // letters/digits only; Space toggles switches
        return GDK_EVENT_PROPAGATE;
    gtk_search_bar_set_search_mode(GTK_SEARCH_BAR(s->bar), TRUE);
    gtk_widget_grab_focus(s->entry);
    return gtk_event_controller_key_forward(controller, s->entry);
}

void on_search_action(GSimpleAction*, GVariant*, gpointer data) {
    Search* s = static_cast<Search*>(data);
    gtk_search_bar_set_search_mode(GTK_SEARCH_BAR(s->bar), TRUE);
    gtk_widget_grab_focus(s->entry);
}

} // namespace

GtkWidget* make_page_icon(const SidebarPage& page) {
    GtkWidget* icon = nullptr;
    if (g_str_has_prefix(page.icon, "glyph:")) {
        icon = gtk_label_new(page.icon + strlen("glyph:"));
        gtk_widget_add_css_class(icon, "page-glyph");
        gtk_widget_set_size_request(icon, 16, 16);
    } else {
        icon = gtk_image_new_from_icon_name(page.icon);
    }
    gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
    return icon;
}

void install_search(const SearchTargets& targets) {
    auto* s = new Search{targets};

    // toggle at the start of the sidebar header, like GNOME Settings
    GtkWidget* toggle = gtk_toggle_button_new();
    gtk_button_set_icon_name(GTK_BUTTON(toggle), "system-search-symbolic");
    gtk_widget_set_tooltip_text(toggle, "Search settings (Ctrl+F)");
    adw_header_bar_pack_start(ADW_HEADER_BAR(targets.sidebar_header), toggle);

    s->entry = gtk_search_entry_new();
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(s->entry), "Search settings");
    gtk_widget_set_hexpand(s->entry, TRUE);
    s->bar = gtk_search_bar_new();
    gtk_search_bar_set_child(GTK_SEARCH_BAR(s->bar), s->entry);
    gtk_search_bar_connect_entry(GTK_SEARCH_BAR(s->bar), GTK_EDITABLE(s->entry));
    // Type-to-search like GNOME Settings, but only while no text field has
    // focus — GtkSearchBar's own key capture would steal keys from entry rows.
    GtkEventController* keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_window_key), s);
    gtk_widget_add_controller(GTK_WIDGET(targets.window), keys);
    gtk_search_bar_set_show_close_button(GTK_SEARCH_BAR(s->bar), FALSE);
    g_object_bind_property(s->bar, "search-mode-enabled", toggle, "active",
                           static_cast<GBindingFlags>(G_BINDING_BIDIRECTIONAL |
                                                      G_BINDING_SYNC_CREATE));
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(targets.sidebar_view), s->bar);

    // sidebar content: page list, results, or "no results"
    s->results = gtk_list_box_new();
    gtk_widget_add_css_class(s->results, "navigation-sidebar");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(s->results), GTK_SELECTION_NONE);
    g_signal_connect(s->results, "row-activated", G_CALLBACK(on_result_activated), s);
    GtkWidget* results_scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(results_scroller), GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(results_scroller), s->results);

    GtkWidget* pages_scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(pages_scroller), GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(pages_scroller), targets.sidebar_list);

    GtkWidget* empty = adw_status_page_new();
    adw_status_page_set_icon_name(ADW_STATUS_PAGE(empty), "edit-find-symbolic");
    adw_status_page_set_title(ADW_STATUS_PAGE(empty), "No Results Found");
    adw_status_page_set_description(ADW_STATUS_PAGE(empty), "Try a different search");
    gtk_widget_add_css_class(empty, "compact");

    s->side_stack = gtk_stack_new();
    gtk_stack_add_named(GTK_STACK(s->side_stack), pages_scroller, "pages");
    gtk_stack_add_named(GTK_STACK(s->side_stack), results_scroller, "results");
    gtk_stack_add_named(GTK_STACK(s->side_stack), empty, "empty");
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(targets.sidebar_view), s->side_stack);

    g_signal_connect_swapped(s->entry, "search-changed", G_CALLBACK(+[](Search* s) { refresh(s); }),
                             s);
    g_signal_connect(s->entry, "activate", G_CALLBACK(on_entry_activate), s);
    g_signal_connect_swapped(s->bar, "notify::search-mode-enabled",
                             G_CALLBACK(+[](Search* s) { refresh(s); }), s);

    // Ctrl+F, like GNOME Settings
    GSimpleAction* action = g_simple_action_new("search", nullptr);
    g_signal_connect(action, "activate", G_CALLBACK(on_search_action), s);
    g_action_map_add_action(G_ACTION_MAP(targets.window), G_ACTION(action));
    g_object_unref(action);
    GtkApplication* app = gtk_window_get_application(targets.window);
    if (app != nullptr) {
        const char* accels[] = {"<Control>f", nullptr};
        gtk_application_set_accels_for_action(app, "win.search", accels);
    }

    g_object_set_data_full(G_OBJECT(targets.window), "settings-search", s,
                           [](gpointer p) { delete static_cast<Search*>(p); });

    // dev hook: HS_SETTINGS_SEARCH=<query> opens the search pre-filled;
    // HS_SETTINGS_SEARCH_OPEN=1 also activates the first result once the
    // window is mapped (screenshots without scripted key presses)
    if (const char* query = g_getenv("HS_SETTINGS_SEARCH")) {
        gtk_search_bar_set_search_mode(GTK_SEARCH_BAR(s->bar), TRUE);
        gtk_editable_set_text(GTK_EDITABLE(s->entry), query);
        if (g_getenv("HS_SETTINGS_SEARCH_OPEN") != nullptr)
            g_timeout_add(
                1500,
                +[](gpointer data) {
                    Search* s = static_cast<Search*>(data);
                    if (!s->hits.empty())
                        activate_hit(s, *s->hits.front());
                    return G_SOURCE_REMOVE;
                },
                s);
    }
}

} // namespace hyprshell::settings
