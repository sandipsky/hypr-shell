#include "bar/modules/workspaces.hpp"

#include "services/hyprland.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace hyprshell {

using nlohmann::json;

Workspaces::Workspaces() : Gtk::Box(Gtk::Orientation::HORIZONTAL, 0) {
    add_css_class("module");
    add_css_class("workspaces");

    auto scroll = Gtk::EventControllerScroll::create();
    scroll->set_flags(Gtk::EventControllerScroll::Flags::VERTICAL);
    scroll->signal_scroll().connect(sigc::mem_fun(*this, &Workspaces::on_scroll), false);
    add_controller(scroll);

    Hyprland::get().signal_event().connect(sigc::mem_fun(*this, &Workspaces::on_event));
    refresh();
}

bool Workspaces::on_scroll(double /*dx*/, double dy) {
    // Accumulate smooth-scroll deltas (touchpads send many small ones); a mouse
    // wheel notch is exactly ±1.0. Switch once per whole unit.
    scroll_accum_ += dy;
    if (scroll_accum_ >= 1.0) {
        scroll_accum_ = 0.0;
        Hyprland::get().focus_workspace("e+1");
    } else if (scroll_accum_ <= -1.0) {
        scroll_accum_ = 0.0;
        Hyprland::get().focus_workspace("e-1");
    }
    return true;
}

void Workspaces::on_event(const std::string& name, const std::string& /*data*/) {
    static constexpr const char* interesting[] = {
        "workspace",        "workspacev2",        "createworkspace", "createworkspacev2",
        "destroyworkspace", "destroyworkspacev2", "renameworkspace", "focusedmon",
        // window count changes the "occupied" state of a workspace
        "openwindow",       "closewindow",        "movewindow",      "movewindowv2",
    };
    for (const auto* candidate : interesting) {
        if (name == candidate) {
            refresh();
            return;
        }
    }
}

void Workspaces::refresh() {
    auto& hypr = Hyprland::get();
    if (!hypr.available()) {
        return;
    }
    auto serial = ++refresh_serial_;
    hypr.request("j/workspaces", [this, serial, &hypr](const std::string& workspaces_reply) {
        hypr.request("j/activeworkspace", [this, serial, workspaces_reply](const std::string& active_reply) {
            if (serial != refresh_serial_) {
                return; // superseded by a newer refresh
            }
            try {
                std::vector<Entry> entries;
                for (const auto& ws : json::parse(workspaces_reply)) {
                    int id = ws.value("id", -1);
                    if (id < 0) {
                        continue; // special workspaces (scratchpads)
                    }
                    entries.push_back({id, ws.value("name", std::to_string(id)), ws.value("windows", 0)});
                }
                std::sort(entries.begin(), entries.end(),
                          [](const Entry& a, const Entry& b) { return a.id < b.id; });
                rebuild(entries, json::parse(active_reply).value("id", -1));
            } catch (const std::exception& e) {
                g_warning("workspace refresh failed: %s", e.what());
            }
        });
    });
}

void Workspaces::rebuild(const std::vector<Entry>& entries, int active_id) {
    while (auto* child = get_first_child()) {
        remove(*child);
    }
    for (const auto& entry : entries) {
        auto* button = Gtk::make_managed<Gtk::Button>(entry.name);
        if (entry.id == active_id) {
            button->add_css_class("active");
        }
        if (entry.windows > 0) {
            button->add_css_class("occupied");
        }
        button->signal_clicked().connect([id = entry.id] {
            Hyprland::get().focus_workspace(id);
        });
        append(*button);
    }
}

} // namespace hyprshell
