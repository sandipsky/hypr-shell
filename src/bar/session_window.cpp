#include "bar/session_window.hpp"

#include "services/config.hpp"
#include "services/session.hpp"

#include <gtk4-layer-shell.h>

#include <algorithm>
#include <cmath>

namespace hyprshell {

namespace {

// Noctalia's LargeButton: 200x200 at uiScale 1, marginXL gaps
constexpr int kButtonSize = 200;
constexpr int kSpacing = 18;

} // namespace

SessionWindow::SessionWindow() {
    set_decorated(false);
    add_css_class("session-window");

    // Layer-shell before mapping: fullscreen overlay with exclusive keyboard
    // focus, like the launcher window.
    auto* window = GTK_WINDOW(gobj());
    gtk_layer_init_for_window(window);
    gtk_layer_set_namespace(window, "hypr-shell-session");
    gtk_layer_set_layer(window, GTK_LAYER_SHELL_LAYER_OVERLAY);
    gtk_layer_set_keyboard_mode(window, GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
    gtk_layer_set_exclusive_zone(window, -1);
    for (auto edge : {GTK_LAYER_SHELL_EDGE_TOP, GTK_LAYER_SHELL_EDGE_BOTTOM,
                      GTK_LAYER_SHELL_EDGE_LEFT, GTK_LAYER_SHELL_EDGE_RIGHT})
        gtk_layer_set_anchor(window, edge, true);

    // dimmed backdrop (the window background) — clicking it closes
    auto backdrop_click = Gtk::GestureClick::create();
    backdrop_click->signal_released().connect([this](int, double, double) { close_menu(); });
    backdrop_.add_controller(backdrop_click);

    grid_.set_halign(Gtk::Align::CENTER);
    grid_.set_valign(Gtk::Align::CENTER);
    grid_.set_row_spacing(kSpacing);
    grid_.set_column_spacing(kSpacing);

    overlay_.set_child(backdrop_);
    overlay_.add_overlay(grid_);
    set_child(overlay_);

    // hover only selects once the mouse really moved after opening
    auto motion = Gtk::EventControllerMotion::create();
    motion->signal_motion().connect([this](double x, double y) {
        if (!mouse_primed_) {
            mouse_primed_ = true;
            mouse_x_ = x;
            mouse_y_ = y;
            return;
        }
        if (std::abs(x - mouse_x_) + std::abs(y - mouse_y_) >= 5.0)
            mouse_active_ = true;
    });
    add_controller(motion);

    auto key = Gtk::EventControllerKey::create();
    key->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    key->signal_key_pressed().connect(sigc::mem_fun(*this, &SessionWindow::on_key_pressed),
                                      false);
    add_controller(key);

    Config::get().signal_changed().connect([this] {
        if (get_visible())
            rebuild();
    });
}

void SessionWindow::toggle() {
    if (get_visible())
        close_menu();
    else
        open();
}

void SessionWindow::open() {
    if (get_visible())
        return;
    mouse_active_ = false;
    mouse_primed_ = false;
    rebuild();
    present();
}

void SessionWindow::close_menu() {
    set_visible(false);
}

void SessionWindow::rebuild() {
    while (auto* child = grid_.get_first_child())
        grid_.remove(*child);
    buttons_.clear();
    selected_ = -1;

    actions_ = enabled_session_actions();
    const int count = static_cast<int>(actions_.size());
    // Noctalia's getGridInfo: one row, or up to 3 columns (ceil(sqrt(n)))
    const bool grid_layout =
        Config::get().session().fullscreen_layout == Config::Session::Layout::Grid;
    columns_ = count == 0 ? 1
               : grid_layout
                   ? std::min(3, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(count)))))
                   : count;

    for (int i = 0; i < count; ++i) {
        const auto* action = actions_[static_cast<std::size_t>(i)];
        auto* button = Gtk::make_managed<Gtk::Button>();
        button->add_css_class("session-big");
        if (action->destructive)
            button->add_css_class("destructive");
        button->set_size_request(kButtonSize, kButtonSize);
        button->set_can_focus(false); // the window's key controller navigates

        auto* content = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
        content->set_valign(Gtk::Align::CENTER);
        auto* glyph = Gtk::make_managed<Gtk::Label>(action->glyph);
        glyph->add_css_class("session-big-glyph");
        content->append(*glyph);
        auto* label = Gtk::make_managed<Gtk::Label>(action->label);
        label->add_css_class("session-big-label");
        content->append(*label);
        button->set_child(*content);

        button->signal_clicked().connect([this, i] { activate_index(i); });
        auto hover = Gtk::EventControllerMotion::create();
        hover->signal_enter().connect([this, i](double, double) {
            if (mouse_active_)
                select(i);
        });
        button->add_controller(hover);

        grid_.attach(*button, i % columns_, i / columns_);
        buttons_.push_back(button);
    }
}

