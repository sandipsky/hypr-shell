#include "bar/session_menu.hpp"

#include "services/config.hpp"
#include "services/session.hpp"

namespace hyprshell {

SessionMenuList::SessionMenuList() : Gtk::Box(Gtk::Orientation::VERTICAL, 2) {
    add_css_class("session-list");
    Config::get().signal_changed().connect(sigc::mem_fun(*this, &SessionMenuList::rebuild));
    rebuild();
}

void SessionMenuList::rebuild() {
    while (auto* child = get_first_child())
        remove(*child);

    const auto actions = enabled_session_actions();
    if (actions.empty()) {
        auto* empty = Gtk::make_managed<Gtk::Label>("No actions enabled");
        empty->add_css_class("session-empty");
        append(*empty);
        return;
    }
    for (const auto* action : actions) {
        auto* item = Gtk::make_managed<Gtk::Button>();
        item->add_css_class("session-item");
        if (action->destructive)
            item->add_css_class("destructive");
        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 9);
        auto* glyph = Gtk::make_managed<Gtk::Label>(action->glyph);
        glyph->add_css_class("session-glyph");
        row->append(*glyph);
        auto* label = Gtk::make_managed<Gtk::Label>(action->label);
        label->add_css_class("session-label");
        label->set_halign(Gtk::Align::START);
        label->set_hexpand(true);
        row->append(*label);
        item->set_child(*row);
        item->signal_clicked().connect([this, action] { activate_.emit(*action); });
        append(*item);
    }
}

} // namespace hyprshell
