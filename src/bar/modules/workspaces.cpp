#include "bar/modules/workspaces.hpp"

#include "services/config.hpp"
#include "services/hyprland.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace hyprshell {

using nlohmann::json;

namespace {

void set_class(Gtk::Widget& widget, const char* name, bool on) {
    if (on)
        widget.add_css_class(name);
    else
        widget.remove_css_class(name);
}

} // namespace

Workspaces::Workspaces() : Gtk::Box(Gtk::Orientation::HORIZONTAL, 0) {
    add_css_class("module");
    add_css_class("workspaces");

    auto scroll = Gtk::EventControllerScroll::create();
    scroll->set_flags(Gtk::EventControllerScroll::Flags::VERTICAL);
    scroll->signal_scroll().connect(sigc::mem_fun(*this, &Workspaces::on_scroll), false);
    add_controller(scroll);

    Hyprland::get().signal_event().connect(sigc::mem_fun(*this, &Workspaces::on_event));
    Config::get().signal_changed().connect(sigc::mem_fun(*this, &Workspaces::refresh));
    refresh();
}

void Workspaces::activate_corner(bool start) {
    const std::size_t shown = shown_ids_.size();
    if (shown == 0 || buttons_.size() < shown)
        return;
    buttons_[start ? 0 : shown - 1]->activate(); // runs the button's click handler
}

bool Workspaces::on_scroll(double /*dx*/, double dy) {
    // Accumulate smooth-scroll deltas (touchpads send many small ones); a mouse
    // wheel notch is exactly ±1.0. Switch once per whole unit.
    scroll_accum_ += dy;
    if (scroll_accum_ >= 1.0) {
        scroll_accum_ = 0.0;
        step(+1);
    } else if (scroll_accum_ <= -1.0) {
        scroll_accum_ = 0.0;
        step(-1);
    }
    return true;
}

// Step through the displayed workspaces (locally, not via Hyprland's e+1 —
// fixed mode navigates placeholders too, and wrap-around is configurable).
void Workspaces::step(int dir) {
    if (shown_ids_.empty()) {
        return;
    }
    const bool wrap = Config::get().workspaces_scroll_wrap();
    int target = -1;
    if (dir > 0) {
        auto it = std::upper_bound(shown_ids_.begin(), shown_ids_.end(), active_id_);
        if (it != shown_ids_.end()) {
            target = *it;
        } else if (wrap) {
            target = shown_ids_.front();
        }
    } else {
        auto it = std::lower_bound(shown_ids_.begin(), shown_ids_.end(), active_id_);
        if (it != shown_ids_.begin()) {
            target = *(it - 1);
        } else if (wrap) {
            target = shown_ids_.back();
        }
    }
    if (target >= 0 && target != active_id_) {
        Hyprland::get().focus_workspace(target);
    }
}

void Workspaces::on_event(const std::string& name, const std::string& data) {
    if (name == "urgent") {
        if (Config::get().workspaces_flash_urgent())
            on_urgent(data);
        return;
    }
    if (name == "openwindow") {
        // ADDRESS,WORKSPACENAME,CLASS,TITLE — remember where the window went;
        // rebuild() flags that workspace when it is not the active one.
        const auto first = data.find(',');
        const auto second = first == std::string::npos ? first : data.find(',', first + 1);
        if (first != std::string::npos)
            opened_workspace_ = data.substr(first + 1, second == std::string::npos
                                                          ? std::string::npos
                                                          : second - first - 1);
    }
    static constexpr const char* interesting[] = {
        "workspace",        "workspacev2",        "createworkspace", "createworkspacev2",
        "destroyworkspace", "destroyworkspacev2", "renameworkspace", "focusedmon",
        // window count changes the "occupied" state of a workspace
        "openwindow",       "closewindow",        "movewindow",      "movewindowv2",
    };
    for (const auto* candidate : interesting) {
        if (name == candidate) {
            schedule_refresh();
            return;
        }
    }
}

