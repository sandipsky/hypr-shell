#include "bar/modules/active_window.hpp"

#include "services/config.hpp"
#include "services/hyprland.hpp"

#include <giomm/desktopappinfo.h>
#include <nlohmann/json.hpp>

#include <algorithm>

namespace hyprshell {

namespace {

// Desktop entry for a Hyprland window class, matching the common id spellings.
// Cached for the last class: update() asks twice per focus change and each
// lookup parses a .desktop file from disk.
Glib::RefPtr<Gio::DesktopAppInfo> desktop_entry(const std::string& klass) {
    static std::string cached_class;
    static Glib::RefPtr<Gio::DesktopAppInfo> cached_info;
    if (klass.empty())
        return {};
    if (klass == cached_class)
        return cached_info;
    cached_class = klass;
    cached_info.reset();
    std::string lower = klass;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    for (const auto& id : {klass, lower}) {
        if (auto info = Gio::DesktopAppInfo::create(id + ".desktop")) {
            cached_info = info;
            break;
        }
    }
    return cached_info;
}

} // namespace

ActiveWindow::ActiveWindow() : Gtk::Box(Gtk::Orientation::HORIZONTAL, 6) {
    add_css_class("module");
    add_css_class("active-window");

    icon_.set_pixel_size(16);
    label_.set_ellipsize(Pango::EllipsizeMode::END);
    label_.set_max_width_chars(70);
    vertical_label_.set_draw_func(sigc::mem_fun(*this, &ActiveWindow::on_vertical_draw));
    append(icon_);
    append(label_);
    append(vertical_label_);

    auto& hypr = Hyprland::get();
    hypr.signal_event().connect([this](const std::string& name, const std::string& data) {
        if (name == "activewindow") {
            // DATA is "<class>,<title>" — split on the first comma
            auto sep = data.find(',');
            klass_ = sep == std::string::npos ? data : data.substr(0, sep);
            title_ = sep == std::string::npos ? "" : data.substr(sep + 1);
            update();
        }
    });

    hypr.request("j/activewindow", [this](const std::string& reply) {
        try {
            auto json = nlohmann::json::parse(reply);
            klass_ = json.value("class", "");
            title_ = json.value("title", "");
        } catch (const std::exception&) {
        }
        update();
    });

    Config::get().signal_changed().connect(sigc::mem_fun(*this, &ActiveWindow::update));
    update();
}

void ActiveWindow::update() {
    auto& cfg = Config::get();
    const bool has_window = !klass_.empty() || !title_.empty();

    // text: window title, application name, or the no-window placeholder
    Glib::ustring text;
    if (cfg.active_window_show_title()) {
        if (has_window) {
            if (cfg.active_window_title_mode() == Config::ActiveWindowText::AppName) {
                auto info = desktop_entry(klass_);
                if (info) {
                    text = info->get_display_name();
                } else {
                    text = klass_;
                }
            } else {
                text = title_;
            }
        } else {
            switch (cfg.active_window_empty_text()) {
            case Config::ActiveWindowEmpty::Default:
                text = "No active window";
                break;
            case Config::ActiveWindowEmpty::Desktop:
                text = "Desktop";
                break;
            case Config::ActiveWindowEmpty::None:
                break;
            }
        }
    }
    text_ = text;
    // vertical bars show the title rotated 90° (book-spine, like Noctalia),
    // with the icon above it
    const bool vertical = cfg.bar_vertical();
    set_orientation(vertical ? Gtk::Orientation::VERTICAL : Gtk::Orientation::HORIZONTAL);
    label_.set_text(text);
    label_.set_visible(!vertical && !text.empty());
    vertical_label_.set_visible(vertical && !text.empty());
    if (vertical && !text.empty()) {
        auto layout = vertical_label_.create_pango_layout(text_);
        int text_width = 0, text_height = 0;
        layout->get_pixel_size(text_width, text_height);
        vertical_label_.set_content_width(text_height);
        vertical_label_.set_content_height(std::min(text_width, 260));
        vertical_label_.queue_draw();
    }
    if (has_window && !title_.empty()) {
        set_tooltip_text(title_);
    } else {
        set_has_tooltip(false);
    }

    icon_.set_visible(cfg.active_window_show_icon() && has_window);
    apply_icon();

    bool shown = true;
    double opacity = 1.0;
    switch (cfg.active_window_hide_mode()) {
    case Config::ActiveWindowHide::Visible:
        break;
    case Config::ActiveWindowHide::Hidden:
        shown = has_window;
        break;
    case Config::ActiveWindowHide::Transparent:
        opacity = has_window ? 1.0 : 0.0;
        break;
    }
    set_opacity(opacity);
    set_visible(shown);
}

void ActiveWindow::on_vertical_draw(const Cairo::RefPtr<Cairo::Context>& cr, int width,
                                    int height) {
    auto layout = vertical_label_.create_pango_layout(text_);
    layout->set_ellipsize(Pango::EllipsizeMode::END);
    layout->set_width(height * PANGO_SCALE); // run length after rotation
    int text_width = 0, text_height = 0;
    layout->get_pixel_size(text_width, text_height);

    const auto color = vertical_label_.get_color();
    cr->set_source_rgba(color.get_red(), color.get_green(), color.get_blue(),
                        color.get_alpha());
    // rotate 90° clockwise: x runs down the bar, baseline centered across width
    cr->translate(width, 0);
    cr->rotate(G_PI / 2);
    cr->move_to(0, (width - text_height) / 2.0);
    layout->show_in_cairo_context(cr);
}

void ActiveWindow::apply_icon() {
    if (!icon_.get_visible()) {
        return;
    }
    if (auto info = desktop_entry(klass_); info && info->get_icon()) {
        icon_.set(info->get_icon());
    } else {
        icon_.set_from_icon_name("application-x-executable-symbolic");
    }
}

} // namespace hyprshell
