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
    void refresh();
    void rebuild(const std::vector<Entry>& entries, int active_id);
    bool on_scroll(double dx, double dy);

    uint64_t refresh_serial_ = 0;
    double scroll_accum_ = 0.0;
};

} // namespace hyprshell
