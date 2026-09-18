#include "bar/modules/app_menu.hpp"

#include "bar/bar_popover.hpp"

#include "services/app_menu_icons.hpp"
#include "services/config.hpp"
#include "services/session.hpp"

#include <giomm.h>

#include <string>

#include <algorithm>
#include <cstdlib>

namespace hyprshell {

namespace {

constexpr int kImageSize = 18; // matches the .icon glyph font size

// /etc/os-release LOGO= (e.g. "archlinux-logo"), read once. Noctalia's
// HostService probes icon paths by hand; here the icon theme resolves it.
std::string distro_logo_name() {
    static const std::string name = [] {
        std::string data;
        try {
            data = Glib::file_get_contents("/etc/os-release");
        } catch (const Glib::Error&) {
            return std::string();
        }
        std::size_t pos = 0;
        while (pos < data.size()) {
            auto end = data.find('\n', pos);
            if (end == std::string::npos)
                end = data.size();
            std::string line = data.substr(pos, end - pos);
            pos = end + 1;
            if (line.rfind("LOGO=", 0) != 0)
                continue;
            std::string value = line.substr(5);
            if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') &&
                value.back() == value.front())
                value = value.substr(1, value.size() - 2);
            return value;
        }
        return std::string();
    }();
    return name;
}

const char* preset_glyph(const std::string& key) {
    for (const auto& preset : kAppMenuIconPresets)
        if (key == preset.key)
            return preset.glyph;
    return nullptr;
}

} // namespace

AppMenu::AppMenu() : Gtk::Box(Gtk::Orientation::HORIZONTAL, 0) {
    add_css_class("module");
    add_css_class("app-menu");

    glyph_.add_css_class("icon");
    image_.set_pixel_size(kImageSize);
    image_.set_valign(Gtk::Align::CENTER);
    label_.add_css_class("app-menu-text");
    label_.set_valign(Gtk::Align::CENTER);
    content_.append(glyph_);
    content_.append(image_);
    content_.append(label_);
    // popover anchored to an Overlay, not the module Box (see the battery
    // module: a Box parent allocates an open popover inline)
    anchor_.set_child(content_);
    append(anchor_);
    set_tooltip_text("Applications");

    panel_ = Gtk::make_managed<AppMenuPanel>();
    popover_.set_child(*panel_);
    popover_.set_parent(anchor_);
    popover_.set_has_arrow(false);
    popover_.add_css_class("app-menu-popover");

    auto click = Gtk::GestureClick::create();
    click->signal_released().connect([this](int, double, double) { open(); });
    add_controller(click);
    popover_.signal_closed().connect([this] { panel_->set_open(false); });
    panel_->signal_request_close().connect([this] { popover_.popdown(); });

    // right click: the shared session menu, like the panel's power button
    session_list_ = Gtk::make_managed<SessionMenuList>();
    session_popover_.set_child(*session_list_);
    session_popover_.set_parent(anchor_);
    session_popover_.set_has_arrow(false);
    session_popover_.add_css_class("session-popover");
    session_list_->signal_activate().connect([this](const SessionAction& action) {
        // close first so the action's own UI (lock screen etc.) can take over
        session_popover_.popdown();
        const SessionAction* act = &action; // static table, see session_actions.hpp
        Glib::signal_idle().connect_once([act] { run_session_action(*act); });
    });
    auto right_click = Gtk::GestureClick::create();
    right_click->set_button(GDK_BUTTON_SECONDARY);
    right_click->signal_released().connect([this](int, double, double) { open_session_menu(); });
    add_controller(right_click);

    // dev hook: HS_OPEN_APP_MENU=1 pops the panel shortly after startup;
    // =2 also opens its session dropdown, =3 the pin menu on the first tile,
    // =4 the right-click session menu instead of the panel
    if (const char* hook = g_getenv("HS_OPEN_APP_MENU")) {
        const bool session = g_strcmp0(hook, "2") == 0;
        const bool pin = g_strcmp0(hook, "3") == 0;
        const bool right_click = g_strcmp0(hook, "4") == 0;
        const int delay = std::max(800, std::atoi(hook)); // >4 = delay in ms
        Glib::signal_timeout().connect_once(
            [this, session, pin, right_click] {
                if (right_click) {
                    open_session_menu();
                    return;
                }
                open();
                if (session)
                    Glib::signal_timeout().connect_once(
                        [this] { panel_->show_session_menu(); }, 400);
                if (pin)
                    Glib::signal_timeout().connect_once(
                        [this] { panel_->open_pin_menu(0); }, 400);
            },
            delay);
    }

    Config::get().signal_changed().connect(sigc::mem_fun(*this, &AppMenu::apply_config));
    apply_config();
    // build the grid and rasterize the icons once startup has settled, so the
    // first open paints as fast as the later ones
    Glib::signal_idle().connect_once([this] { panel_->prewarm(); }, Glib::PRIORITY_LOW);
}

