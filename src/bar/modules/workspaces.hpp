#pragma once

#include <gtkmm.h>

#include <cstdint>
#include <string>
#include <vector>

namespace hyprshell {

class Workspaces : public Gtk::Box {
public:
    Workspaces();

private:
    struct Entry {
        int id;
        std::string name;
        int windows;
    };

    void on_event(const std::string& name, const std::string& data);
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
    sigc::connection refresh_timer_;
};

} // namespace hyprshell
