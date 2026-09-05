#include "bar/launcher_window.hpp"

#include "services/apps.hpp"
#include "services/config.hpp"
#include "services/math_eval.hpp"
#include "services/session.hpp"

#include <gtk4-layer-shell.h>

#include <algorithm>
#include <cctype>

namespace hyprshell {

namespace {

// noctalia-tabler-icons glyphs (\u escapes — never literal PUA)
constexpr const char* kIconCalculator = "\uEB80";
constexpr const char* kIconSettings = "\uEB20";
constexpr const char* kIconWorld = "\uEB54";

// Noctalia list metrics (default density): 36px badge, 60px entry.
constexpr int kIconSize = 36;
// Noctalia's overlay panel: max(25% of screen, 500 + 2*26) x max(50%, 600).
constexpr int kMinPanelWidth = 552;
constexpr int kMinPanelHeight = 600;
constexpr int kPageJump = 10; // floor(600 / entryHeight)
// Spotlight mode: everything around the list (panel padding, search input,
// spacing, footer) — subtracted from the panel cap to cap the list itself.
constexpr int kPanelChromeHeight = 140;
constexpr double kListAnimMs = 260.0; // Spotlight growth, soft ease-out

std::string trimmed(const std::string& text) {
    const auto begin = text.find_first_not_of(" \t\n");
    if (begin == std::string::npos)
        return "";
    const auto end = text.find_last_not_of(" \t\n");
    return text.substr(begin, end - begin + 1);
}

std::string lowercase(const std::string& text) {
    std::string out = text;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return out;
}

} // namespace

LauncherWindow::LauncherWindow() {
    set_decorated(false);
    add_css_class("launcher");

    // Layer-shell before mapping: fullscreen overlay above everything, with
    // exclusive keyboard focus so typing works over any window (Noctalia's
    // LauncherOverlayWindow: Overlay layer, ExclusionMode.Ignore, exclusive
    // keyboard).
    auto* window = GTK_WINDOW(gobj());
    gtk_layer_init_for_window(window);
    gtk_layer_set_namespace(window, "hypr-shell-launcher");
    gtk_layer_set_layer(window, GTK_LAYER_SHELL_LAYER_OVERLAY);
    gtk_layer_set_keyboard_mode(window, GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
    gtk_layer_set_exclusive_zone(window, -1);
    for (auto edge : {GTK_LAYER_SHELL_EDGE_TOP, GTK_LAYER_SHELL_EDGE_BOTTOM,
                      GTK_LAYER_SHELL_EDGE_LEFT, GTK_LAYER_SHELL_EDGE_RIGHT})
        gtk_layer_set_anchor(window, edge, true);

    // dimmed backdrop (the window background) — clicking it closes
    auto backdrop_click = Gtk::GestureClick::create();
    backdrop_click->signal_released().connect(
        [this](int, double, double) { close_launcher(); });
    backdrop_.add_controller(backdrop_click);

    // centered panel
    panel_.add_css_class("launcher-panel");
    panel_.set_halign(Gtk::Align::CENTER);
    panel_.set_valign(Gtk::Align::CENTER);
    panel_width_ = kMinPanelWidth;
    panel_max_height_ = kMinPanelHeight;

    search_.add_css_class("launcher-search");
    search_.set_placeholder_text("Search entries...");
    search_.signal_changed().connect([this] {
        if (get_visible())
            update_results();
    });
    panel_.append(search_);

    scroller_.set_child(list_);
    scroller_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    // a real scrollbar (not the hover-only overlay one) when the list overflows
    scroller_.set_overlay_scrolling(false);
    panel_.append(scroller_);

    divider_.add_css_class("launcher-divider");
    footer_.append(divider_);
    count_label_.add_css_class("launcher-count");
    count_label_.set_justify(Gtk::Justification::CENTER);
    footer_.append(count_label_);
    panel_.append(footer_);

    overlay_.set_child(backdrop_);
    overlay_.add_overlay(panel_);
    set_child(overlay_);

    // hover only selects once the mouse really moved after opening
    auto motion = Gtk::EventControllerMotion::create();
    motion->signal_motion().connect([this](double x, double y) {
        if (!mouse_primed_) {
            mouse_primed_ = true;
            mouse_x_ = x;
            mouse_y_ = y;
            return;
        }
        if (std::abs(x - mouse_x_) + std::abs(y - mouse_y_) >= 5.0)
            mouse_active_ = true;
    });
    panel_.add_controller(motion);

    auto key = Gtk::EventControllerKey::create();
    key->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    key->signal_key_pressed().connect(
        sigc::mem_fun(*this, &LauncherWindow::on_key_pressed), false);
    add_controller(key);

    Apps::get(); // build the app index up front, not on first open
}

void LauncherWindow::toggle() {
    if (get_visible())
        close_launcher();
    else
        open();
}

void LauncherWindow::open() {
    if (get_visible())
        return;
    mouse_active_ = false;
    mouse_primed_ = false;
    // dev hook: HS_LAUNCHER_QUERY pre-fills the search on every open
    const char* preset = g_getenv("HS_LAUNCHER_QUERY");
    search_.set_text(preset ? preset : "");
    update_results();
    present();
    search_.grab_focus();
    search_.set_position(-1);

    // Noctalia panel sizing: max(25% of the screen, 552) x max(50%, 600).
    // The window spans the whole output, so its own size is the screen's —
    // known only after the first allocation.
    add_tick_callback([this](const Glib::RefPtr<Gdk::FrameClock>&) {
        if (get_width() <= 1)
            return true;
        panel_width_ = std::max(kMinPanelWidth, get_width() / 4);
        panel_max_height_ = std::max(kMinPanelHeight, get_height() / 2);
        // Spotlight mode: the panel's top edge is pinned where the fixed
        // (show_all_apps) panel's top sits, so both modes put the search box
        // in the same place; fully grown it matches that panel's footprint.
        panel_top_margin_ = std::max(0, (get_height() - panel_max_height_) / 2);
        spotlight_list_max_ = panel_max_height_ - kPanelChromeHeight;
        apply_panel_layout();
        return false;
    });
}

void LauncherWindow::close_launcher() {
    set_visible(false);
}

// -- results ------------------------------------------------------------------

void LauncherWindow::update_results() {
    results_.clear();
    const std::string query = trimmed(search_.get_text());

    if (query.empty()) {
        // browsing mode: every app, alphabetical (Noctalia's empty search)
        if (Config::get().launcher().show_all_apps) {
            for (const auto& app : Apps::get().entries()) {
                Result r;
                r.name = app.name;
                r.description = app.description;
                r.gicon = app.icon;
                r.app_id = app.id;
                auto entry = app;
                r.activate = [entry] { Apps::get().launch(entry); };
                results_.push_back(std::move(r));
            }
            std::sort(results_.begin(), results_.end(),
                      [](const Result& a, const Result& b) {
                          return lowercase(a.name) < lowercase(b.name);
                      });
        }
    } else {
        add_app_results(query);
        add_calc_result(query);
        add_settings_results(query);
        add_session_results(query);
        add_web_result(query);
        // higher score first (Noctalia: apps 0..1, session -1.., settings
        // -2.., web fixed last); stable keeps each provider's own order
        std::stable_sort(results_.begin(), results_.end(),
                         [](const Result& a, const Result& b) {
                             return a.score > b.score;
                         });
    }

    selected_ = 0;
    rebuild_rows();

    if (results_.empty())
        count_label_.set_text(query.empty() ? "" : "No results");
    else
        count_label_.set_text(std::to_string(results_.size()) +
                              (results_.size() == 1 ? " result" : " results"));
    apply_panel_layout();
}

// With show_all_apps on the panel is a fixed Noctalia-style box; with it off
// it behaves like macOS Spotlight — just the input box, pinned in place, with
// the list growing downward (animated) up to the screen-derived cap.
void LauncherWindow::apply_panel_layout() {
    auto& cfg = Config::get().launcher();
    const bool spotlight = !cfg.show_all_apps;
    const bool query_empty = trimmed(search_.get_text()).empty();

    if (spotlight) {
        // pin the top edge so the input never moves while the panel grows
        panel_.set_valign(panel_top_margin_ > 0 ? Gtk::Align::START
                                                : Gtk::Align::CENTER);
        panel_.set_margin_top(panel_top_margin_);
        panel_.set_size_request(panel_width_, -1); // content-sized height
        scroller_.set_vexpand(false);
        scroller_.set_propagate_natural_height(true);
        footer_.set_visible(cfg.show_result_count && !query_empty);
        if (results_.empty()) {
            list_anim_running_ = false;
            scroller_.set_visible(false);
            scroller_.set_min_content_height(0);
            scroller_.set_max_content_height(0);
        } else {
            scroller_.set_visible(true);
            int min_h = 0, natural_h = 0, min_b = 0, nat_b = 0;
            list_.measure(Gtk::Orientation::VERTICAL, panel_width_ - 26, min_h,
                          natural_h, min_b, nat_b);
            const int cap = std::max(spotlight_list_max_, 120);
            animate_list_height(std::min(natural_h, cap));
        }
    } else {
        list_anim_running_ = false;
        panel_.set_valign(Gtk::Align::CENTER);
        panel_.set_margin_top(0);
        panel_.set_size_request(panel_width_, panel_max_height_);
        scroller_.set_vexpand(true);
        scroller_.set_propagate_natural_height(false);
        scroller_.set_min_content_height(-1);
        scroller_.set_max_content_height(-1);
        scroller_.set_visible(true);
        footer_.set_visible(cfg.show_result_count);
    }
}

// Animates the scroller between its current and target height by driving
// min/max content height together — propagate_natural_height then makes the
// panel follow, so the growth eases downward instead of jumping.
void LauncherWindow::animate_list_height(int target) {
    list_anim_from_ = scroller_.get_visible() ? scroller_.get_height() : 0;
    list_anim_target_ = target;
    list_anim_start_us_ = 0;
    if (list_anim_from_ == target) {
        scroller_.set_min_content_height(target);
        scroller_.set_max_content_height(target);
        return;
    }
    if (list_anim_running_)
        return; // the running tick picks up the new from/target
    list_anim_running_ = true;
    add_tick_callback([this](const Glib::RefPtr<Gdk::FrameClock>& clock) {
        if (!list_anim_running_)
            return false; // cancelled (mode switch / list hidden)
        const gint64 now = clock->get_frame_time();
        if (list_anim_start_us_ == 0)
            list_anim_start_us_ = now;
        const double t =
            std::clamp((now - list_anim_start_us_) / (kListAnimMs * 1000.0), 0.0, 1.0);
        const double eased = 1.0 - std::pow(1.0 - t, 4); // ease-out quart
        const int h = static_cast<int>(std::lround(
            list_anim_from_ + (list_anim_target_ - list_anim_from_) * eased));
        scroller_.set_min_content_height(h);
        scroller_.set_max_content_height(h);
        if (t < 1.0)
            return true;
        list_anim_running_ = false;
        return false;
    });
}

void LauncherWindow::add_app_results(const std::string& query) {
    const std::string query_lc = lowercase(query);
    std::vector<Result> matches;
    for (const auto& app : Apps::get().entries()) {
        // like Noctalia's FuzzySort keys: name, comment/generic, executable
        double score = fuzzy_score(query_lc, app.name);
        score = std::max(score, fuzzy_score(query_lc, app.description));
        score = std::max(score, fuzzy_score(query_lc, app.exec_name));
        if (score < 0)
            continue;
        Result r;
        r.name = app.name;
        r.description = app.description;
        r.gicon = app.icon;
        r.app_id = app.id;
        r.score = score;
        auto entry = app;
        r.activate = [entry] { Apps::get().launch(entry); };
        matches.push_back(std::move(r));
    }
    std::stable_sort(matches.begin(), matches.end(),
                     [](const Result& a, const Result& b) { return a.score > b.score; });
    if (matches.size() > 20)
        matches.resize(20); // Noctalia's limit
    for (auto& m : matches)
        results_.push_back(std::move(m));
}

void LauncherWindow::add_calc_result(const std::string& query) {
    if (!math_eval::is_math_expression(query))
        return;
    const auto value = math_eval::evaluate(query);
    if (!value)
        return;
    Result r;
    r.name = math_eval::format_result(*value);
    r.description = "Press Enter to copy result";
    r.glyph = kIconCalculator;
    r.score = 0.0; // like Noctalia: ties with the weakest app matches
    const std::string text = r.name;
    r.activate = [text] { spawn_detached({"wl-copy", text}); };
    results_.push_back(std::move(r));
}

void LauncherWindow::add_settings_results(const std::string& query) {
    if (!Config::get().launcher().enable_settings_search || query.size() < 2)
        return;

    // Static index of hypr-shell-settings entries; activation opens the app
    // directly on the right page via its HS_SETTINGS_PAGE hook (Noctalia's
    // SettingsProvider opens its settings panel at the matched entry).
    struct Entry {
        const char* label;
        const char* breadcrumb;
        const char* page; // HS_SETTINGS_PAGE tag; "" = main Bar page
    };
    static constexpr Entry kIndex[] = {
        {"Bar position", "Bar", ""},
        {"Bar visibility", "Bar", ""},
        {"Bar background opacity", "Bar", ""},
        {"Bar modules", "Bar", ""},
        {"Module layout", "Bar", ""},
        {"Workspaces mode", "Bar › Workspaces", "workspaces"},
        {"Fixed workspace count", "Bar › Workspaces", "workspaces"},
        {"Workspace scroll wrap-around", "Bar › Workspaces", "workspaces"},
        {"Clock format", "Bar › Clock", "clock"},
        {"First day of week", "Bar › Clock", "clock"},
        {"Active window title", "Bar › Active window", "active_window"},
        {"Window icon", "Bar › Active window", "active_window"},
        {"Bluetooth auto-connect", "Bar › Bluetooth", "bluetooth"},
        {"Control center cards", "Bar › Control center", "control_center"},
        {"Media player card", "Bar › Control center", "control_center"},
        {"System monitor", "Bar › Control center", "control_center"},
        {"App menu icon", "Bar › App menu", "app_menu"},
        {"App menu label", "Bar › App menu", "app_menu"},
        {"App menu buttons", "Bar › App menu", "app_menu"},
        {"App menu grid columns", "Bar › App menu", "app_menu"},
        {"App menu two-line names", "Bar › App menu", "app_menu"},
        {"App menu search bar", "Bar › App menu", "app_menu"},
        {"App menu keybind", "Bar › App menu", "app_menu"},
        {"Battery panel cards", "Bar › Battery", "battery"},
        {"Brightness slider", "Bar › Battery", "battery"},
        {"Power profiles", "Bar › Battery", "battery"},
        {"Notification unread badge", "Bar › Notifications", "notifications"},
        {"Session menu style", "Session menu", "session_page"},
        {"Session menu fullscreen layout", "Session menu", "session_page"},
        {"Session menu actions", "Session menu", "session_page"},
        {"Wallpaper", "Wallpaper", "wallpaper_page"},
        {"Wallpaper folder", "Wallpaper", "wallpaper_page"},
        {"Wallpaper fill mode", "Wallpaper", "wallpaper_page"},
        {"Wallpaper transitions", "Wallpaper", "wallpaper_page"},
        {"Wallpaper slideshow", "Wallpaper", "wallpaper_page"},
        {"Night light", "Night light", "night_light_page"},
        {"Night light temperature", "Night light", "night_light_page"},
        {"Night light schedule", "Night light", "night_light_page"},
        {"Blue light filter", "Night light", "night_light_page"},
        {"Lock screen background", "Lock screen", "lock_page"},
        {"Lock screen blur", "Lock screen", "lock_page"},
        {"Idle timeouts", "Idle", "idle_page"},
        {"Turn off screen", "Idle", "idle_page"},
        {"Lock screen timeout", "Idle", "idle_page"},
        {"Suspend timeout", "Idle", "idle_page"},
        {"On-screen display", "On-screen display", "osd_page"},
        {"OSD position", "On-screen display", "osd_page"},
        {"OSD orientation", "On-screen display", "osd_page"},
        {"Volume and brightness overlay", "On-screen display", "osd_page"},
        {"Launcher search providers", "Launcher", "launcher_page"},
        {"Session search", "Launcher", "launcher_page"},
        {"Web search", "Launcher", "launcher_page"},
        {"Enable notifications", "Notifications", "notifications_page"},
        {"Do not disturb", "Notifications", "notifications_page"},
        {"Notification position", "Notifications", "notifications_page"},
        {"Notification duration", "Notifications", "notifications_page"},
        {"Notification history", "Notifications", "notifications_page"},
        {"Notification sounds", "Notifications", "notifications_page"},
        {"Notification filter rules", "Notifications", "notifications_page"},
        {"Wi-Fi hotspot", "Hotspot", "hotspot_page"},
        {"Share internet connection", "Hotspot", "hotspot_page"},
        {"Hotspot password", "Hotspot", "hotspot_page"},
        {"VPN", "VPN", "vpn_page"},
        {"VPN profiles", "VPN", "vpn_page"},
        {"Import VPN configuration", "VPN", "vpn_page"},
        {"WireGuard", "VPN", "vpn_page"},
        {"OpenVPN", "VPN", "vpn_page"},
        {"About", "About", "about_page"},
        {"System information", "About", "about_page"},
        {"Hardware model", "About", "about_page"},
        {"Kernel version", "About", "about_page"},
        {"Hyprland version", "About", "about_page"},
    };

    const std::string query_lc = lowercase(query);
    std::vector<Result> matches;
    for (const auto& entry : kIndex) {
        const double score = std::max(fuzzy_score(query_lc, entry.label),
                                      fuzzy_score(query_lc, entry.breadcrumb));
        if (score < 0)
            continue;
        Result r;
        r.name = entry.label;
        r.description = std::string("Settings › ") + entry.breadcrumb;
        r.glyph = kIconSettings;
        r.score = score - 2.0; // Noctalia ranks settings below apps and session
        const std::string page = entry.page;
        r.activate = [page] { open_settings(page); };
        matches.push_back(std::move(r));
    }
    std::stable_sort(matches.begin(), matches.end(),
                     [](const Result& a, const Result& b) { return a.score > b.score; });
    if (matches.size() > 10)
        matches.resize(10); // Noctalia's limit
    for (auto& m : matches)
        results_.push_back(std::move(m));
}

void LauncherWindow::add_session_results(const std::string& query) {
    if (!Config::get().launcher().enable_session_search || query.size() < 2)
        return;

    // Noctalia's SessionProvider actions + keywords, from the shared session
    // table (also the app menu's power dropdown).
    const std::string query_lc = lowercase(query);
    std::vector<Result> matches;
    for (const auto* action : enabled_session_actions()) {
        const double score = std::max(fuzzy_score(query_lc, action->label),
                                      fuzzy_score(query_lc, action->keywords));
        if (score < 0)
            continue;
        Result r;
        r.name = action->label;
        r.description = "Session menu";
        r.glyph = action->glyph;
        r.score = score - 1.0; // Noctalia ranks session actions below apps
        r.activate = [action] { run_session_action(*action); };
        matches.push_back(std::move(r));
    }
    std::stable_sort(matches.begin(), matches.end(),
                     [](const Result& a, const Result& b) { return a.score > b.score; });
    if (matches.size() > 6)
        matches.resize(6); // Noctalia's limit
    for (auto& m : matches)
        results_.push_back(std::move(m));
}

void LauncherWindow::add_web_result(const std::string& query) {
    if (!Config::get().launcher().enable_web_search)
        return;
    Result r;
    r.name = "Search the web for “" + query + "”";
    r.description = "Opens in your default browser";
    r.glyph = kIconWorld;
    r.score = -3.0; // always last
    const std::string uri =
        "https://www.google.com/search?q=" + Glib::uri_escape_string(query);
    r.activate = [uri] {
        try {
            Gio::AppInfo::launch_default_for_uri(uri);
        } catch (const Glib::Error& e) {
            g_warning("launcher: opening browser failed: %s", e.what());
        }
    };
    results_.push_back(std::move(r));
}

// -- list UI --------------------------------------------------------------------

void LauncherWindow::rebuild_rows() {
    while (auto* child = list_.get_first_child())
        list_.remove(*child);
    rows_.clear();

    for (std::size_t i = 0; i < results_.size(); ++i) {
        const auto& result = results_[i];

        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 9);
        row->add_css_class("launcher-row");

        if (!result.glyph.empty()) {
            auto* glyph = Gtk::make_managed<Gtk::Label>(result.glyph);
            glyph->add_css_class("launcher-glyph");
            glyph->set_size_request(kIconSize, kIconSize);
            row->append(*glyph);
        } else {
            auto* icon = Gtk::make_managed<Gtk::Image>();
            if (result.gicon)
                icon->set(result.gicon);
            else
                icon->set_from_icon_name("application-x-executable");
            icon->set_pixel_size(kIconSize);
            row->append(*icon);
        }

        auto* text = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
        text->set_hexpand(true);
        text->set_valign(Gtk::Align::CENTER);
        auto* name = Gtk::make_managed<Gtk::Label>(result.name);
        name->add_css_class("launcher-name");
        name->set_halign(Gtk::Align::START);
        name->set_ellipsize(Pango::EllipsizeMode::END);
        text->append(*name);
        if (!result.description.empty()) {
            auto* desc = Gtk::make_managed<Gtk::Label>(result.description);
            desc->add_css_class("launcher-desc");
            desc->set_halign(Gtk::Align::START);
            desc->set_ellipsize(Pango::EllipsizeMode::END);
            text->append(*desc);
        }
        row->append(*text);

        const int index = static_cast<int>(i);
        auto click = Gtk::GestureClick::create();
        click->signal_released().connect([this, index](int, double, double) {
            activate_index(index);
        });
        row->add_controller(click);

        auto hover = Gtk::EventControllerMotion::create();
        hover->signal_enter().connect([this, index](double, double) {
            if (mouse_active_)
                select(index, /*scroll_into_view=*/false);
        });
        row->add_controller(hover);

        list_.append(*row);
        rows_.push_back(row);
    }

    if (!rows_.empty())
        select(0, /*scroll_into_view=*/true);
}

void LauncherWindow::select(int index, bool scroll_into_view) {
    if (rows_.empty())
        return;
    index = std::clamp(index, 0, static_cast<int>(rows_.size()) - 1);

    const auto old = static_cast<std::size_t>(selected_);
    if (old < rows_.size()) {
        rows_[old]->remove_css_class("selected");
    }
    selected_ = index;
    auto* row = rows_[static_cast<std::size_t>(index)];
    row->add_css_class("selected");

    if (!scroll_into_view)
        return;
    graphene_rect_t bounds;
    if (!gtk_widget_compute_bounds(row->gobj(), GTK_WIDGET(list_.gobj()), &bounds))
        return;
    auto adjustment = scroller_.get_vadjustment();
    const double top = bounds.origin.y;
    const double bottom = top + bounds.size.height;
    if (top < adjustment->get_value())
        adjustment->set_value(top);
    else if (bottom > adjustment->get_value() + adjustment->get_page_size())
        adjustment->set_value(bottom - adjustment->get_page_size());
}

void LauncherWindow::activate_index(int index) {
    if (index < 0 || index >= static_cast<int>(results_.size()))
        return;
    // Close first so the launched app can take focus (Noctalia's
    // closeImmediately + deferred execution).
    auto activate = results_[static_cast<std::size_t>(index)].activate;
    close_launcher();
    if (activate)
        Glib::signal_idle().connect_once(activate);
}

bool LauncherWindow::on_key_pressed(guint keyval, guint, Gdk::ModifierType) {
    const int count = static_cast<int>(results_.size());
    auto wrap = [count](int index) { return ((index % count) + count) % count; };

    switch (keyval) {
    case GDK_KEY_Escape:
        close_launcher();
        return true;
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:
        activate_index(selected_);
        return true;
    case GDK_KEY_Down:
    case GDK_KEY_Tab:
        if (count > 0)
            select(wrap(selected_ + 1), true);
        return true;
    case GDK_KEY_Up:
    case GDK_KEY_ISO_Left_Tab:
        if (count > 0)
            select(wrap(selected_ - 1), true);
        return true;
    case GDK_KEY_Home:
        select(0, true);
        return true;
    case GDK_KEY_End:
        select(count - 1, true);
        return true;
    case GDK_KEY_Page_Down:
        select(std::min(selected_ + kPageJump, count - 1), true);
        return true;
    case GDK_KEY_Page_Up:
        select(std::max(selected_ - kPageJump, 0), true);
        return true;
    default:
        return false; // let the search entry have it
    }
}

} // namespace hyprshell
