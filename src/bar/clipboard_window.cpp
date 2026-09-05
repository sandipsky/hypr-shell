#include "bar/clipboard_window.hpp"

#include "services/config.hpp"

#include <gtk4-layer-shell.h>

#include <algorithm>
#include <cctype>
#include <cmath>

namespace hyprshell {

namespace {

// noctalia-tabler-icons glyphs (\u escapes — never literal PUA)
constexpr const char* kIconClipboard = "\uEA6F";
constexpr const char* kIconLink = "\uEADE";
constexpr const char* kIconFile = "\uEAA4";
constexpr const char* kIconCode = "\uEA77";
constexpr const char* kIconPalette = "\uEB01";
constexpr const char* kIconPhoto = "\uEB0A";
constexpr const char* kIconTrash = "\uEB41";

constexpr int kIconSize = 36;       // the launcher's badge size
constexpr int kThumbWidth = 64;     // image thumbnails get a wider slot
constexpr int kMinPanelWidth = 552; // the launcher's fixed panel metrics
constexpr int kMinPanelHeight = 600;
constexpr int kEdgeMargin = 13;     // Style.marginL, distance from the screen edge
constexpr int kPageJump = 10;
constexpr unsigned kPasteDelayMs = 150; // let focus return before wtype presses keys

std::string lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return text;
}

std::string trimmed(const std::string& text) {
    const auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
        return "";
    const auto end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

// UTF-8-safe prefix of `max` characters with an ellipsis (Noctalia's 60/80 cuts)
std::string clipped(const std::string& text, std::size_t max) {
    const Glib::ustring u(text);
    if (u.size() <= max)
        return text;
    return u.substr(0, max - 3) + "...";
}

const char* kind_glyph(Clipboard::Item::Kind kind) {
    using Kind = Clipboard::Item::Kind;
    switch (kind) {
    case Kind::Image: return kIconPhoto;
    case Kind::Link: return kIconLink;
    case Kind::File: return kIconFile;
    case Kind::Code: return kIconCode;
    case Kind::Color: return kIconPalette;
    case Kind::Text: break;
    }
    return kIconClipboard;
}

} // namespace

ClipboardWindow::ClipboardWindow() {
    set_decorated(false);
    add_css_class("launcher"); // the launcher's dimmed backdrop
    add_css_class("clipboard");

    // Layer-shell before mapping: overlay layer with exclusive zone 0 so the
    // surface excludes the bar's strip — a top/bottom position sits next to
    // the bar, not under it. Keyboard mode is ON_DEMAND, not the launcher's
    // EXCLUSIVE: Hyprland routes pointer input ONLY to exclusive layer
    // surfaces while one is mapped (InputManager::mouseMoveUnified checks
    // m_exclusiveLSes first), so the bar's clipboard button could never be
    // clicked to close this window. On-demand surfaces still get keyboard
    // focus when they map (LayerSurface::onMap's GRABSFOCUS).
    auto* window = GTK_WINDOW(gobj());
    gtk_layer_init_for_window(window);
    gtk_layer_set_namespace(window, "hypr-shell-clipboard");
    gtk_layer_set_layer(window, GTK_LAYER_SHELL_LAYER_OVERLAY);
    gtk_layer_set_keyboard_mode(window, GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);
    gtk_layer_set_exclusive_zone(window, 0);
    for (auto edge : {GTK_LAYER_SHELL_EDGE_TOP, GTK_LAYER_SHELL_EDGE_BOTTOM,
                      GTK_LAYER_SHELL_EDGE_LEFT, GTK_LAYER_SHELL_EDGE_RIGHT})
        gtk_layer_set_anchor(window, edge, true);

    auto backdrop_click = Gtk::GestureClick::create();
    backdrop_click->signal_released().connect([this](int, double, double) { close_window(); });
    backdrop_.add_controller(backdrop_click);

    panel_.add_css_class("launcher-panel");
    panel_.add_css_class("clipboard-panel");
    panel_width_ = kMinPanelWidth;
    panel_height_ = kMinPanelHeight;
    apply_position();

    search_.add_css_class("launcher-search");
    search_.set_placeholder_text("Search clipboard...");
    search_.signal_changed().connect([this] {
        if (get_visible())
            update_results();
    });
    panel_.append(search_);

    scroller_.set_child(list_);
    scroller_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    scroller_.set_overlay_scrolling(false);
    scroller_.set_vexpand(true);
    panel_.append(scroller_);

    divider_.add_css_class("launcher-divider");
    footer_.append(divider_);
    count_label_.add_css_class("launcher-count");
    count_label_.set_halign(Gtk::Align::START);
    count_label_.set_hexpand(true);
    footer_row_.append(count_label_);
    clear_button_.set_label("Clear all");
    clear_button_.add_css_class("clipboard-clear");
    clear_button_.set_has_frame(false);
    clear_button_.set_tooltip_text("Remove every entry from the clipboard history");
    clear_button_.signal_clicked().connect([] { Clipboard::get().wipe(); });
    footer_row_.append(clear_button_);
    footer_.append(footer_row_);
    panel_.append(footer_);

    overlay_.set_child(backdrop_);
    overlay_.add_overlay(panel_);
    set_child(overlay_);

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
    panel_.add_controller(motion);

    auto key = Gtk::EventControllerKey::create();
    key->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    key->signal_key_pressed().connect(sigc::mem_fun(*this, &ClipboardWindow::on_key_pressed),
                                      false);
    add_controller(key);

    signal_map().connect([this] {
        g_debug("clipboard window: mapped");
        Glib::signal_timeout().connect_once(
            [this] { g_debug("clipboard window: keyboard focus %d", is_active()); }, 300);
    });
    signal_unmap().connect([] { g_debug("clipboard window: unmapped"); });
    Clipboard::get().signal_changed().connect([this] {
        if (get_visible())
            update_results();
    });
    Config::get().signal_changed().connect([this] {
        apply_position();
        if (get_visible())
            update_results();
    });
}

ClipboardWindow::~ClipboardWindow() {
    *alive_ = false;
}

void ClipboardWindow::toggle() {
    g_debug("clipboard window: toggle (visible %d, mapped %d)", get_visible(), get_mapped());
    if (get_visible())
        close_window();
    else
        open();
}

void ClipboardWindow::open() {
    if (get_visible())
        return;
    mouse_active_ = false;
    mouse_primed_ = false;
    search_.set_text("");
    Clipboard::get().refresh(); // Noctalia re-lists on every open
    update_results();
    present();
    search_.grab_focus();

    // panel: max(25% of the screen, 552) x max(50%, 600) like the launcher —
    // the window is the (bar-less) output, so its size is known after the
    // first allocation
    add_tick_callback([this](const Glib::RefPtr<Gdk::FrameClock>&) {
        if (get_width() <= 1)
            return true;
        panel_width_ = std::min(get_width() - 2 * kEdgeMargin,
                                std::max(kMinPanelWidth, get_width() / 4));
        panel_height_ = std::min(get_height() - 2 * kEdgeMargin,
                                 std::max(kMinPanelHeight, get_height() / 2));
        panel_.set_size_request(panel_width_, panel_height_);
        return false;
    });
}

void ClipboardWindow::close_window() {
    g_debug("clipboard window: close");
    set_visible(false);
}

// clipboard.position → where the panel sits inside the (bar-less) output
void ClipboardWindow::apply_position() {
    using P = Config::Clipboard::Position;
    const auto position = Config::get().clipboard().position;
    auto halign = Gtk::Align::CENTER;
    auto valign = Gtk::Align::CENTER;
    switch (position) {
    case P::TopLeft: halign = Gtk::Align::START; valign = Gtk::Align::START; break;
    case P::Top: valign = Gtk::Align::START; break;
    case P::TopRight: halign = Gtk::Align::END; valign = Gtk::Align::START; break;
    case P::BottomLeft: halign = Gtk::Align::START; valign = Gtk::Align::END; break;
    case P::Bottom: valign = Gtk::Align::END; break;
    case P::BottomRight: halign = Gtk::Align::END; valign = Gtk::Align::END; break;
    case P::Center: break;
    }
    panel_.set_halign(halign);
    panel_.set_valign(valign);
    panel_.set_margin_start(halign == Gtk::Align::START ? kEdgeMargin : 0);
    panel_.set_margin_end(halign == Gtk::Align::END ? kEdgeMargin : 0);
    panel_.set_margin_top(valign == Gtk::Align::START ? kEdgeMargin : 0);
    panel_.set_margin_bottom(valign == Gtk::Align::END ? kEdgeMargin : 0);
    panel_.set_size_request(panel_width_, panel_height_);
}

// -- results ------------------------------------------------------------------

void ClipboardWindow::update_results() {
    // keep the selection on the same entry across refreshes
    std::string selected_id;
    if (selected_ >= 0 && selected_ < static_cast<int>(rows_.size()) && !rows_[selected_].message)
        selected_id = rows_[selected_].item.id;

    rows_.clear();
    auto& clipboard = Clipboard::get();
    const auto& cfg = Config::get().clipboard();
    const std::string query = lowercase(trimmed(search_.get_text()));

    auto message = [this](const char* name, const char* description, const char* glyph) {
        Row r;
        r.message = true;
        r.name = name;
        r.description = description;
        r.glyph = glyph;
        rows_.push_back(std::move(r));
    };

    if (!clipboard.enabled()) {
        message("Clipboard history is disabled",
                clipboard.available()
                    ? "Turn it on in Settings › Clipboard."
                    : "Install cliphist and wl-clipboard, then turn it on in Settings › Clipboard.",
                kIconClipboard);
    } else {
        std::size_t total = 0;
        for (const auto& item : clipboard.items()) {
            if (item.is_image && !cfg.show_images)
                continue;
            ++total;
            if (!query.empty() && lowercase(item.preview).find(query) == std::string::npos)
                continue;
            Row r;
            r.item = item;
            r.glyph = kind_glyph(item.kind);
            if (item.is_image) {
                // Noctalia's formatImageEntry
                r.name = item.width > 0 ? "Image " + std::to_string(item.width) + "×" +
                                              std::to_string(item.height)
                                        : "Image";
                r.description = !item.format.empty() ? item.format + " • " + item.size_text
                                                     : item.mime;
            } else {
                // Noctalia's formatTextEntry: first non-empty line, then the
                // second line or a character/word count
                std::vector<std::string> lines;
                std::size_t start = 0;
                const std::string& p = item.preview;
                while (start <= p.size()) {
                    auto end = p.find('\n', start);
                    if (end == std::string::npos)
                        end = p.size();
                    const std::string line = trimmed(p.substr(start, end - start));
                    if (!line.empty())
                        lines.push_back(line);
                    start = end + 1;
                }
                r.name = lines.empty() ? "Empty text" : clipped(lines[0], 60);
                if (lines.size() > 1) {
                    r.description = clipped(lines[1], 80);
                } else if (Glib::ustring(p).size() >= 100) {
                    r.description = "Long text";
                } else {
                    const std::string t = trimmed(p);
                    std::size_t words = 0;
                    bool in_word = false;
                    for (unsigned char c : t) {
                        if (std::isspace(c)) {
                            in_word = false;
                        } else if (!in_word) {
                            in_word = true;
                            ++words;
                        }
                    }
                    r.description = std::to_string(Glib::ustring(t).size()) + " characters, " +
                                    std::to_string(words) + (words == 1 ? " word" : " words");
                }
            }
            rows_.push_back(std::move(r));
        }
        if (rows_.empty()) {
            if (total == 0 && clipboard.loading())
                message("Loading clipboard history...", "", kIconClipboard);
            else if (total == 0)
                message("Clipboard is empty", "Copy something to see it here.", kIconClipboard);
            else
                message("No matching items",
                        ("No entries containing \"" + trimmed(search_.get_text()) + "\"").c_str(),
                        kIconClipboard);
        }
    }

    selected_ = 0;
    if (!selected_id.empty())
        for (std::size_t i = 0; i < rows_.size(); ++i)
            if (!rows_[i].message && rows_[i].item.id == selected_id)
                selected_ = static_cast<int>(i);
    rebuild_rows();

    const std::size_t count = std::count_if(rows_.begin(), rows_.end(),
                                            [](const Row& r) { return !r.message; });
    count_label_.set_text(count == 0 ? "" : std::to_string(count) + (count == 1 ? " item" : " items"));
    clear_button_.set_visible(clipboard.enabled() && !clipboard.items().empty());
}

// icon slot: thumbnail for images (decoded async, photo glyph meanwhile),
// colour swatch for "#rrggbb" entries, a tabler glyph otherwise
Gtk::Widget* ClipboardWindow::make_icon(const Row& row, std::size_t index) {
    if (!row.message && row.item.is_image) {
        // A fixed 64x36 slot: the Picture is an *unmeasured, clipped* overlay
        // child, so a 1920x49 banner cannot widen the row to its natural
        // width (a plain Picture in the box did exactly that); COVER fills
        // the slot. The photo glyph underneath shows until the decode lands.
        auto* overlay = Gtk::make_managed<Gtk::Overlay>();
        overlay->set_valign(Gtk::Align::CENTER);
        auto* glyph = Gtk::make_managed<Gtk::Label>(kIconPhoto);
        glyph->add_css_class("launcher-glyph");
        glyph->set_size_request(kThumbWidth, kIconSize);
        overlay->set_child(*glyph);
        auto* picture = Gtk::make_managed<Gtk::Picture>();
        picture->set_content_fit(Gtk::ContentFit::COVER);
        picture->set_can_shrink(true);
        picture->add_css_class("clipboard-thumb");
        picture->set_overflow(Gtk::Overflow::HIDDEN);
        picture->set_visible(false);
        overlay->add_overlay(*picture);
        overlay->set_measure_overlay(*picture, false);
        overlay->set_clip_overlay(*picture, true);
        const unsigned serial = rebuild_serial_;
        Clipboard::get().thumbnail(
            row.item, 128,
            [this, alive = alive_, serial, index, glyph, picture](Glib::RefPtr<Gdk::Texture> texture) {
                if (!*alive || serial != rebuild_serial_ || !texture ||
                    index >= row_widgets_.size())
                    return; // the list was rebuilt meanwhile — widgets are gone
                picture->set_paintable(texture);
                picture->set_visible(true);
                glyph->set_opacity(0.0);
            });
        return overlay;
    }
    if (!row.message && row.item.kind == Clipboard::Item::Kind::Color) {
        Gdk::RGBA color;
        if (color.set(trimmed(row.item.preview))) {
            auto* swatch = Gtk::make_managed<Gtk::DrawingArea>();
            swatch->set_content_width(kIconSize);
            swatch->set_content_height(kIconSize);
            swatch->set_valign(Gtk::Align::CENTER);
            swatch->add_css_class("clipboard-swatch");
            swatch->set_draw_func([color](const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
                const double r = 8.0;
                cr->begin_new_sub_path();
                cr->arc(w - r, r, r, -M_PI / 2, 0);
                cr->arc(w - r, h - r, r, 0, M_PI / 2);
                cr->arc(r, h - r, r, M_PI / 2, M_PI);
                cr->arc(r, r, r, M_PI, 3 * M_PI / 2);
                cr->close_path();
                cr->set_source_rgba(color.get_red(), color.get_green(), color.get_blue(),
                                    color.get_alpha());
                cr->fill_preserve();
                cr->set_source_rgba(1, 1, 1, 0.35);
                cr->set_line_width(2);
                cr->stroke();
            });
            return swatch;
        }
    }
    auto* glyph = Gtk::make_managed<Gtk::Label>(row.glyph);
    glyph->add_css_class("launcher-glyph");
    glyph->set_size_request(kIconSize, kIconSize);
    return glyph;
}

void ClipboardWindow::rebuild_rows() {
    ++rebuild_serial_;
    Clipboard::get().cancel_thumbnail_requests(); // the old rows are gone
    while (auto* child = list_.get_first_child())
        list_.remove(*child);
    row_widgets_.clear();
    delete_buttons_.clear();

    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const auto& result = rows_[i];
        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 9);
        row->add_css_class("launcher-row");
        row_widgets_.push_back(row); // before make_icon: its callback checks the index
        row->append(*make_icon(result, i));