// Hyprland emits `urgent>>ADDRESS` when a window requests activation (an app
// opening a link in a browser that sits on another workspace, an XWayland
// urgency hint). Resolve the window's workspace and flag it unless it is the
// one on screen — Noctalia shows the same flag as its isUrgent pill.
void Workspaces::on_urgent(const std::string& address) {
    if (address.empty() || address.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
        return;
    const std::string full = "0x" + address;
    Hyprland::get().request("j/clients", [this, full](const std::string& reply) {
        try {
            for (const auto& client : json::parse(reply)) {
                if (client.value("address", "") != full)
                    continue;
                const int id = client.value("workspace", json::object()).value("id", -1);
                if (id >= 0 && id != active_id_) {
                    urgent_ids_.insert(id);
                    schedule_refresh();
                }
                return;
            }
        } catch (const std::exception& e) {
            g_warning("urgent window lookup failed: %s", e.what());
        }
    });
}

void Workspaces::schedule_refresh() {
    if (refresh_timer_.connected())
        return;
    refresh_timer_ = Glib::signal_timeout().connect(
        [this] {
            refresh();
            return false;
        },
        30);
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
    set_orientation(Config::get().bar_vertical() ? Gtk::Orientation::VERTICAL
                                                 : Gtk::Orientation::HORIZONTAL);
    // Fixed mode (Noctalia semantics): always show 1..count, placeholders for
    // ids that don't exist yet, and keep real workspaces beyond the range so
    // the focused one never disappears. Clicking a placeholder creates it.
    std::vector<Entry> shown = entries;
    auto& cfg = Config::get();
    if (cfg.workspaces_mode() == Config::WorkspacesMode::Fixed) {
        const int count = cfg.workspaces_fixed_count();
        shown.clear();
        for (int n = 1; n <= count; ++n) {
            auto match = std::find_if(entries.begin(), entries.end(),
                                      [n](const Entry& e) { return e.id == n; });
            shown.push_back(match != entries.end() ? *match
                                                   : Entry{n, std::to_string(n), 0});
        }
        for (const auto& entry : entries) {
            if (entry.id > count) {
                shown.push_back(entry);
            }
        }
    }

    shown_ids_.clear();
    for (const auto& entry : shown)
        shown_ids_.push_back(entry.id);
    active_id_ = active_id;

    // Urgency: a window that opened on a workspace other than the active one
    // flags it; focusing a workspace clears its flag; gone workspaces drop out.
    if (!cfg.workspaces_flash_urgent()) {
        urgent_ids_.clear();
    } else if (!opened_workspace_.empty()) {
        for (const auto& entry : entries)
            if (entry.name == opened_workspace_ && entry.id != active_id)
                urgent_ids_.insert(entry.id);
    }
    opened_workspace_.clear();
    urgent_ids_.erase(active_id);
    for (auto it = urgent_ids_.begin(); it != urgent_ids_.end();) {
        if (std::binary_search(shown_ids_.begin(), shown_ids_.end(), *it))
            ++it;
        else
            it = urgent_ids_.erase(it);
    }
    set_class(*this, "accent-active", cfg.workspaces_accent_active());

    // Reuse the existing buttons: a workspace switch or a window open/close
    // only flips classes and labels — no widget churn on every event.
    while (buttons_.size() > shown.size()) {
        remove(*buttons_.back());
        buttons_.pop_back();
    }
    while (buttons_.size() < shown.size()) {
        auto* button = Gtk::make_managed<Gtk::Button>();
        button->signal_clicked().connect([this, button] {
            const auto it = std::find(buttons_.begin(), buttons_.end(), button);
            if (it != buttons_.end())
                Hyprland::get().focus_workspace(shown_ids_[static_cast<std::size_t>(it - buttons_.begin())]);
        });
        append(*button);
        buttons_.push_back(button);
    }
    for (std::size_t i = 0; i < shown.size(); ++i) {
        auto* button = buttons_[i];
        if (button->get_label().raw() != shown[i].name)
            button->set_label(shown[i].name);
        set_class(*button, "active", shown[i].id == active_id);
        set_class(*button, "occupied", shown[i].windows > 0);
        set_class(*button, "urgent", urgent_ids_.count(shown[i].id) > 0);
    }
}

} // namespace hyprshell
