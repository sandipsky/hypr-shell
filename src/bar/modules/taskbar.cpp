#include "bar/modules/taskbar.hpp"

#include "services/apps.hpp"
#include "services/config.hpp"
#include "services/hyprland.hpp"

#include <giomm/desktopappinfo.h>
#include <gtk/gtk.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <map>

namespace hyprshell {

namespace {

// Noctalia's default bar density: barHeight 31 / capsuleHeight 25
constexpr int kCapsuleHeight = 25;
constexpr int kMarginS = 6;   // Style.marginS — icon/title spacing
constexpr int kMarginM = 9;   // Style.marginM — capsule/title side padding
constexpr int kIndicatorHeight = 4;
constexpr unsigned kRefreshCoalesceMs = 30;
constexpr unsigned kWheelCooldownMs = 150; // wheelDebounce
constexpr double kFadeMs = 300.0;          // Style.animationNormal
constexpr int kMinTitleWidth = 20;

// Style.toOdd
int to_odd(double n) {
    return static_cast<int>(std::floor(n / 2.0)) * 2 + 1;
}

// ThemeIcons.iconForAppId: the entry's icon, else the app id as an icon name,
// else the generic executable icon.
void apply_icon(Gtk::Image& image, const Apps::Entry* entry, const std::string& app_id) {
    if (entry && entry->icon) {
        image.set(entry->icon);
        return;
    }
    auto theme = Gtk::IconTheme::get_for_display(image.get_display());
    if (!app_id.empty() && theme && theme->has_icon(app_id)) {
        image.set_from_icon_name(app_id);
        return;
    }
    image.set_from_icon_name("application-x-executable");
}

} // namespace

Taskbar::Taskbar() : Gtk::Box(Gtk::Orientation::HORIZONTAL, 0) {
    add_css_class("module");
    add_css_class("taskbar");
    capsule_.add_css_class("taskbar-capsule");
    capsule_.set_valign(Gtk::Align::CENTER);
    capsule_.set_halign(Gtk::Align::CENTER);
    items_box_.set_valign(Gtk::Align::CENTER);
    capsule_.append(items_box_);
    append(capsule_);

    auto scroll = Gtk::EventControllerScroll::create();
    scroll->set_flags(Gtk::EventControllerScroll::Flags::BOTH_AXES);
    scroll->signal_scroll().connect(sigc::mem_fun(*this, &Taskbar::on_scroll), false);
    add_controller(scroll);

    auto& hypr = Hyprland::get();
    hypr.signal_event().connect(sigc::mem_fun(*this, &Taskbar::on_event));
    Apps::get().signal_changed().connect([this] { update_model(); });
    Config::get().signal_changed().connect([this] { refresh(); });

    // the bar's monitor is only known once mapped — filter again then
    signal_map().connect([this] { schedule_refresh(); });
    refresh();
}

Taskbar::~Taskbar() {
    refresh_timer_.disconnect();
    wheel_timer_.disconnect();
}

// -- data ----------------------------------------------------------------------

void Taskbar::on_event(const std::string& name, const std::string& /*data*/) {
    static constexpr const char* interesting[] = {
        "openwindow",     "closewindow",        "movewindow",     "movewindowv2",
        "activewindow",   "activewindowv2",     "windowtitle",    "windowtitlev2",
        "workspace",      "workspacev2",        "focusedmon",     "focusedmonv2",
        "changefloatingmode", "fullscreen",     "monitoradded",   "monitorremoved",
        "activespecial",  "activespecialv2",
    };
    for (const auto* candidate : interesting)
        if (name == candidate) {
            schedule_refresh();
            return;
        }
}

// Events arrive in bursts (open + activewindow + title …) — one refresh each.
void Taskbar::schedule_refresh() {
    if (refresh_timer_.connected())
        return;
    refresh_timer_ = Glib::signal_timeout().connect(
        [this] {
            refresh();
            return false;
        },
        kRefreshCoalesceMs);
}

void Taskbar::refresh() {
    auto& hypr = Hyprland::get();
    if (!hypr.available()) {
        windows_.clear();
        update_model();
        return;
    }
    const auto serial = ++serial_;
    hypr.request("j/monitors", [this, serial, &hypr](const std::string& monitors_reply) {
        if (serial != serial_)
            return;
        auto monitors = std::make_shared<std::map<int, std::string>>();
        auto active = std::make_shared<std::vector<int>>();
        try {
            for (const auto& m : nlohmann::json::parse(monitors_reply)) {
                (*monitors)[m.value("id", -1)] = m.value("name", "");
                if (m.contains("activeWorkspace"))
                    active->push_back(m["activeWorkspace"].value("id", -1));
                if (m.contains("specialWorkspace")) {
                    const int special = m["specialWorkspace"].value("id", 0);
                    if (special != 0)
                        active->push_back(special);
                }
            }
        } catch (const std::exception& e) {
            g_warning("taskbar: monitors parse failed: %s", e.what());
        }
        hypr.request("j/clients", [this, serial, &hypr, monitors,
                                   active](const std::string& clients_reply) {
            if (serial != serial_)
                return;
            auto windows = std::make_shared<std::vector<Window>>();
            try {
                for (const auto& c : nlohmann::json::parse(clients_reply)) {
                    if (!c.value("mapped", true) || c.value("hidden", false))
                        continue;
                    Window w;
                    w.address = c.value("address", "");
                    if (w.address.empty())
                        continue;
                    w.app_id = c.value("class", "");
                    if (w.app_id.empty())
                        w.app_id = c.value("initialClass", "");
                    w.title = c.value("title", "");
                    if (c.contains("workspace") && c["workspace"].is_object())
                        w.workspace_id = c["workspace"].value("id", -1);
                    if (auto it = monitors->find(c.value("monitor", -1)); it != monitors->end())
                        w.output = it->second;
                    if (c.contains("at") && c["at"].is_array() && c["at"].size() >= 2) {
                        w.x = c["at"][0].get<int>();
                        w.y = c["at"][1].get<int>();
                    }
                    windows->push_back(std::move(w));
                }
            } catch (const std::exception& e) {
                g_warning("taskbar: clients parse failed: %s", e.what());
            }
            hypr.request("j/activewindow", [this, serial, windows,
                                            active](const std::string& active_reply) {
                if (serial != serial_)
                    return;
                std::string focused;
                try {
                    const auto json = nlohmann::json::parse(active_reply);
                    if (json.is_object())
                        focused = json.value("address", "");
                } catch (const std::exception&) {
                    // no active window
                }
                active_workspaces_ = *active;
                windows_ = std::move(*windows);
                active_address_ = focused;
                // a window only counts as focused on an active workspace
                for (auto& w : windows_)
                    w.focused = !focused.empty() && w.address == focused &&
                                std::find(active_workspaces_.begin(), active_workspaces_.end(),
                                          w.workspace_id) != active_workspaces_.end();
                // toSortedWindowList: workspace, then x, then y, then address
                std::sort(windows_.begin(), windows_.end(), [](const Window& a, const Window& b) {
                    if (a.workspace_id != b.workspace_id)
                        return a.workspace_id < b.workspace_id;
                    if (a.x != b.x)
                        return a.x < b.x;
                    if (a.y != b.y)
                        return a.y < b.y;
                    return a.address < b.address;
                });
                update_model();
            });
        });
    });
}

std::string Taskbar::bar_output() {
    auto* native = get_native();
    if (!native)
        return "";
    auto surface = native->get_surface();
    if (!surface)
        return "";
    auto monitor = get_display()->get_monitor_at_surface(surface);
    return monitor ? std::string(monitor->get_connector()) : "";
}

int Taskbar::screen_width() {
    auto* native = get_native();
    if (!native)
        return 0;
    auto surface = native->get_surface();
    if (!surface)
        return 0;
    auto monitor = get_display()->get_monitor_at_surface(surface);
    if (!monitor)
        return 0;
    Gdk::Rectangle geometry;
    monitor->get_geometry(geometry);
    return geometry.get_width();
}

std::string Taskbar::desktop_id_for(const std::string& app_id) const {
    if (const auto* entry = Apps::get().lookup_for_class(app_id))
        return entry->id;
    return app_id;
}

std::string Taskbar::app_name_for(const std::string& app_id) const {
    auto& apps = Apps::get();
    if (const auto* entry = apps.lookup_for_class(app_id))
        return entry->name;
    if (const auto* entry = apps.find_by_id(app_id))
        return entry->name;
    return app_id;
}

// isAppIdPinned: the class itself or its resolved desktop id is pinned
bool Taskbar::is_pinned_app(const std::string& app_id) const {
    if (app_id.empty())
        return false;
    auto& apps = Apps::get();
    if (apps.is_pinned(app_id))
        return true;
    const std::string resolved = desktop_id_for(app_id);
    return resolved != app_id && apps.is_pinned(resolved);
}

std::string Taskbar::item_key(const Item& item) const {
    // window identity for running apps (distinguishes instances), app id otherwise
    return item.window >= 0 ? windows_[static_cast<std::size_t>(item.window)].address
                            : item.app_id;
}

// updateCombinedModel
void Taskbar::update_model() {
    const auto& cfg = Config::get().taskbar();
    auto& apps = Apps::get();
    std::vector<Item> items;
    std::vector<std::string> processed; // normalized app ids already shown

    const std::string output = bar_output();
    for (std::size_t i = 0; i < windows_.size(); ++i) {
        const auto& w = windows_[i];
        const bool pass_output = !cfg.only_same_monitor || output.empty() || w.output == output;
        const bool pass_workspace =
            !cfg.only_active_workspaces ||
            std::find(active_workspaces_.begin(), active_workspaces_.end(), w.workspace_id) !=
                active_workspaces_.end();
        if (!pass_output || !pass_workspace)
            continue;
        Item item;
        item.id = w.address;
        item.type = is_pinned_app(w.app_id) ? ItemType::PinnedRunning : ItemType::Running;
        if (cfg.apps == Config::Taskbar::Apps::Pinned && item.type != ItemType::PinnedRunning)
            continue; // "only pinned": windows of unpinned apps stay out
        item.window = static_cast<int>(i);
        item.app_id = w.app_id;
        item.title = !w.title.empty() ? w.title : app_name_for(w.app_id);
        items.push_back(std::move(item));
        processed.push_back(normalize_app_id(w.app_id));
        const std::string resolved = desktop_id_for(w.app_id);
        if (resolved != w.app_id)
            processed.push_back(normalize_app_id(resolved));
    }

    if (cfg.apps != Config::Taskbar::Apps::Running) {
        for (const auto& pinned : apps.pinned()) {
            const std::string key = normalize_app_id(pinned);
            if (std::find(processed.begin(), processed.end(), key) != processed.end())
                continue;
            Item item;
            item.id = pinned;
            item.type = ItemType::Pinned;
            item.app_id = pinned;
            item.title = app_name_for(pinned);
            items.push_back(std::move(item));
        }
    }

    std::vector<Item> previous = std::move(items_);
    items_ = sort_items(std::move(items));
    if (session_order_.empty() || session_order_.size() != items_.size()) {
        session_order_.clear();
        for (const auto& item : items_)
            session_order_.push_back(item_key(item));
    }
    rebuild(previous);
}

// sortApps: pinned apps keep the pinned order (running or not), then the
// unpinned running apps in transient session order.
std::vector<Taskbar::Item> Taskbar::sort_items(std::vector<Item> items) const {
    std::vector<Item> pinned_items, running_items;
    for (auto& item : items) {
        if (item.type == ItemType::Pinned || item.type == ItemType::PinnedRunning)
            pinned_items.push_back(std::move(item));
        else
            running_items.push_back(std::move(item));
    }

    std::vector<Item> ordered;
    for (const auto& pinned : Apps::get().pinned()) {
        const std::string key = normalize_app_id(pinned);
        for (auto it = pinned_items.begin(); it != pinned_items.end();) {
            if (normalize_app_id(it->app_id) == key ||
                normalize_app_id(desktop_id_for(it->app_id)) == key) {
                ordered.push_back(std::move(*it));
                it = pinned_items.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (auto& item : pinned_items)
        ordered.push_back(std::move(item));

    for (const auto& key : session_order_) {
        auto it = std::find_if(running_items.begin(), running_items.end(),
                               [&](const Item& item) { return item_key(item) == key; });
        if (it != running_items.end()) {
            ordered.push_back(std::move(*it));
            running_items.erase(it);
        }
    }
    for (auto& item : running_items)
        ordered.push_back(std::move(item));
    return ordered;
}

// -- ui --------------------------------------------------------------------------

void Taskbar::rebuild(const std::vector<Item>& previous) {
    const auto& cfg = Config::get().taskbar();
    const bool vertical = Config::get().bar_vertical();
    const int item_size = to_odd(kCapsuleHeight * std::max(0.1, cfg.icon_scale));
    const bool show_title = cfg.show_title && !vertical;
    int title_width = cfg.title_width;
    if (show_title && cfg.smart_width && !items_.empty()) {
        // smartWidth: shrink titles so the whole widget fits max_width_percent
        const int max_width = screen_width() * cfg.max_width_percent / 100;
        if (max_width > 0) {
            const double per_entry = static_cast<double>(max_width) / items_.size() -
                                     item_size - kMarginS - 2 * kMarginM;
            title_width = static_cast<int>(std::min<double>(title_width, per_entry));
        }
        title_width = std::max(title_width, kMinTitleWidth);
    }
    if (!show_title)
        title_width = 0;

    const std::string layout_key = std::to_string(vertical) + ':' + std::to_string(cfg.item_gap) +
                                   ':' + std::to_string(item_size) + ':' + std::to_string(title_width);
    bool same = layout_key == layout_key_ && previous.size() == items_.size();
    for (std::size_t i = 0; same && i < items_.size(); ++i)
        same = previous[i].root != nullptr && previous[i].id == items_[i].id &&
               previous[i].type == items_[i].type && previous[i].app_id == items_[i].app_id;
    if (same) {
        // window titles and focus change constantly (windowtitle / activewindow
        // events) — keep the widgets, refresh their state
        for (std::size_t i = 0; i < items_.size(); ++i) {
            items_[i].root = previous[i].root;
            items_[i].title_label = previous[i].title_label;
            refresh_item(i);
        }
        apply_hide_mode();
        return;
    }
    layout_key_ = layout_key;

    while (auto* child = items_box_.get_first_child())
        items_box_.remove(*child);
    items_box_.set_orientation(vertical ? Gtk::Orientation::VERTICAL
                                        : Gtk::Orientation::HORIZONTAL);
    items_box_.set_spacing(cfg.item_gap);
    for (const char* name : {"vertical", "with-titles"})
        capsule_.remove_css_class(name);
    if (vertical)
        capsule_.add_css_class("vertical");
    if (show_title)
        capsule_.add_css_class("with-titles");

    for (std::size_t i = 0; i < items_.size(); ++i) {
        items_[i].root = build_item(i, item_size, title_width);
        items_box_.append(*items_[i].root);
    }
    apply_hide_mode();
}

void Taskbar::refresh_item(std::size_t index) {
    auto& item = items_[index];
    const bool focused =
        item.window >= 0 && windows_[static_cast<std::size_t>(item.window)].focused;
    if (focused)
        item.root->add_css_class("focused");
    else
        item.root->remove_css_class("focused");
    const bool running_dot = item.window >= 0 && !focused && Config::get().taskbar().running_indicator;
    if (running_dot)
        item.root->add_css_class("running");
    else
        item.root->remove_css_class("running");
    const std::string& tooltip = item.title.empty() ? item.app_id : item.title;
    if (item.root->get_tooltip_text().raw() != tooltip)
        item.root->set_tooltip_text(tooltip);
    if (item.title_label != nullptr && item.title_label->get_text().raw() != item.title)
        item.title_label->set_text(item.title);
}

Gtk::Widget* Taskbar::build_item(std::size_t index, int item_size, int title_width) {
    auto& item = items_[index];
    const bool running = item.window >= 0;
    const bool focused = running && windows_[static_cast<std::size_t>(item.window)].focused;
    const bool show_title = title_width > 0 && item.type != ItemType::Pinned;

    // the item root is an Overlay so the indicator can hang 2px below the
    // icon like Noctalia's anchors.bottomMargin: -2
    auto* root = Gtk::make_managed<Gtk::Overlay>();
    root->add_css_class("taskbar-item");
    if (focused)
        root->add_css_class("focused");
    if (running && !focused && Config::get().taskbar().running_indicator)
        root->add_css_class("running"); // grey dot: opened, not focused
    if (item.id == hovered_id_)
        root->add_css_class("hovered");
    if (show_title)
        root->add_css_class("with-title");
    root->set_tooltip_text(item.title.empty() ? item.app_id : item.title);

    auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, kMarginS);
    row->set_valign(Gtk::Align::CENTER);

    auto* icon_box = Gtk::make_managed<Gtk::Overlay>();
    icon_box->set_size_request(item_size, item_size + 2);
    auto* icon = Gtk::make_managed<Gtk::Image>();
    icon->add_css_class("taskbar-icon");
    icon->set_pixel_size(item_size);
    icon->set_valign(Gtk::Align::START);
    icon->set_halign(Gtk::Align::CENTER);
    apply_icon(*icon, Apps::get().lookup_for_class(item.app_id), item.app_id);
    icon_box->set_child(*icon);
    if (!show_title) {
        auto* indicator = Gtk::make_managed<Gtk::Box>();
        indicator->add_css_class("taskbar-indicator");
        indicator->set_size_request(to_odd(item_size * 0.25), kIndicatorHeight);
        indicator->set_halign(Gtk::Align::CENTER);
        indicator->set_valign(Gtk::Align::END);
        indicator->set_can_target(false);
        icon_box->add_overlay(*indicator);
    }
    row->append(*icon_box);

    if (show_title) {
        auto* title = Gtk::make_managed<Gtk::Label>(item.title);
        title->add_css_class("taskbar-title");
        title->set_ellipsize(Pango::EllipsizeMode::END);
        title->set_xalign(0.0f);
        title->set_single_line_mode(true);
        title->set_size_request(title_width, -1);
        title->set_max_width_chars(1); // the size request wins, never the text
        title->set_hexpand(false);
        row->append(*title);
        item.title_label = title;
    }
    root->set_child(*row);

    // hover
    auto motion = Gtk::EventControllerMotion::create();
    const std::string id = item.id;
    motion->signal_enter().connect([this, id](double, double) { set_hovered(id, true); });
    motion->signal_leave().connect([this, id] { set_hovered(id, false); });
    root->add_controller(motion);

    // left click focuses / launches (no right-click menu, per user)
    auto click = Gtk::GestureClick::create();
    click->set_button(GDK_BUTTON_PRIMARY);
    click->signal_released().connect([this, id](int, double, double) {
        auto it = std::find_if(items_.begin(), items_.end(),
                               [&](const Item& i) { return i.id == id; });
        if (it == items_.end())
            return;
        if (it->window >= 0)
            Hyprland::get().focus_window(windows_[static_cast<std::size_t>(it->window)].address);
        else if (it->type == ItemType::Pinned)
            launch_pinned(it->app_id);
    });
    root->add_controller(click);

    // drag to reorder: the payload is the source index
    auto drag = Gtk::DragSource::create();
    drag->set_actions(Gdk::DragAction::MOVE);
    drag->signal_prepare().connect(
        [index](double, double) -> Glib::RefPtr<Gdk::ContentProvider> {
            Glib::Value<int> value;
            value.init(Glib::Value<int>::value_type());
            value.set(static_cast<int>(index));
            return Gdk::ContentProvider::create(value);
        },
        false);
    drag->signal_drag_begin().connect([drag, icon, item_size](const Glib::RefPtr<Gdk::Drag>&) {
        if (auto paintable = icon->get_paintable())
            drag->set_icon(paintable, item_size / 2, item_size / 2);
    });
    root->add_controller(drag);

    auto drop = Gtk::DropTarget::create(Glib::Value<int>::value_type(), Gdk::DragAction::MOVE);
    drop->signal_drop().connect(
        [this, index](const Glib::ValueBase& value, double, double) {
            if (!G_VALUE_HOLDS_INT(value.gobj()))
                return false;
            const int from = static_cast<const Glib::Value<int>&>(value).get();
            if (from < 0)
                return false;
            reorder(static_cast<std::size_t>(from), index);
            return true;
        },
        false);
    root->add_controller(drop);

    return root;
}

void Taskbar::set_hovered(const std::string& id, bool hovered) {
    if (hovered)
        hovered_id_ = id;
    else if (hovered_id_ == id)
        hovered_id_.clear();
    for (const auto& item : items_)
        if (item.root) {
            if (item.id == id && hovered)
                item.root->add_css_class("hovered");
            else
                item.root->remove_css_class("hovered");
        }
}

// visible: always; hidden: hide when empty; transparent: keep the space,
// fade out (Noctalia's opacity Behavior, 300ms OutCubic)
void Taskbar::apply_hide_mode() {
    const bool has_items = !items_.empty();
    switch (Config::get().taskbar().hide_mode) {
    case Config::Taskbar::HideMode::Visible:
        set_visible(true);
        animate_opacity(1.0);
        break;
    case Config::Taskbar::HideMode::Hidden:
        set_visible(has_items);
        animate_opacity(1.0);
        break;
    case Config::Taskbar::HideMode::Transparent:
        set_visible(true);
        animate_opacity(has_items ? 1.0 : 0.0);
        break;
    }
}

void Taskbar::animate_opacity(double target) {
    if (std::abs(opacity_target_ - target) < 1e-6)
        return;
    opacity_target_ = target;
    opacity_from_ = get_opacity();
    fade_start_us_ = 0;
    if (!get_mapped()) {
        set_opacity(target);
        return;
    }
    if (fade_running_)
        return; // the running tick picks up the new target
    fade_running_ = true;
    add_tick_callback([this](const Glib::RefPtr<Gdk::FrameClock>& clock) {
        const gint64 now = clock->get_frame_time();
        if (fade_start_us_ == 0)
            fade_start_us_ = now;
        const double t = std::clamp((now - fade_start_us_) / (kFadeMs * 1000.0), 0.0, 1.0);
        const double eased = 1.0 - std::pow(1.0 - t, 3); // OutCubic
        set_opacity(opacity_from_ + (opacity_target_ - opacity_from_) * eased);
        if (t < 1.0)
            return true;
        fade_running_ = false;
        return false;
    });
}

// Wheel: cycle focus through the running windows, one step per notch, with
// Noctalia's 150ms cooldown. GTK's dy > 0 is scroll-down = next.
bool Taskbar::on_scroll(double dx, double dy) {
    if (wheel_cooldown_ || items_.empty())
        return true;
    scroll_accum_ += std::abs(dy) >= std::abs(dx) ? dy : dx;
    if (std::abs(scroll_accum_) < 1.0)
        return true;
    const int direction = scroll_accum_ > 0 ? 1 : -1;
    scroll_accum_ = 0.0;

    int current = -1;
    for (std::size_t i = 0; i < items_.size(); ++i)
        if (items_[i].window >= 0 &&
            windows_[static_cast<std::size_t>(items_[i].window)].focused) {
            current = static_cast<int>(i);
            break;
        }
    if (current < 0)
        for (std::size_t i = 0; i < items_.size(); ++i)
            if (items_[i].window >= 0) {
                current = static_cast<int>(i);
                break;
            }
    if (current >= 0) {
        const int n = static_cast<int>(items_.size());
        const auto& next = items_[static_cast<std::size_t>((current + direction + n) % n)];
        if (next.window >= 0)
            Hyprland::get().focus_window(windows_[static_cast<std::size_t>(next.window)].address);
    }
    wheel_cooldown_ = true;
    wheel_timer_.disconnect();
    wheel_timer_ = Glib::signal_timeout().connect(
        [this] {
            wheel_cooldown_ = false;
            scroll_accum_ = 0.0;
            return false;
        },
        kWheelCooldownMs);
    return true;
}

// reorderApps: move the dragged item, remember the session order, persist
// the pinned order
void Taskbar::reorder(std::size_t from, std::size_t to) {
    if (from == to || from >= items_.size() || to >= items_.size())
        return;
    Item moved = std::move(items_[from]);
    items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(from));
    items_.insert(items_.begin() + static_cast<std::ptrdiff_t>(to), std::move(moved));
    session_order_.clear();
    for (const auto& item : items_)
        session_order_.push_back(item_key(item));
    save_pinned_order();
    rebuild({});
}

// savePinnedOrder: pinned ids in their visual order, then any pinned app that
// is not shown (filtered by monitor / workspace) in its old place
void Taskbar::save_pinned_order() {
    auto& apps = Apps::get();
    const auto current = apps.pinned();
    std::vector<std::string> ordered;
    const auto push_stored = [&](const std::string& app_id) {
        const std::string keys[] = {normalize_app_id(app_id),
                                    normalize_app_id(desktop_id_for(app_id))};
        for (const auto& stored : current)
            for (const auto& key : keys)
                if (normalize_app_id(stored) == key &&
                    std::find(ordered.begin(), ordered.end(), stored) == ordered.end()) {
                    ordered.push_back(stored);
                    return;
                }
    };
    for (const auto& item : items_)
        if (!item.app_id.empty())
            push_stored(item.app_id);
    for (const auto& stored : current)
        if (std::find(ordered.begin(), ordered.end(), stored) == ordered.end())
            ordered.push_back(stored);
    apps.set_pinned(std::move(ordered));
}

void Taskbar::launch_pinned(const std::string& app_id) {
    auto& apps = Apps::get();
    const auto* entry = apps.find_by_id(app_id);
    if (!entry)
        entry = apps.lookup_for_class(app_id);
    if (!entry) {
        g_warning("taskbar: no desktop entry for pinned app %s", app_id.c_str());
        return;
    }
    apps.launch(*entry);
}

} // namespace hyprshell
