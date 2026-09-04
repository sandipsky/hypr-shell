#include "bar/modules/vpn.hpp"

#include "bar/bar_popover.hpp"

#include "services/config.hpp"
#include "services/vpn.hpp"

namespace hyprshell {

namespace {

// noctalia-tabler-icons glyphs (\u escapes — never literal PUA)
constexpr const char* kShield = "";
constexpr const char* kShieldLock = "";
constexpr const char* kShieldOff = "";
constexpr const char* kSettings = "";

constexpr unsigned kPillDelayMs = 500;      // Noctalia's Style.pillDelay
constexpr unsigned kPillAnimationMs = 300;  // Style.animationNormal

std::string color_class(const std::string& key) {
    return key == "none" || key.empty() ? std::string() : "color-" + key;
}

} // namespace

Vpn::Vpn() : Gtk::Box(Gtk::Orientation::HORIZONTAL, 0) {
    add_css_class("module");
    add_css_class("vpn");
    icon_.add_css_class("icon");
    text_.add_css_class("pill-text");
    text_.set_single_line_mode(true);
    revealer_.set_child(text_);
    revealer_.set_transition_duration(kPillAnimationMs);
    revealer_.set_reveal_child(false);
    // icon left, text right (left/center sections); apply_config flips it
    append(icon_);
    append(revealer_);

    // click opens the VPN panel; anchored to the icon (see the battery
    // module's popover-anchor gotcha)
    panel_ = Gtk::make_managed<VpnPanel>();
    popover_.set_child(*panel_);
    popover_.set_parent(icon_);
    popover_.set_has_arrow(false);
    popover_.add_css_class("vpn-popover");
    popover_.signal_closed().connect([this] { panel_->set_open(false); });

    set_cursor(Gdk::Cursor::create("pointer"));

    auto click = Gtk::GestureClick::create();
    click->signal_released().connect([this](int, double, double) { open_panel(); });
    add_controller(click);

    // hover reveals the pill text after Noctalia's 500ms delay (on-hover mode)
    auto motion = Gtk::EventControllerMotion::create();
    motion->signal_enter().connect([this](double, double) {
        hovered_ = true;
        if (Config::get().vpn().display_mode == Config::Vpn::DisplayMode::OnHover &&
            !Config::get().bar_vertical()) {
            show_timer_.disconnect();
            show_timer_ = Glib::signal_timeout().connect(
                [this] {
                    if (hovered_)
                        set_revealed(true);
                    return false;
                },
                kPillDelayMs);
        }
    });
    motion->signal_leave().connect([this] {
        hovered_ = false;
        show_timer_.disconnect();
        if (Config::get().vpn().display_mode == Config::Vpn::DisplayMode::OnHover)
            set_revealed(false);
    });
    add_controller(motion);

    // dev hook: HS_OPEN_VPN=1 pops the panel shortly after startup; =3 also
    // opens the import file dialog 1.5s later (the dialog must not be parented
    // to the layer-shell bar — see VpnPanel::pick_import_file)
    if (const char* hook = g_getenv("HS_OPEN_VPN")) {
        Glib::signal_timeout().connect_once([this] { open_panel(); }, 800);
        if (g_strcmp0(hook, "3") == 0)
            Glib::signal_timeout().connect_once([this] { panel_->open_import_dialog(); }, 2300);
    }

    VpnService::get().signal_changed().connect(sigc::mem_fun(*this, &Vpn::update));
    Config::get().signal_changed().connect(sigc::mem_fun(*this, &Vpn::apply_config));
    apply_config();
    update();
}

Vpn::~Vpn() {
    popover_.unparent();
}

void Vpn::open_panel() {
    place_bar_popover(popover_);
    panel_->set_open(true);
    popover_.popup();
}

void Vpn::set_revealed(bool revealed) {
    // never reveal an empty pill; slide sideways like the pill width animation
    revealer_.set_reveal_child(revealed && !text_.get_text().empty());
}

void Vpn::apply_config() {
    const auto& cfg = Config::get().vpn();
    // icon on the section's outer side: right section → text left of the icon
    const auto& right = Config::get().bar_layout(Config::BarSection::Right);
    const bool icon_last = std::find(right.begin(), right.end(), "vpn") != right.end();
    if (icon_last) {
        reorder_child_after(icon_, revealer_);
        revealer_.set_transition_type(Gtk::RevealerTransitionType::SLIDE_LEFT);
    } else {
        reorder_child_after(revealer_, icon_);
        revealer_.set_transition_type(Gtk::RevealerTransitionType::SLIDE_RIGHT);
    }
    // Noctalia's icon / text colour keys → CSS classes
    const auto swap = [](Gtk::Widget& w, std::string& current, const std::string& next) {
        if (current == next)
            return;
        if (!current.empty())
            w.remove_css_class(current);
        if (!next.empty())
            w.add_css_class(next);
        current = next;
    };
    swap(icon_, icon_color_class_, color_class(cfg.icon_color));
    swap(text_, text_color_class_, color_class(cfg.text_color));
    update();
}

void Vpn::update() {
    auto& vpn = VpnService::get();
    const auto& cfg = Config::get().vpn();
    const auto active = vpn.active_connections();
    icon_.set_text(vpn.has_active_connection() ? kShieldLock : kShield);

    std::string text;
    if (!active.empty())
        text = active.front().name;
    else if (!vpn.connecting_uuid().empty()) {
        if (auto it = vpn.connections().find(vpn.connecting_uuid()); it != vpn.connections().end())
            text = it->second.name;
    }
    if (active.size() > 1)
        text += " + " + std::to_string(active.size() - 1);
    text_.set_text(text);

    // BarPill: forceOpen = horizontal + alwaysShow, forceClose = vertical |
    // alwaysHide | empty text; otherwise hover decides
    const bool vertical = Config::get().bar_vertical();
    if (vertical || cfg.display_mode == Config::Vpn::DisplayMode::AlwaysHide || text.empty())
        set_revealed(false);
    else if (cfg.display_mode == Config::Vpn::DisplayMode::AlwaysShow)
        set_revealed(true);
    else
        set_revealed(hovered_ && revealer_.get_reveal_child());
    set_tooltip_text(text.empty() ? "Manage VPN connections" : text);
}

} // namespace hyprshell
