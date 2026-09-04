#include "bar/vpn_panel.hpp"

#include "services/vpn.hpp"

namespace hyprshell {

namespace {

// noctalia-tabler-icons glyphs (\u escapes — never literal PUA)
constexpr const char* kShield = "";
constexpr const char* kShieldLock = "";
constexpr const char* kShieldOff = "";
constexpr const char* kPlus = "";
constexpr const char* kRefresh = "";
constexpr const char* kClose = "";
constexpr const char* kTrash = "";

Gtk::Button* round_button(const char* glyph, const char* tooltip) {
    auto* button = Gtk::make_managed<Gtk::Button>();
    auto* label = Gtk::make_managed<Gtk::Label>(glyph);
    label->add_css_class("vp-round-icon");
    button->set_child(*label);
    button->add_css_class("vp-round-btn");
    button->set_tooltip_text(tooltip);
    button->set_has_frame(false);
    return button;
}

} // namespace

VpnPanel::VpnPanel() : Gtk::Box(Gtk::Orientation::VERTICAL, 9) {
    add_css_class("vpn-panel");
    set_size_request(440, 500);
    set_margin(13);

    // header card: icon, title, import / refresh / close
    auto* header = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 9);
    header->add_css_class("bp-card");
    header->add_css_class("vp-header");
    header_icon_.set_text(kShield);
    header_icon_.add_css_class("vp-header-icon");
    header->append(header_icon_);
    auto* title = Gtk::make_managed<Gtk::Label>("VPN");
    title->add_css_class("np-title");
    title->set_xalign(0.0f);
    title->set_hexpand(true);
    header->append(*title);
    auto* import = round_button(kPlus, "Import profile");
    import->signal_clicked().connect(sigc::mem_fun(*this, &VpnPanel::pick_import_file));
    header->append(*import);
    append(*header);
    VpnService::get().signal_changed().connect(
        [import] { import->set_sensitive(!VpnService::get().importing()); });

    scroller_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    scroller_.set_vexpand(true);
    scroller_.set_child(list_);
    append(scroller_);

    VpnService::get().signal_changed().connect([this] {
        if (open_)
            rebuild();
    });
    rebuild();
}

void VpnPanel::set_open(bool open) {
    open_ = open;
    if (open) {
        confirming_uuid_.clear();
        VpnService::get().refresh(); // Noctalia: refresh the profile list on every open
        rebuild();
    }
}

// Noctalia's NFilePicker: WireGuard .conf / OpenVPN .ovpn, starting in
// ~/Downloads. **No parent window**: the panel's root is the bar, a
// layer-shell surface, and making an xdg dialog transient for it is a Wayland
// protocol error that kills the whole shell ("Lost connection to Wayland
// compositor"). The popover closes first so its grab does not fight the dialog.
void VpnPanel::pick_import_file() {
    if (auto* popover = dynamic_cast<Gtk::Popover*>(get_ancestor(GTK_TYPE_POPOVER)))
        popover->popdown();
    GtkFileDialog* dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Select a WireGuard (.conf) or OpenVPN (.ovpn) file");
    GListStore* filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    GtkFileFilter* vpn_filter = gtk_file_filter_new();
    gtk_file_filter_set_name(vpn_filter, "VPN profiles (*.conf, *.ovpn)");
    gtk_file_filter_add_pattern(vpn_filter, "*.conf");
    gtk_file_filter_add_pattern(vpn_filter, "*.ovpn");
    g_list_store_append(filters, vpn_filter);
    GtkFileFilter* all = gtk_file_filter_new();
    gtk_file_filter_set_name(all, "All files");
    gtk_file_filter_add_pattern(all, "*");
    g_list_store_append(filters, all);
    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
    gtk_file_dialog_set_default_filter(dialog, vpn_filter);
    g_object_unref(all);
    g_object_unref(vpn_filter);
    g_object_unref(filters);
    const auto downloads = Glib::get_user_special_dir(Glib::UserDirectory::DOWNLOAD);
    GFile* folder = g_file_new_for_path((downloads.empty() ? Glib::get_home_dir() : downloads).c_str());
    gtk_file_dialog_set_initial_folder(dialog, folder);
    g_object_unref(folder);
    gtk_file_dialog_open(
        dialog, /*parent=*/nullptr, nullptr,
        [](GObject* source, GAsyncResult* result, gpointer) {
            GFile* file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), result, nullptr);
            if (file != nullptr) {
                gchar* path = g_file_get_path(file);
                if (path != nullptr)
                    VpnService::get().import_config(path);
                g_free(path);
                g_object_unref(file);
            }
        },
        nullptr);
    g_object_unref(dialog);
}