        auto* text = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
        text->set_hexpand(true);
        text->set_valign(Gtk::Align::CENTER);
        auto* name = Gtk::make_managed<Gtk::Label>(result.name);
        name->add_css_class("launcher-name");
        name->set_halign(Gtk::Align::START);
        name->set_ellipsize(Pango::EllipsizeMode::END);
        name->set_single_line_mode(true);
        text->append(*name);
        if (!result.description.empty()) {
            auto* desc = Gtk::make_managed<Gtk::Label>(result.description);
            desc->add_css_class("launcher-desc");
            desc->set_halign(Gtk::Align::START);
            desc->set_ellipsize(Pango::EllipsizeMode::END);
            desc->set_single_line_mode(true);
            text->append(*desc);
        }
        row->append(*text);

        Gtk::Button* remove = nullptr;
        if (!result.message) {
            remove = Gtk::make_managed<Gtk::Button>();
            auto* trash = Gtk::make_managed<Gtk::Label>(kIconTrash);
            trash->add_css_class("launcher-glyph");
            remove->set_child(*trash);
            remove->add_css_class("clipboard-delete");
            remove->set_has_frame(false);
            remove->set_valign(Gtk::Align::CENTER);
            remove->set_tooltip_text("Delete this entry");
            remove->set_visible(false); // shown on the selected row only
            const int index = static_cast<int>(i);
            remove->signal_clicked().connect([this, index] { delete_index(index); });
            row->append(*remove);
        }
        delete_buttons_.push_back(remove);