void SessionWindow::select(int index) {
    if (buttons_.empty())
        return;
    index = std::clamp(index, 0, static_cast<int>(buttons_.size()) - 1);
    if (selected_ >= 0 && selected_ < static_cast<int>(buttons_.size()))
        buttons_[static_cast<std::size_t>(selected_)]->remove_css_class("selected");
    selected_ = index;
    buttons_[static_cast<std::size_t>(index)]->add_css_class("selected");
}

// Port of Noctalia's navigateGrid: left/right wrap within the row, up/down
// wrap across rows and clamp to the shorter last row; with nothing selected
// any move lands on the first entry.
void SessionWindow::navigate(int dx, int dy) {
    const int count = static_cast<int>(actions_.size());
    if (count == 0)
        return;
    if (selected_ < 0) {
        select(0);
        return;
    }
    const int rows = (count + columns_ - 1) / columns_;
    auto items_in_row = [&](int row) { return std::min(columns_, count - row * columns_); };
    int row = selected_ / columns_;
    int col = selected_ % columns_;
    if (dx != 0) {
        const int n = items_in_row(row);
        col = ((col + dx) % n + n) % n;
    }
    if (dy != 0) {
        row = ((row + dy) % rows + rows) % rows;
        col = std::min(col, items_in_row(row) - 1);
    }
    select(row * columns_ + col);
}

void SessionWindow::activate_index(int index) {
    if (index < 0 || index >= static_cast<int>(actions_.size()))
        return;
    // close first so the action's own UI (lock screen etc.) can take over
    const SessionAction* action = actions_[static_cast<std::size_t>(index)];
    close_menu();
    Glib::signal_idle().connect_once([action] { run_session_action(*action); });
}

bool SessionWindow::on_key_pressed(guint keyval, guint, Gdk::ModifierType) {
    const int count = static_cast<int>(actions_.size());
    switch (keyval) {
    case GDK_KEY_Escape:
        close_menu();
        return true;
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:
    case GDK_KEY_space:
        activate_index(selected_);
        return true;
    case GDK_KEY_Right:
        navigate(1, 0);
        return true;
    case GDK_KEY_Left:
        navigate(-1, 0);
        return true;
    case GDK_KEY_Down:
        navigate(0, 1);
        return true;
    case GDK_KEY_Up:
        navigate(0, -1);
        return true;
    case GDK_KEY_Tab:
        if (count > 0)
            select(selected_ < 0 ? 0 : (selected_ + 1) % count);
        return true;
    case GDK_KEY_ISO_Left_Tab:
        if (count > 0)
            select(selected_ < 0 ? count - 1 : (selected_ - 1 + count) % count);
        return true;
    case GDK_KEY_Home:
        select(0);
        return true;
    case GDK_KEY_End:
        select(count - 1);
        return true;
    default:
        break;
    }
    // Noctalia's default per-entry keybinds: 1..n pick an entry directly
    if (keyval >= GDK_KEY_1 && keyval <= GDK_KEY_9) {
        const int index = static_cast<int>(keyval - GDK_KEY_1);
        if (index < count) {
            select(index);
            activate_index(index);
        }
        return true;
    }
    return false;
}

} // namespace hyprshell