Gtk::Widget* VpnPanel::make_profile_card(const std::string& uuid) {
    auto& vpn = VpnService::get();
    const auto& conn = vpn.connections().at(uuid);
    auto* card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    card->add_css_class("bp-card");
    card->add_css_class("vp-profile");
    const bool any_busy = vpn.busy();

    if (confirming_uuid_ == uuid) {
        // confirm-delete state: warning + confirm / cancel
        auto* icon = Gtk::make_managed<Gtk::Label>(kTrash);
        icon->add_css_class("vp-trash-icon");
        card->append(*icon);
        auto* text = Gtk::make_managed<Gtk::Label>("Delete this VPN profile?");
        text->add_css_class("vp-confirm-text");
        text->set_xalign(0.0f);
        text->set_hexpand(true);
        text->set_ellipsize(Pango::EllipsizeMode::END);
        card->append(*text);
        auto* del = Gtk::make_managed<Gtk::Button>("Delete");
        del->add_css_class("vp-delete-btn");
        del->set_sensitive(!any_busy);
        del->signal_clicked().connect([this, uuid] {
            confirming_uuid_.clear();
            VpnService::get().remove(uuid);
        });
        card->append(*del);
        auto* cancel = round_button(kClose, "Cancel");
        cancel->signal_clicked().connect([this] {
            confirming_uuid_.clear();
            rebuild();
        });
        card->append(*cancel);
        return card;
    }

    // normal state: NToggle (icon, name, state, switch) + delete button
    auto* icon = Gtk::make_managed<Gtk::Label>(conn.active ? kShieldLock : kShield);
    icon->add_css_class("vp-profile-icon");
    card->append(*icon);
    auto* texts = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
    texts->set_hexpand(true);
    texts->set_valign(Gtk::Align::CENTER);
    auto* name = Gtk::make_managed<Gtk::Label>(conn.name);
    name->add_css_class("np-ssid");
    name->set_xalign(0.0f);
    name->set_ellipsize(Pango::EllipsizeMode::END);
    texts->append(*name);
    const char* state = vpn.connecting() && vpn.connecting_uuid() == uuid         ? "Connecting…"
                        : vpn.disconnecting() && vpn.disconnecting_uuid() == uuid ? "Disconnecting…"
                        : vpn.removing() && vpn.removing_uuid() == uuid           ? "Removing…"
                        : conn.active                                             ? "Connected"
                                                                                  : "Not connected";
    auto* sub = Gtk::make_managed<Gtk::Label>(state);
    sub->add_css_class("bt-sub");
    sub->set_xalign(0.0f);
    texts->append(*sub);
    card->append(*texts);
    auto* toggle = Gtk::make_managed<Gtk::Switch>();
    toggle->set_valign(Gtk::Align::CENTER);
    toggle->set_active(conn.active);
    toggle->set_sensitive(!any_busy);
    toggle->property_active().signal_changed().connect([toggle, uuid, active = conn.active] {
        if (toggle->get_active() == active)
            return; // set programmatically
        if (toggle->get_active())
            VpnService::get().connect(uuid);
        else
            VpnService::get().disconnect(uuid);
    });
    card->append(*toggle);
    auto* trash = round_button(kTrash, "Delete");
    trash->add_css_class("vp-trash-btn");
    trash->set_sensitive(!any_busy);
    trash->signal_clicked().connect([this, uuid] {
        confirming_uuid_ = uuid;
        rebuild();
    });
    card->append(*trash);
    return card;
}

void VpnPanel::rebuild() {
    auto& vpn = VpnService::get();
    header_icon_.set_text(vpn.has_active_connection() ? kShieldLock : kShield);
    if (vpn.has_active_connection())
        header_icon_.add_css_class("active");
    else
        header_icon_.remove_css_class("active");

    while (auto* child = list_.get_first_child())
        list_.remove(*child);

    if (vpn.connections().empty()) {
        // empty state (Noctalia's emptyBox)
        auto* card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 13);
        card->add_css_class("bp-card");
        card->add_css_class("vp-empty");
        card->set_vexpand(true);
        card->set_valign(Gtk::Align::FILL);
        auto* spacer_top = Gtk::make_managed<Gtk::Box>();
        spacer_top->set_vexpand(true);
        card->append(*spacer_top);
        auto* icon = Gtk::make_managed<Gtk::Label>(kShieldOff);
        icon->add_css_class("vp-empty-icon");
        card->append(*icon);
        auto* title = Gtk::make_managed<Gtk::Label>("No VPN profiles");
        title->add_css_class("vp-empty-title");
        card->append(*title);
        auto* hint = Gtk::make_managed<Gtk::Label>(
            "Import a WireGuard or OpenVPN profile and it will appear here.");
        hint->add_css_class("vp-empty-hint");
        hint->set_wrap(true);
        hint->set_justify(Gtk::Justification::CENTER);
        hint->set_max_width_chars(1);
        hint->set_hexpand(true);
        card->append(*hint);
        auto* import_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        auto* import_icon = Gtk::make_managed<Gtk::Label>(kPlus);
        import_icon->add_css_class("vp-btn-icon");
        import_box->append(*import_icon);
        import_box->append(*Gtk::make_managed<Gtk::Label>("Import profile"));
        auto* import = Gtk::make_managed<Gtk::Button>();
        import->set_child(*import_box);
        import->add_css_class("vp-import-btn");
        import->set_halign(Gtk::Align::CENTER);
        import->set_sensitive(!vpn.importing());
        import->signal_clicked().connect(sigc::mem_fun(*this, &VpnPanel::pick_import_file));
        card->append(*import);
        auto* spacer_bottom = Gtk::make_managed<Gtk::Box>();
        spacer_bottom->set_vexpand(true);
        card->append(*spacer_bottom);
        list_.append(*card);
    } else {
        // active connections first, then inactive (Noctalia's order)
        for (const auto& conn : vpn.active_connections())
            list_.append(*make_profile_card(conn.uuid));
        for (const auto& conn : vpn.inactive_connections())
            list_.append(*make_profile_card(conn.uuid));
    }
    if (!vpn.last_error().empty()) {
        auto* error = Gtk::make_managed<Gtk::Label>(vpn.last_error());
        error->add_css_class("vp-error");
        error->set_wrap(true);
        error->set_xalign(0.0f);
        list_.append(*error);
    }
}

} // namespace hyprshell