AppMenu::~AppMenu() {
    popover_.unparent();
    session_popover_.unparent();
}

void AppMenu::toggle() {
    if (popover_.get_visible()) {
        popover_.popdown();
        return;
    }
    if (!get_mapped()) {
        g_message("app menu: not shown — module disabled or bar hidden");
        return;
    }
    open();
}

void AppMenu::open() {
    session_popover_.popdown();
    // keep the panel on the free side of the bar
    place_bar_popover(popover_);
    panel_->set_open(true);
    popover_.popup();
}

void AppMenu::open_session_menu() {
    if (!Config::get().app_menu().right_click_session)
        return;
    popover_.popdown();
    if (Config::get().session().mode == Config::Session::Mode::Fullscreen) {
        if (auto app = Gio::Application::get_default())
            app->activate_action("session"); // the App owns the fullscreen window
        return;
    }
    place_bar_popover(session_popover_);
    session_popover_.popup();
}

void AppMenu::apply_config() {
    const auto& cfg = Config::get().app_menu();
    using Display = Config::AppMenu::Display;
    // a vertical bar has no room for a label: icon only, whatever `display`
    // says (like Noctalia's bar pills on the side bars)
    const bool vertical = Config::get().bar_vertical();
    const bool want_icon = cfg.display != Display::Text || vertical;
    const bool want_text = cfg.display != Display::Icon && !vertical;

    bool use_image = false;
    const char* glyph = kAppMenuIconPresets[0].glyph; // rocket fallback
    if (cfg.icon == kAppMenuIconDistro) {
        const std::string logo = distro_logo_name();
        if (!logo.empty()) {
            image_.set_from_icon_name(logo);
            use_image = true;
        }
    } else if (cfg.icon == kAppMenuIconCustom) {
        if (!cfg.custom_icon.empty()) {
            std::string spec = cfg.custom_icon;
            if (spec.rfind("~/", 0) == 0)
                spec = Glib::get_home_dir() + spec.substr(1);
            try {
                // g_icon_new_for_string: an absolute path is a file icon,
                // anything else a themed icon name
                image_.set(Gio::Icon::create(spec));
                use_image = true;
            } catch (const Glib::Error& e) {
                g_warning("app menu: bad custom icon '%s': %s", spec.c_str(), e.what());
            }
        }
    } else if (const char* preset = preset_glyph(cfg.icon)) {
        glyph = preset;
    }

    glyph_.set_text(glyph);
    glyph_.set_visible(want_icon && !use_image);
    image_.set_visible(want_icon && use_image);
    label_.set_text(cfg.text);
    label_.set_visible(want_text && !cfg.text.empty());
    // text-only with an empty label would leave nothing to click on
    if (!glyph_.get_visible() && !image_.get_visible() && !label_.get_visible())
        glyph_.set_visible(true);
    content_.set_spacing(glyph_.get_visible() || image_.get_visible() ? 6 : 0);
}

} // namespace hyprshell
