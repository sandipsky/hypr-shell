#include "bar/desktop_menu.hpp"

#include "bar/frame_probe.hpp"
#include "bar/icon_cache.hpp"
#include "services/apps.hpp"
#include "services/config.hpp"
#include "services/session.hpp"
#include "services/settings_pages.hpp"
#include "services/wallpaper.hpp"

#include <algorithm>
#include <cctype>

namespace hyprshell {

namespace {

constexpr const char* kIconChevron = "";  // tabler chevron-right
constexpr const char* kIconSettings = ""; // tabler settings ("All Settings")
constexpr int kAppIconSize = 20;
constexpr int kAppRowHeight = 34;       // CSS: 22px min-height + 2×6px padding (+ 2px gap)
constexpr unsigned kHoverOpenMs = 160;  // hover dwell before a submenu opens
constexpr unsigned kHoverCloseMs = 260; // dwell on another row before the open one closes
// A submenu's first row lines up with the row that opened it: 4px contents
// padding + 1px border on the submenu, minus a slight overlap onto the parent.
constexpr int kSubmenuOffsetX = -3;
constexpr int kSubmenuOffsetY = -5;

std::string lowercase(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

// The Login screen settings page exists for the Elegant SDDM theme only; the
// settings app hides its sidebar row without SDDM + that theme, so the menu
// applies the same two cheap tests (the "selected in sddm.conf" part is not
// re-implemented here).
bool login_page_available() {
    return !Glib::find_program_in_path("sddm").empty() &&
           Glib::file_test("/usr/share/sddm/themes/Elegant", Glib::FileTest::IS_DIR);
}

} // namespace

DesktopMenu::DesktopMenu(Gtk::Widget& anchor) : anchor_(anchor) {
    popover_.set_parent(anchor_);
    popover_.set_has_arrow(false);
    popover_.set_position(Gtk::PositionType::BOTTOM);
    // GTK anchors a popover by its halign: START hangs its left edge off the
    // pointing-to rect's left edge, so the menu's top-left corner sits at the
    // click, like every desktop's context menu (the compositor flips it above
    // / slides it left at the screen edges)
    popover_.set_halign(Gtk::Align::START);
    popover_.set_valign(Gtk::Align::FILL);
    popover_.add_css_class("dm-popover");
    popover_.add_css_class("dm-root");
    content_.set_child(list_);
    popover_.set_child(content_);
    popover_.signal_closed().connect([this] {
        hover_timer_.disconnect();
        pending_row_ = nullptr;
        hide_submenus();
    });

    apps_scroller_.set_child(apps_list_);
    apps_scroller_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    apps_scroller_.set_propagate_natural_height(true);
    apps_scroller_.set_propagate_natural_width(true);
    apps_scroller_.set_overlay_scrolling(false);
    apps_scroller_.add_css_class("dm-apps");
    setup_submenu(apps_popover_, apps_scroller_);
    setup_submenu(settings_popover_, settings_list_);
    setup_submenu(session_popover_, session_list_);

    // Right / Left step into and out of submenus (GTK's own bindings give the
    // rows Up / Down / Enter / Escape)
    auto key = Gtk::EventControllerKey::create();
    key->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    key->signal_key_pressed().connect(
        [this](guint keyval, guint, Gdk::ModifierType) {
            if (keyval != GDK_KEY_Right && keyval != GDK_KEY_space)
                return false;
            for (auto& row : rows_) {
                if (row.submenu && row.button->has_focus()) {
                    show_submenu(row);
                    if (auto* first = row.submenu == &apps_popover_ ? apps_list_.get_first_child()
                                      : row.submenu == &settings_popover_
                                          ? settings_list_.get_first_child()
                                          : session_list_.get_first_child())
                        first->grab_focus();
                    return true;
                }
            }
            return false;
        },
        false);
    popover_.add_controller(key);
    for (auto* submenu : {&apps_popover_, &settings_popover_, &session_popover_}) {
        auto back = Gtk::EventControllerKey::create();
        back->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
        back->signal_key_pressed().connect(
            [this, submenu](guint keyval, guint, Gdk::ModifierType) {
                if (keyval != GDK_KEY_Left)
                    return false;
                submenu->popdown();
                return true;
            },
            false);
        submenu->add_controller(back);
        submenu->signal_closed().connect([this, submenu] {
            if (open_row_ && open_row_->submenu == submenu) {
                open_row_->button->remove_css_class("open");
                open_row_ = nullptr;
            }
        });
        // the pointer arriving on the submenu cancels a pending close
        auto motion = Gtk::EventControllerMotion::create();
        motion->signal_enter().connect([this](double, double) {
            if (pending_row_ && pending_row_ != open_row_ && !pending_row_->submenu)
                hover_timer_.disconnect();
            else if (pending_row_ && pending_row_ != open_row_)
                hover_timer_.disconnect();
            pending_row_ = nullptr;
        });
        submenu->add_controller(motion);
    }
}

DesktopMenu::~DesktopMenu() {
    hover_timer_.disconnect();
    icons_connection_.disconnect();
    for (auto* popover : {&apps_popover_, &settings_popover_, &session_popover_, &popover_}) {
        if (popover->get_visible())
            popover->popdown();
        if (popover->get_parent())
            popover->unparent();
    }
}

void DesktopMenu::setup_submenu(Gtk::Popover& popover, Gtk::Widget& child) {
    popover.set_parent(content_);
    popover.set_has_arrow(false);
    popover.set_position(Gtk::PositionType::RIGHT);
    popover.set_valign(Gtk::Align::START);
    popover.set_halign(Gtk::Align::FILL);
    popover.set_offset(kSubmenuOffsetX, kSubmenuOffsetY);
    popover.add_css_class("dm-popover");
    popover.set_child(child);
}

void DesktopMenu::open(double x, double y) {
    if (popover_.get_visible())
        close();
    rebuild();
    if (rows_.empty())
        return;
    popover_.set_pointing_to(Gdk::Rectangle(static_cast<int>(x), static_cast<int>(y), 1, 1));
    log_first_frame(popover_, "desktop menu"); // HS_FRAME_DEBUG
    popover_.popup();
}

void DesktopMenu::close() {
    hide_submenus();
    if (popover_.get_visible())
        popover_.popdown();
}

void DesktopMenu::hide_submenus() {
    for (auto* popover : {&apps_popover_, &settings_popover_, &session_popover_})
        if (popover->get_visible())
            popover->popdown();
    if (open_row_)
        open_row_->button->remove_css_class("open");
    open_row_ = nullptr;
}

Gtk::Widget* DesktopMenu::make_glyph(const char* glyph) {
    auto* label = Gtk::make_managed<Gtk::Label>(glyph);
    label->add_css_class("dm-glyph");
    return label;
}

Gtk::Widget* DesktopMenu::make_page_icon(const char* icon) {
    if (g_str_has_prefix(icon, "glyph:"))
        return make_glyph(icon + strlen("glyph:"));
    auto* image = Gtk::make_managed<Gtk::Image>();
    image->set_from_icon_name(icon);
    image->set_pixel_size(16);
    image->add_css_class("dm-icon");
    return image;
}

Gtk::Button* DesktopMenu::make_row(const std::string& label, Gtk::Widget* icon, bool has_submenu,
                                   bool destructive) {
    auto* button = Gtk::make_managed<Gtk::Button>();
    button->add_css_class("dm-item");
    if (destructive)
        button->add_css_class("destructive");
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    if (icon)
        box->append(*icon);
    auto* text = Gtk::make_managed<Gtk::Label>(label);
    text->add_css_class("dm-label");
    text->set_halign(Gtk::Align::START);
    text->set_hexpand(true);
    text->set_ellipsize(Pango::EllipsizeMode::END);
    text->set_max_width_chars(36);
    box->append(*text);
    if (has_submenu) {
        auto* chevron = Gtk::make_managed<Gtk::Label>(kIconChevron);
        chevron->add_css_class("dm-chevron");
        box->append(*chevron);
    }
    button->set_child(*box);
    return button;
}

void DesktopMenu::rebuild() {
    while (auto* child = list_.get_first_child())
        list_.remove(*child);
    rows_.clear();
    open_row_ = nullptr;
    pending_row_ = nullptr;
    hover_timer_.disconnect();

    const auto& cfg = Config::get().desktop_menu();
    const bool icons = cfg.show_icons;
    for (const auto* item : desktop_menu_items_in_order(cfg.order)) {
        if (!cfg.item_enabled(item->key, item->default_on))
            continue;
        const std::string key = item->key;
        // "Next desktop background" only makes sense while the slideshow runs
        if (key == "next_wallpaper" && !Config::get().wallpaper().slideshow)
            continue;
        Row row;
        row.key = key;
        if (key == "apps")
            row.submenu = &apps_popover_;
        else if (key == "settings")
            row.submenu = &settings_popover_;
        else if (key == "session")
            row.submenu = &session_popover_;
        row.button = make_row(item->label, icons ? make_glyph(item->glyph) : nullptr,
                              row.submenu != nullptr, false);
        rows_.push_back(row);
    }
    for (auto& row : rows_) {
        list_.append(*row.button);
        Row* self = &row;
        auto hover = Gtk::EventControllerMotion::create();
        hover->signal_enter().connect([this, self](double, double) { on_row_hover(self); });
        row.button->add_controller(hover);
        if (row.submenu) {
            row.button->signal_clicked().connect([this, self] {
                hover_timer_.disconnect();
                if (open_row_ == self)
                    hide_submenus();
                else
                    show_submenu(*self);
            });
        } else if (row.key == "next_wallpaper") {
            row.button->signal_clicked().connect(
                [this] { run_after_close([] { Wallpaper::get().next(); }); });
        } else if (row.key == "keybindings") {
            row.button->signal_clicked().connect([this] {
                run_after_close([] {
                    if (auto app = Gio::Application::get_default())
                        app->activate_action("keybindings");
                });
            });
        }
    }
    // submenu contents are built now (cheap) so they exist before any hover
    build_apps_submenu();
    build_settings_submenu();
    build_session_submenu();
}

// Hover dwell: a submenu row opens its menu after a short pause; any other row
// closes the open submenu after a slightly longer one, so a diagonal move
// towards the open submenu does not shut it (the classic menu problem).
void DesktopMenu::on_row_hover(Row* row) {
    hover_timer_.disconnect();
    if (row == open_row_) {
        pending_row_ = nullptr;
        return;
    }
    pending_row_ = row;
    hover_timer_ = Glib::signal_timeout().connect(
        [this, row] {
            pending_row_ = nullptr;
            if (row->submenu)
                show_submenu(*row);
            else
                hide_submenus();
            return false;
        },
        row->submenu ? kHoverOpenMs : kHoverCloseMs);
}

void DesktopMenu::show_submenu(Row& row) {
    if (!row.submenu || !popover_.get_visible())
        return;
    if (open_row_ == &row)
        return;
    hide_submenus();
    const auto bounds = row.button->compute_bounds(content_);
    if (!bounds)
        return;
    row.submenu->set_pointing_to(Gdk::Rectangle(
        static_cast<int>(bounds->get_x()), static_cast<int>(bounds->get_y()),
        std::max(1, static_cast<int>(bounds->get_width())),
        std::max(1, static_cast<int>(bounds->get_height()))));
    if (row.submenu == &apps_popover_) {
        // never taller than 60% of the screen (the anchor spans the monitor)
        const int screen = anchor_.get_height();
        const int cap = screen > 0 ? std::max(kAppRowHeight * 4, screen * 6 / 10) : 600;
        int minimum = 0, natural = 0, mb = 0, nb = 0;
        apps_list_.measure(Gtk::Orientation::VERTICAL, -1, minimum, natural, mb, nb);
        apps_scroller_.set_max_content_height(std::min(natural + 2, cap));
        apps_scroller_.set_min_content_height(std::min(natural + 2, cap));
    }
    open_row_ = &row;
    row.button->add_css_class("open");
    row.submenu->popup();
}

void DesktopMenu::open_submenu(const std::string& key) {
    for (auto& row : rows_)
        if (row.key == key)
            show_submenu(row);
}

// Close first so the launched app / settings window / overlay can take focus
// (the launcher's closeImmediately + deferred execution).
void DesktopMenu::run_after_close(std::function<void()> action) {
    close();
    Glib::signal_idle().connect_once(std::move(action));
}

void DesktopMenu::build_apps_submenu() {
    while (auto* child = apps_list_.get_first_child())
        apps_list_.remove(*child);
    pending_icons_.clear();
    const bool icons = Config::get().desktop_menu().show_icons;

    std::vector<const Apps::Entry*> entries;
    for (const auto& app : Apps::get().entries())
        entries.push_back(&app);
    std::sort(entries.begin(), entries.end(), [](const Apps::Entry* a, const Apps::Entry* b) {
        return lowercase(a->name) < lowercase(b->name);
    });
    std::vector<Glib::RefPtr<Gio::Icon>> missing;
    for (const auto* app : entries) {
        Gtk::Widget* icon = nullptr;
        if (icons) {
            auto* image = Gtk::make_managed<Gtk::Image>();
            image->set_pixel_size(kAppIconSize);
            image->add_css_class("dm-app-icon");
            if (!app->icon) {
                image->set_from_icon_name("application-x-executable");
            } else if (auto paintable = IconCache::get().find(app->icon, kAppIconSize)) {
                image->set(paintable);
            } else {
                image->set(app->icon);
                missing.push_back(app->icon);
                pending_icons_.emplace_back(image, app->icon);
            }
            icon = image;
        }
        auto* button = make_row(app->name, icon, false, false);
        button->add_css_class("dm-app");
        const Apps::Entry entry = *app;
        button->signal_clicked().connect(
            [this, entry] { run_after_close([entry] { Apps::get().launch(entry); }); });
        apps_list_.append(*button);
    }
    if (entries.empty()) {
        auto* empty = Gtk::make_managed<Gtk::Label>("No applications found");
        empty->add_css_class("dm-empty");
        apps_list_.append(*empty);
    }
    if (!missing.empty()) {
        IconCache::get().request(anchor_, missing, kAppIconSize);
        if (!icons_connection_.connected())
            icons_connection_ = IconCache::get().signal_rendered().connect(
                sigc::mem_fun(*this, &DesktopMenu::refresh_app_icons));
    }
}

void DesktopMenu::refresh_app_icons() {
    auto& cache = IconCache::get();
    std::erase_if(pending_icons_, [&](auto& pair) {
        auto paintable = cache.find(pair.second, kAppIconSize);
        if (!paintable)
            return false;
        pair.first->set(paintable);
        return true;
    });
    if (pending_icons_.empty())
        icons_connection_.disconnect();
}

void DesktopMenu::build_settings_submenu() {
    while (auto* child = settings_list_.get_first_child())
        settings_list_.remove(*child);
    const bool icons = Config::get().desktop_menu().show_icons;

    auto* all = make_row("All Settings", icons ? make_glyph(kIconSettings) : nullptr, false, false);
    all->signal_clicked().connect([this] { run_after_close([] { open_settings(); }); });
    settings_list_.append(*all);
    auto* separator = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);
    separator->add_css_class("dm-separator");
    settings_list_.append(*separator);

    for (const auto& page : kSettingsPages) {
        const std::string name = page.name;
        if (name == "about_page" || (name == "login_page" && !login_page_available()))
            continue;
        auto* button = make_row(page.title, icons ? make_page_icon(page.icon) : nullptr, false, false);
        button->signal_clicked().connect(
            [this, name] { run_after_close([name] { open_settings(name); }); });
        settings_list_.append(*button);
    }
}

void DesktopMenu::build_session_submenu() {
    while (auto* child = session_list_.get_first_child())
        session_list_.remove(*child);
    const bool icons = Config::get().desktop_menu().show_icons;
    const auto actions = enabled_session_actions();
    if (actions.empty()) {
        auto* empty = Gtk::make_managed<Gtk::Label>("No actions enabled");
        empty->add_css_class("dm-empty");
        session_list_.append(*empty);
        return;
    }
    for (const auto* action : actions) {
        auto* button = make_row(action->label, icons ? make_glyph(action->glyph) : nullptr, false,
                                action->destructive);
        button->signal_clicked().connect(
            [this, action] { run_after_close([action] { run_session_action(*action); }); });
        session_list_.append(*button);
    }
}

} // namespace hyprshell
