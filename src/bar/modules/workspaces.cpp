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

void Workspaces::on_event(const std::string& name, const std::string& /*data*/) {
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
    }
}

} // namespace hyprshell
