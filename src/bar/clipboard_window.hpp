#pragma once

#include "services/clipboard.hpp"

#include <gtkmm.h>

#include <memory>
#include <string>
#include <vector>

namespace hyprshell {

// Clipboard history window: Noctalia's clipboard launcher provider as its own
// overlay (user request — separate from the launcher, sharing its design: the
// widgets carry the launcher's CSS classes). Search box, one row per cliphist
// entry (text with a type glyph / colour swatch, images with a thumbnail),
// Enter or click copies — or pastes, with `clipboard.paste_on_click` — and
// Delete / the trash button removes an entry. Toggled from the bar's
// clipboard module or `hypr-shell --clipboard` (bind = SUPER, V, exec, …).
class ClipboardWindow : public Gtk::ApplicationWindow {
public:
    ClipboardWindow();
    ~ClipboardWindow() override;

    void toggle();
    void open();
    void close_window();

private:
    struct Row {
        Clipboard::Item item;
        bool message = false; // empty / disabled / loading state, not activatable
        std::string name;
        std::string description;
        std::string glyph;
    };

    void apply_position();
    void update_results();
    void rebuild_rows();
    Gtk::Widget* make_icon(const Row& row, std::size_t index);
    void select(int index, bool scroll_into_view);
    void activate_index(int index);
    void delete_index(int index);
    bool on_key_pressed(guint keyval, guint keycode, Gdk::ModifierType state);

    Gtk::Overlay overlay_;
    Gtk::Box backdrop_;
    Gtk::Box panel_{Gtk::Orientation::VERTICAL, 13};
    Gtk::Entry search_;
    Gtk::ScrolledWindow scroller_;
    Gtk::Box list_{Gtk::Orientation::VERTICAL, 6};
    Gtk::Box footer_{Gtk::Orientation::VERTICAL, 6};
    Gtk::Separator divider_{Gtk::Orientation::HORIZONTAL};
    Gtk::Box footer_row_{Gtk::Orientation::HORIZONTAL, 6};
    Gtk::Label count_label_;
    Gtk::Button clear_button_;

    std::vector<Row> rows_;
    std::vector<Gtk::Widget*> row_widgets_;
    std::vector<Gtk::Widget*> delete_buttons_; // parallel to row_widgets_ (nullptr for messages)
    int selected_ = 0;
    unsigned rebuild_serial_ = 0; // stale thumbnail callbacks are dropped
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

    int panel_width_ = 0;
    int panel_height_ = 0;

    // Noctalia's ignoreMouseHover (see the launcher)
    bool mouse_active_ = false;
    bool mouse_primed_ = false;
    double mouse_x_ = 0.0, mouse_y_ = 0.0;
};

} // namespace hyprshell
