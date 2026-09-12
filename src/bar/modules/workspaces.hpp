#pragma once

#include "bar/modules/corner_target.hpp"

#include <gtkmm.h>

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace hyprshell {

class Workspaces : public Gtk::Box, public CornerTarget {
public:
    Workspaces();

    // corner click = the first (start corner) or last (end corner) workspace button
    void activate_corner(bool start) override;

private:
    struct Entry {
        int id;
        std::string name;
        int windows;
    };

    void on_event(const std::string& name, const std::string& data);
    void on_urgent(const std::string& address); // a window asked for attention
    void schedule_refresh(); // events arrive in bursts — one refresh per burst
    void refresh();
    void rebuild(const std::vector<Entry>& entries, int active_id);
    bool on_scroll(double dx, double dy);
    void step(int dir);

    uint64_t refresh_serial_ = 0;
    double scroll_accum_ = 0.0;
    std::vector<int> shown_ids_; // sorted, drives scroll stepping
    std::vector<Gtk::Button*> buttons_; // one per shown_ids_ entry, reused across refreshes
    int active_id_ = -1;
    // workspaces flagged by `urgent` / a window opening off-screen; a flag is
    // cleared when its workspace becomes the active one (Quickshell semantics)
    std::set<int> urgent_ids_;
    std::string opened_workspace_; // WORKSPACENAME from the last openwindow event
    sigc::connection refresh_timer_;
};

} // namespace hyprshell