        const int index = static_cast<int>(i);
        auto click = Gtk::GestureClick::create();
        click->signal_released().connect([this, index, row, remove](int, double x, double y) {
            // a click on the trash button is the button's, not the row's
            if (remove != nullptr && remove->get_visible()) {
                if (auto bounds = remove->compute_bounds(*row))
                    if (bounds->contains_point(Gdk::Graphene::Point(static_cast<float>(x),
                                                                   static_cast<float>(y))))
                        return;
            }
            activate_index(index);
        });
        row->add_controller(click);

        auto hover = Gtk::EventControllerMotion::create();
        hover->signal_enter().connect([this, index](double, double) {
            if (mouse_active_)
                select(index, /*scroll_into_view=*/false);
        });
        row->add_controller(hover);

        list_.append(*row);
    }

    if (!row_widgets_.empty())
        select(selected_, /*scroll_into_view=*/true);
}

void ClipboardWindow::select(int index, bool scroll_into_view) {
    if (row_widgets_.empty())
        return;
    index = std::clamp(index, 0, static_cast<int>(row_widgets_.size()) - 1);
    const auto old = static_cast<std::size_t>(selected_);
    if (old < row_widgets_.size()) {
        row_widgets_[old]->remove_css_class("selected");
        if (delete_buttons_[old] != nullptr)
            delete_buttons_[old]->set_visible(false);
    }
    selected_ = index;
    auto* row = row_widgets_[static_cast<std::size_t>(index)];
    row->add_css_class("selected");
    if (delete_buttons_[static_cast<std::size_t>(index)] != nullptr)
        delete_buttons_[static_cast<std::size_t>(index)]->set_visible(true);

    if (!scroll_into_view)
        return;
    graphene_rect_t bounds;
    if (!gtk_widget_compute_bounds(row->gobj(), GTK_WIDGET(list_.gobj()), &bounds))
        return;
    auto adjustment = scroller_.get_vadjustment();
    const double top = bounds.origin.y;
    const double bottom = top + bounds.size.height;
    if (top < adjustment->get_value())
        adjustment->set_value(top);
    else if (bottom > adjustment->get_value() + adjustment->get_page_size())
        adjustment->set_value(bottom - adjustment->get_page_size());
}

