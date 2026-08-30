#pragma once

#include <gtkmm.h>

#include <string>

namespace hyprshell {

class ActiveWindow : public Gtk::Label {
public:
    ActiveWindow();

private:
    void update_title(const std::string& title);
};

} // namespace hyprshell
