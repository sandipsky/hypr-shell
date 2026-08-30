#include "bar/modules/active_window.hpp"

#include "services/hyprland.hpp"

#include <nlohmann/json.hpp>

namespace hyprshell {

ActiveWindow::ActiveWindow() {
    add_css_class("module");
    add_css_class("active-window");
    set_ellipsize(Pango::EllipsizeMode::END);
    set_max_width_chars(70);

    auto& hypr = Hyprland::get();
    hypr.signal_event().connect([this](const std::string& name, const std::string& data) {
        if (name == "activewindow") {
            // DATA is "<class>,<title>" — split on the first comma
            auto sep = data.find(',');
            update_title(sep == std::string::npos ? data : data.substr(sep + 1));
        }
    });

    hypr.request("j/activewindow", [this](const std::string& reply) {
        try {
            update_title(nlohmann::json::parse(reply).value("title", ""));
        } catch (const std::exception&) {
            // no active window at startup
        }
    });
}

void ActiveWindow::update_title(const std::string& title) {
    set_label(title);
    if (title.empty()) {
        set_has_tooltip(false);
    } else {
        set_tooltip_text(title);
    }
}

} // namespace hyprshell
