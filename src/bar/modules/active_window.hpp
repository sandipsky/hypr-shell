#pragma once

#include <gtkmm.h>

#include <string>

namespace hyprshell {

// Focused-window indicator: app icon + title/app-name text, with Noctalia's
// ActiveWindow widget settings (hide mode, text mode, placeholder text).
class ActiveWindow : public Gtk::Box {
public:
    ActiveWindow();

private:
    void update();
    void apply_icon();
    void on_vertical_draw(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height);

    Gtk::Image icon_;
    Gtk::Label label_;
    Gtk::DrawingArea vertical_label_; // title rotated 90° for vertical bars
    Glib::ustring text_;
    std::string klass_;
    std::string title_;
};

} // namespace hyprshell
