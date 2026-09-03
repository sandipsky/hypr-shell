#include "bar/app_menu_panel.hpp"

#include "services/config.hpp"
#include "services/session.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace hyprshell {

namespace {

// noctalia-tabler-icons glyphs (\u escapes — never literal PUA)
constexpr const char* kIconSettings = "\uEB20";
constexpr const char* kIconPower = "\uEB0D";

// Fixed width; the grid area's height is fixed per open (popover-resize
// gotcha: a mapped popover surface never resizes on Hyprland, and the grid
// changes with every keystroke) — up to kGridMaxRows rows of tiles, so a
// short app list gets a short panel instead of dead space.
constexpr int kPanelWidth = 480;
constexpr int kGridMaxRows = 5;
constexpr int kGridMinHeight = 120; // room for the "no matches" label
constexpr int kGridSpacing = 6;
constexpr int kScrollbarFallbackWidth = 8; // if the scrollbar can't be measured
// Tile icon size per column count (3..8): roughly half a tile, Noctalia's
// 65%-of-cell icon badge scaled for the smaller panel.
constexpr int kIconSizes[] = {56, 48, 44, 40, 34, 30};
constexpr int kMinColumns = 3;
constexpr int kMaxColumns = 8;

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

AppMenuPanel::AppMenuPanel() : Gtk::Box(Gtk::Orientation::VERTICAL, 9) {
    add_css_class("app-menu-panel");
    set_size_request(kPanelWidth, -1);

    // -- header: [ search ..................... ] [settings] [power] ----------
    search_.add_css_class("am-search");
    search_.set_placeholder_text("Search applications...");
    search_.set_hexpand(true);
    search_.signal_changed().connect([this] { update_results(); });
    header_.append(search_);

    settings_button_.add_css_class("am-icon-btn");
    settings_button_.set_valign(Gtk::Align::CENTER);
    settings_button_.set_tooltip_text("Settings");
    auto* settings_glyph = Gtk::make_managed<Gtk::Label>(kIconSettings);
    settings_glyph->add_css_class("am-btn-glyph");
    settings_button_.set_child(*settings_glyph);
    settings_button_.signal_clicked().connect(
        [this] { run_after_close([] { open_settings(); }); });
    header_.append(settings_button_);

    // session button: the shared session menu — a nested dropdown popover,
    // or the fullscreen window (app "session" action) per session.mode
    session_button_.add_css_class("am-icon-btn");
    session_button_.set_valign(Gtk::Align::CENTER);
    session_button_.set_tooltip_text("Session");
    auto* power_glyph = Gtk::make_managed<Gtk::Label>(kIconPower);
    power_glyph->add_css_class("am-btn-glyph");
    session_button_.set_child(*power_glyph);
    session_button_.signal_clicked().connect([this] { show_session_menu(); });
    session_list_ = Gtk::make_managed<SessionMenuList>();
    session_list_->signal_activate().connect([this](const SessionAction& action) {
        session_popover_.popdown();
        const SessionAction* act = &action; // static table, see session_actions.hpp
        run_after_close([act] { run_session_action(*act); });
    });
    session_popover_.set_child(*session_list_);
    session_popover_.set_parent(session_button_);
    session_popover_.set_has_arrow(false);
    session_popover_.add_css_class("session-popover");
    session_popover_.signal_show().connect(
        [this] { session_button_.add_css_class("active"); });
    session_popover_.signal_closed().connect([this] {
        session_button_.remove_css_class("active");
        focus_default(); // the dropdown took keyboard focus
    });
    header_.append(session_button_);
    append(header_);

    // -- application grid ---------------------------------------------------------
    // tiles have a fixed size (see rebuild_grid) — the grid never stretches a
    // short result row to fill the panel
    grid_.set_row_spacing(kGridSpacing);
    grid_.set_column_spacing(kGridSpacing);
    grid_.set_halign(Gtk::Align::START);
    grid_.set_valign(Gtk::Align::START);
    scroller_.set_child(grid_);
    scroller_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    scroller_.set_overlay_scrolling(false); // a real scrollbar, like the launcher
    scroller_.set_vexpand(true);            // fills the grid area's fixed height

    empty_label_.set_text("No matching applications");
    empty_label_.add_css_class("am-empty");
    empty_label_.set_valign(Gtk::Align::CENTER);

    // a stack keeps the area's size while the grid and the label swap
    content_stack_.add(scroller_);
    content_stack_.add(empty_label_);
    content_stack_.set_transition_type(Gtk::StackTransitionType::NONE);
    content_stack_.set_vexpand(true);
    append(content_stack_);

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
    add_controller(motion);

    // CAPTURE phase so navigation keys never reach the entry
    auto key = Gtk::EventControllerKey::create();
    key->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    key->signal_key_pressed().connect(
        sigc::mem_fun(*this, &AppMenuPanel::on_key_pressed), false);
    add_controller(key);

    Apps::get().signal_changed().connect([this] {
        if (open_)
            update_results();
        else
            dirty_ = true;
    });
    Config::get().signal_changed().connect(sigc::mem_fun(*this, &AppMenuPanel::apply_config));
    apply_config();
}

AppMenuPanel::~AppMenuPanel() {
    session_popover_.unparent();
}

void AppMenuPanel::set_open(bool open) {
    open_ = open;
    if (!open) {
        session_popover_.popdown();
        return;
    }
    mouse_active_ = false;
    mouse_primed_ = false;
    // a fresh search on every open; set_text only fires "changed" when the
    // text actually changes, so an already-empty box needs the explicit path
    if (!search_.get_text().empty())
        search_.set_text("");
    else if (dirty_)
        update_results();
    else
        select(0, /*scroll_into_view=*/true);
    scroller_.get_vadjustment()->set_value(0);
    focus_default();

    // Grid area height for this open: up to kGridMaxRows rows of tiles (it
    // must not change while the popover shows — more rows scroll).
    const int cols = std::max(1, columns_);
    const int rows = (static_cast<int>(results_.size()) + cols - 1) / cols;
    int height = kGridMinHeight;
    if (rows > 0) {
        const int shown = std::min(rows, kGridMaxRows);
        height = std::max(kGridMinHeight, shown * tile_height_ + (shown - 1) * kGridSpacing);
    }
    content_stack_.set_size_request(-1, height);
}

void AppMenuPanel::show_session_menu() {
    if (!session_button_.get_visible())
        return;
    if (Config::get().session().mode == Config::Session::Mode::Fullscreen) {
        // the fullscreen menu replaces this panel — close first, then toggle
        run_after_close([] {
            if (auto app = Gio::Application::get_default())
                app->activate_action("session");
        });
        return;
    }
    session_popover_.popup();
}

void AppMenuPanel::apply_config() {
    const auto& cfg = Config::get().app_menu();
    show_search_ = cfg.show_search;
    search_.set_visible(show_search_);
    // without the entry the panel itself takes focus so the arrow/Enter
    // handling below still has a target (a focusable panel would otherwise
    // steal the initial focus from the entry)
    set_focusable(!show_search_);
    settings_button_.set_visible(cfg.show_settings_button);
    session_button_.set_visible(cfg.show_session_button);
    header_.set_visible(show_search_ || cfg.show_settings_button || cfg.show_session_button);
    header_.set_halign(show_search_ ? Gtk::Align::FILL : Gtk::Align::END);
    const int columns = std::clamp(cfg.columns, kMinColumns, kMaxColumns);
    if (columns != columns_ || cfg.multiline_labels != multiline_) {
        columns_ = columns;
        multiline_ = cfg.multiline_labels;
        if (open_)
            rebuild_grid();
        else
            dirty_ = true;
    }
}

// -- results ------------------------------------------------------------------

void AppMenuPanel::update_results() {
    dirty_ = false;
    results_.clear();
    const std::string query = trimmed(search_.get_text());
    const auto& apps = Apps::get().entries();

    if (query.empty()) {
        // browsing: every app, alphabetical (Noctalia's empty search)
        results_ = apps;
        std::sort(results_.begin(), results_.end(),
                  [](const Apps::Entry& a, const Apps::Entry& b) {
                      return lowercase(a.name) < lowercase(b.name);
                  });
    } else {
        // like Noctalia's FuzzySort keys: name, comment/generic, executable
        const std::string query_lc = lowercase(query);
        std::vector<std::pair<double, const Apps::Entry*>> matches;
        for (const auto& app : apps) {
            double score = fuzzy_score(query_lc, app.name);
            score = std::max(score, fuzzy_score(query_lc, app.description));
            score = std::max(score, fuzzy_score(query_lc, app.exec_name));
            if (score >= 0)
                matches.emplace_back(score, &app);
        }
        std::stable_sort(matches.begin(), matches.end(), [](const auto& a, const auto& b) {
            if (a.first != b.first)
                return a.first > b.first;
            return lowercase(a.second->name) < lowercase(b.second->name);
        });
        for (const auto& [score, app] : matches)
            results_.push_back(*app);
    }
    rebuild_grid();
}

void AppMenuPanel::rebuild_grid() {
    while (auto* child = grid_.get_first_child())
        grid_.remove(*child);
    tiles_.clear();
    selected_ = -1;

    const int cols = std::clamp(columns_, kMinColumns, kMaxColumns);
    const int icon_px = kIconSizes[cols - kMinColumns];
    const int lines = multiline_ ? 2 : 1;

    // Every tile gets the same fixed size. Width: the panel split into `cols`
    // columns. A scrollbar is only ever needed when the WHOLE app list
    // overflows the grid area (filtering never adds rows) — then its width is
    // reserved and it stays shown for the whole open so tiles never shift;
    // otherwise tiles span the full width with no gap on the right. Height:
    // a probe tile whose label holds `lines` lines — whatever a real name
    // needs.
    const int total_rows =
        (static_cast<int>(Apps::get().entries().size()) + cols - 1) / cols;
    const bool scrolls = total_rows > kGridMaxRows;
    scroller_.set_policy(Gtk::PolicyType::NEVER,
                         scrolls ? Gtk::PolicyType::ALWAYS : Gtk::PolicyType::AUTOMATIC);
    int scrollbar_w = 0;
    if (scrolls) {
        scrollbar_w = kScrollbarFallbackWidth;
        if (auto* scrollbar = scroller_.get_vscrollbar()) {
            int min_w = 0, nat_w = 0, min_b = 0, nat_b = 0;
            scrollbar->measure(Gtk::Orientation::HORIZONTAL, -1, min_w, nat_w, min_b, nat_b);
            if (nat_w > 0)
                scrollbar_w = nat_w;
        }
    }
    const int tile_w = (kPanelWidth - scrollbar_w - (cols - 1) * kGridSpacing) / cols;

    // `app` null builds the measuring probe with `probe_text` as its label
    auto build_tile = [&](const Apps::Entry* app, const char* probe_text) {
        auto* tile = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
        tile->add_css_class("am-tile");

        auto* icon = Gtk::make_managed<Gtk::Image>();
        if (app && app->icon)
            icon->set(app->icon);
        else
            icon->set_from_icon_name("application-x-executable");
        icon->set_pixel_size(icon_px);
        icon->set_halign(Gtk::Align::CENTER);
        tile->append(*icon);

        auto* name = Gtk::make_managed<Gtk::Label>(app ? app->name : probe_text);
        name->add_css_class("am-tile-name");
        // natural width must never widen the tile (see the launcher's
        // layer-window width gotcha) — the fixed tile width wins
        name->set_max_width_chars(1);
        name->set_hexpand(true);
        name->set_xalign(0.5f);
        name->set_justify(Gtk::Justification::CENTER);
        name->set_valign(Gtk::Align::START);
        if (app) {
            name->set_ellipsize(Pango::EllipsizeMode::END);
            if (lines > 1) {
                name->set_wrap(true);
                name->set_wrap_mode(Pango::WrapMode::WORD_CHAR);
                name->set_lines(lines);
            }
        }
        tile->append(*name);
        return tile;
    };

    // measured in-tree so the CSS padding/font apply, then discarded
    auto* probe = build_tile(nullptr, lines > 1 ? "X\nX" : "X");
    grid_.attach(*probe, 0, 0);
    int min_h = 0, nat_h = 0, min_b = 0, nat_b = 0;
    probe->measure(Gtk::Orientation::VERTICAL, tile_w, min_h, nat_h, min_b, nat_b);
    grid_.remove(*probe);
    tile_height_ = std::max(nat_h, icon_px + 30);

    for (std::size_t i = 0; i < results_.size(); ++i) {
        const auto& app = results_[i];

        auto* tile = build_tile(&app, nullptr);
        tile->set_size_request(tile_w, tile_height_);
        // the label may end up ellipsized — the tooltip carries the full name
        tile->set_tooltip_text(app.description.empty() ? app.name
                                                       : app.name + "\n" + app.description);

        const int index = static_cast<int>(i);
        auto click = Gtk::GestureClick::create();
        click->signal_released().connect(
            [this, index](int, double, double) { activate_index(index); });
        tile->add_controller(click);

        auto hover = Gtk::EventControllerMotion::create();
        hover->signal_enter().connect([this, index](double, double) {
            if (mouse_active_)
                select(index, /*scroll_into_view=*/false);
        });
        tile->add_controller(hover);

        grid_.attach(*tile, index % cols, index / cols);
        tiles_.push_back(tile);
    }

    const bool empty = results_.empty();
    content_stack_.set_visible_child(empty ? static_cast<Gtk::Widget&>(empty_label_)
                                           : static_cast<Gtk::Widget&>(scroller_));
    if (!empty)
        select(0, /*scroll_into_view=*/true);
}

void AppMenuPanel::select(int index, bool scroll_into_view) {
    if (tiles_.empty())
        return;
    index = std::clamp(index, 0, static_cast<int>(tiles_.size()) - 1);

    if (selected_ >= 0 && selected_ < static_cast<int>(tiles_.size()))
        tiles_[static_cast<std::size_t>(selected_)]->remove_css_class("selected");
    selected_ = index;
    auto* tile = tiles_[static_cast<std::size_t>(index)];
    tile->add_css_class("selected");

    if (!scroll_into_view)
        return;
    graphene_rect_t bounds;
    if (!gtk_widget_compute_bounds(tile->gobj(), GTK_WIDGET(grid_.gobj()), &bounds))
        return;
    auto adjustment = scroller_.get_vadjustment();
    const double top = bounds.origin.y;
    const double bottom = top + bounds.size.height;
    if (top < adjustment->get_value())
        adjustment->set_value(top);
    else if (bottom > adjustment->get_value() + adjustment->get_page_size())
        adjustment->set_value(bottom - adjustment->get_page_size());
}

void AppMenuPanel::activate_index(int index) {
    if (index < 0 || index >= static_cast<int>(results_.size()))
        return;
    const auto entry = results_[static_cast<std::size_t>(index)];
    run_after_close([entry] { Apps::get().launch(entry); });
}

// Close first so the launched app / settings window can take focus
// (Noctalia's closeImmediately + deferred execution).
void AppMenuPanel::run_after_close(std::function<void()> action) {
    request_close_.emit();
    Glib::signal_idle().connect_once(std::move(action));
}

void AppMenuPanel::focus_default() {
    if (show_search_)
        search_.grab_focus();
    else
        grab_focus();
}

bool AppMenuPanel::on_key_pressed(guint keyval, guint, Gdk::ModifierType) {
    if (session_popover_.get_visible())
        return false; // GTK navigates the dropdown itself (Esc closes just it)

    const int count = static_cast<int>(tiles_.size());
    const int cols = std::max(1, columns_);
    const int page = cols * 3;
    auto wrap = [count](int index) { return ((index % count) + count) % count; };

    switch (keyval) {
    case GDK_KEY_Escape:
        request_close_.emit();
        return true;
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:
        activate_index(selected_);
        return true;
    case GDK_KEY_Right:
    case GDK_KEY_Tab:
        if (count > 0)
            select(wrap(selected_ + 1), true);
        return true;
    case GDK_KEY_Left:
    case GDK_KEY_ISO_Left_Tab:
        if (count > 0)
            select(wrap(selected_ - 1), true);
        return true;
    case GDK_KEY_Down:
        if (count > 0) {
            if (selected_ + cols < count)
                select(selected_ + cols, true);
            else if (selected_ / cols < (count - 1) / cols)
                select(count - 1, true); // partial last row: land on its end
            else
                select(selected_ % cols, true); // wrap to the first row
        }
        return true;
    case GDK_KEY_Up:
        if (count > 0) {
            if (selected_ - cols >= 0)
                select(selected_ - cols, true);
            else
                select(std::min(((count - 1) / cols) * cols + selected_ % cols, count - 1),
                       true); // wrap to the last row
        }
        return true;
    case GDK_KEY_Home:
        select(0, true);
        return true;
    case GDK_KEY_End:
        select(count - 1, true);
        return true;
    case GDK_KEY_Page_Down:
        select(std::min(selected_ + page, count - 1), true);
        return true;
    case GDK_KEY_Page_Up:
        select(std::max(selected_ - page, 0), true);
        return true;
    default:
        return false; // let the search entry have it
    }
}

} // namespace hyprshell