// Enter / click: copy the entry — or paste it into the window that regains
// focus once this one is gone (Noctalia's autoPasteClipboard). Close first,
// then act from the main loop like the launcher.
void ClipboardWindow::activate_index(int index) {
    if (index < 0 || index >= static_cast<int>(rows_.size()) || rows_[index].message)
        return;
    const Clipboard::Item item = rows_[index].item;
    close_window();
    auto& clipboard = Clipboard::get();
    if (Config::get().clipboard().paste_on_click && clipboard.paste_available()) {
        Glib::signal_timeout().connect_once([item] { Clipboard::get().paste(item); },
                                            kPasteDelayMs);
    } else {
        Glib::signal_idle().connect_once([item] { Clipboard::get().copy(item); });
    }
}

void ClipboardWindow::delete_index(int index) {
    if (index < 0 || index >= static_cast<int>(rows_.size()) || rows_[index].message)
        return;
    Clipboard::get().remove(rows_[index].item.id); // emits changed → update_results
}

bool ClipboardWindow::on_key_pressed(guint keyval, guint, Gdk::ModifierType) {
    const int count = static_cast<int>(rows_.size());
    auto wrap = [count](int index) { return ((index % count) + count) % count; };

    switch (keyval) {
    case GDK_KEY_Escape:
        close_window();
        return true;
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:
        activate_index(selected_);
        return true;
    case GDK_KEY_Delete:
        // the entry has focus, but its text is a search query: Delete with
        // nothing typed removes the selected clipboard entry
        if (search_.get_text().empty()) {
            delete_index(selected_);
            return true;
        }
        return false;
    case GDK_KEY_Down:
    case GDK_KEY_Tab:
        if (count > 0)
            select(wrap(selected_ + 1), true);
        return true;
    case GDK_KEY_Up:
    case GDK_KEY_ISO_Left_Tab:
        if (count > 0)
            select(wrap(selected_ - 1), true);
        return true;
    case GDK_KEY_Home:
        select(0, true);
        return true;
    case GDK_KEY_End:
        select(count - 1, true);
        return true;
    case GDK_KEY_Page_Down:
        select(std::min(selected_ + kPageJump, count - 1), true);
        return true;
    case GDK_KEY_Page_Up:
        select(std::max(selected_ - kPageJump, 0), true);
        return true;
    default:
        return false; // the search entry takes it
    }
}

} // namespace hyprshell
