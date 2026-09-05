#include "bar/control_center_panel.hpp"
#include "services/theme.hpp"

#include "services/brightness.hpp"
#include "services/config.hpp"
#include "services/mpris.hpp"
#include "services/pulse.hpp"
#include "bar/avatar.hpp"

#include "services/session.hpp"
#include "services/system_stats.hpp"
#include "services/user_info.hpp"

#include <fstream>

#include <gdk-pixbuf/gdk-pixbuf.h>

#include <algorithm>
#include <cmath>

namespace hyprshell {

namespace {

// noctalia-tabler-icons glyphs (\u escapes — never literal PUA)
constexpr const char* kVolume = "";
constexpr const char* kVolumeOff = "";
constexpr const char* kMic = "";
constexpr const char* kMicOff = "";
constexpr const char* kSunOff = "";
constexpr const char* kBrightnessLow = "ﬣ";
constexpr const char* kBrightnessHigh = "ﬤ";
constexpr const char* kSkipBack = "";
constexpr const char* kPlay = "";
constexpr const char* kPause = "";
constexpr const char* kSkipForward = "";
constexpr const char* kDisc = "\U0001003E";
constexpr const char* kCaretDown = "שׁ";
constexpr const char* kCpuUsage = "勺";
constexpr const char* kFlame = "";
constexpr const char* kMemory = "";
constexpr const char* kStorage = "";

// Noctalia colour snapshot (the user's colors.json, like the other panels)
// palette lookups (theme-dependent, so functions rather than constants)
Gdk::RGBA kPrimary() { return Theme::get().rgba("mPrimary"); }
Gdk::RGBA kTertiary() { return Theme::get().rgba("mTertiary"); } // warning
Gdk::RGBA kError() { return Theme::get().rgba("mError"); }       // critical
Gdk::RGBA kSurface() { return Theme::get().rgba("mSurface"); }

constexpr const char* kUser = "\uEB4D";
constexpr const char* kSettings = "\uEB20";
constexpr const char* kPower = "\uEB0D";

constexpr int kCardProfile = 64;

// Noctalia's Time.formatVagueHumanReadableDuration: "1d 2h 3m", seconds only
// when there are neither hours nor minutes
std::string vague_duration(long total_seconds) {
    if (total_seconds < 0)
        return "0s";
    const long days = total_seconds / 86400, hours = (total_seconds % 86400) / 3600,
               minutes = (total_seconds % 3600) / 60, seconds = total_seconds % 60;
    std::string out;
    const auto add = [&out](long v, const char* unit) {
        if (!out.empty())
            out += ' ';
        out += std::to_string(v) + unit;
    };
    if (days)
        add(days, "d");
    if (hours)
        add(hours, "h");
    if (minutes)
        add(minutes, "m");
    if (!hours && !minutes)
        add(seconds, "s");
    return out;
}

constexpr int kCardAudio = 60, kCardBrightness = 60, kCardMedia = 220, kCardSysmon = 84;
constexpr double kPi = 3.14159265358979323846;

double ease_out_cubic(double t) { return 1 - std::pow(1 - t, 3); }

Gdk::RGBA stat_color(double value, double warning, double critical) {
    if (value >= critical)
        return kError();
    if (value >= warning)
        return kTertiary();
    return kPrimary();
}

void style_round_button(Gtk::Button& button, Gtk::Label& icon, const char* css) {
    icon.add_css_class("cc-btn-icon");
    button.set_child(icon);
    button.add_css_class(css);
    button.set_has_frame(false);
    button.set_valign(Gtk::Align::CENTER);
}

} // namespace

// ---------------------------------------------------------------------------
// CircleStat

CircleStat::CircleStat(const char* glyph, const char* suffix) : glyph_(glyph), suffix_(suffix), color_(kPrimary()) {
    set_content_width(57);
    set_content_height(47);
    set_halign(Gtk::Align::CENTER);
    set_valign(Gtk::Align::CENTER);
    set_draw_func(sigc::mem_fun(*this, &CircleStat::draw));
}

void CircleStat::set_ratio(double ratio, const Gdk::RGBA& color) {
    ratio = std::clamp(ratio, 0.0, 1.0);
    color_ = color;
    if (std::abs(ratio - ratio_) < 0.0005) {
        queue_draw();
        return;
    }
    ratio_ = ratio;
    from_ = animated_;
    start_us_ = 0;
    if (!animating_) {
        animating_ = true;
        add_tick_callback([this](const Glib::RefPtr<Gdk::FrameClock>& clock) {
            const gint64 now = clock->get_frame_time();
            if (start_us_ == 0)
                start_us_ = now;
            const double t = std::clamp((now - start_us_) / 300000.0, 0.0, 1.0); // animationNormal
            animated_ = from_ + (ratio_ - from_) * ease_out_cubic(t);
            queue_draw();
            if (t < 1.0)
                return true;
            animating_ = false;
            return false;
        });
    }
}

void CircleStat::draw(const Cairo::RefPtr<Cairo::Context>& cr, int w, int) {
    // Noctalia's NCircleStat at contentScale 0.95: gauge 57, line 5.7, radius 23.75
    const double cx = w / 2.0, cy = 57 / 2.0, radius = 23.75, line = 5.7;
    const double start = 150.0 * kPi / 180.0, sweep = 240.0 * kPi / 180.0;
    cr->set_line_width(line);
    cr->set_line_cap(Cairo::Context::LineCap::ROUND);
    const Gdk::RGBA surface = kSurface();
    cr->set_source_rgba(surface.get_red(), surface.get_green(), surface.get_blue(), 1.0);
    cr->arc(cx, cy, radius, start, start + sweep);
    cr->stroke();
    if (animated_ > 0.005) {
        cr->set_source_rgba(color_.get_red(), color_.get_green(), color_.get_blue(), 1.0);
        cr->arc(cx, cy, radius, start, start + sweep * animated_);
        cr->stroke();
    }
    // value, bold 9.4pt, centered 3.8px above the middle
    auto layout = create_pango_layout(std::to_string(static_cast<int>(std::round(animated_ * 100))) + suffix_);
    Pango::FontDescription font(Theme::get().font() + " Bold 9.4");
    layout->set_font_description(font);
    int tw = 0, th = 0;
    layout->get_pixel_size(tw, th);
    cr->set_source_rgba(color_.get_red(), color_.get_green(), color_.get_blue(), 1.0);
    cr->move_to(cx - tw / 2.0, cy - 3.8 - th / 2.0);
    layout->show_in_cairo_context(cr);
    // icon below the value, in the arc's bottom gap
    auto icon = create_pango_layout(glyph_);
    Pango::FontDescription icon_font("noctalia-tabler-icons 10.45");
    icon->set_font_description(icon_font);
    int iw = 0, ih = 0;
    icon->get_pixel_size(iw, ih);
    cr->move_to(cx - iw / 2.0, cy - 3.8 + th / 2.0 + 3.8);
    icon->show_in_cairo_context(cr);
}

// ---------------------------------------------------------------------------
// MediaBackground

MediaBackground::MediaBackground() {
    set_can_target(false);
    set_overflow(Gtk::Overflow::HIDDEN);
}

void MediaBackground::set_art_url(const std::string& url) {
    if (url == url_)
        return;
    url_ = url;
    texture_.reset();
    if (cancellable_)
        cancellable_->cancel();
    queue_draw();
    if (url.empty())
        return;
    cancellable_ = Gio::Cancellable::create();
    auto cancellable = cancellable_;
    auto file = Gio::File::create_for_uri(url);
    file->read_async(
        [this, file, url, cancellable, alive = alive_](Glib::RefPtr<Gio::AsyncResult>& result) {
            if (!*alive || cancellable->is_cancelled())
                return;
            Glib::RefPtr<Gio::FileInputStream> stream;
            try {
                stream = file->read_finish(result);
            } catch (const Glib::Error& e) {
                g_message("control center: album art %s: %s", url.c_str(), e.what());
                return;
            }
            struct Pending {
                std::shared_ptr<bool> alive;
                MediaBackground* self;
                std::string url;
            };
            auto* pending = new Pending{alive, this, url};
            // Noctalia decodes at 256px; cover-scaled at draw time
            gdk_pixbuf_new_from_stream_at_scale_async(
                G_INPUT_STREAM(stream->gobj()), 256, 256, TRUE, cancellable->gobj(),
                [](GObject*, GAsyncResult* res, gpointer data) {
                    std::unique_ptr<Pending> p(static_cast<Pending*>(data));
                    GdkPixbuf* pixbuf = gdk_pixbuf_new_from_stream_finish(res, nullptr);
                    if (!*p->alive || pixbuf == nullptr || p->self->url_ != p->url) {
                        g_clear_object(&pixbuf);
                        return;
                    }
                    auto wrapped = Glib::wrap(pixbuf, /*take_copy=*/true);
                    p->self->texture_ = Gdk::Texture::create_for_pixbuf(wrapped);
                    g_object_unref(pixbuf);
                    p->self->queue_draw();
                },
                pending);
        },
        cancellable);
}

void MediaBackground::snapshot_vfunc(const Glib::RefPtr<Gtk::Snapshot>& snapshot) {
    const float w = get_width(), h = get_height();
    if (w <= 0 || h <= 0)
        return;
    auto* gs = snapshot->gobj();
    const graphene_rect_t full = GRAPHENE_RECT_INIT(0, 0, w, h);
    GskRoundedRect rounded;
    gsk_rounded_rect_init_from_rect(&rounded, &full, 16);
    gtk_snapshot_push_rounded_clip(gs, &rounded);
    const GdkRGBA surface{0x13 / 255.0f, 0x13 / 255.0f, 0x16 / 255.0f, 1.0f};
    gtk_snapshot_append_color(gs, &surface, &full);
    if (texture_) {
        // Noctalia: MultiEffect blurMax 8 × blur 0.33
        gtk_snapshot_push_blur(gs, 8.0 * 0.33);
        const double tw = texture_->get_width(), th = texture_->get_height();
        const double f = std::max(w / tw, h / th);
        const graphene_rect_t rect = GRAPHENE_RECT_INIT(
            static_cast<float>((w - tw * f) / 2), static_cast<float>((h - th * f) / 2),
            static_cast<float>(tw * f), static_cast<float>(th * f));
        gtk_snapshot_append_scaled_texture(gs, texture_->gobj(), GSK_SCALING_FILTER_TRILINEAR, &rect);
        gtk_snapshot_pop(gs);
        const GdkRGBA scrim{0x13 / 255.0f, 0x13 / 255.0f, 0x16 / 255.0f, 0.65f};
        gtk_snapshot_append_color(gs, &scrim, &full);
    }
    gtk_snapshot_pop(gs);
}

// ---------------------------------------------------------------------------
// panel

ControlCenterPanel::ControlCenterPanel() : Gtk::Box(Gtk::Orientation::VERTICAL, 13) {
    add_css_class("control-center");
    set_size_request(440, -1);
    set_margin(13);

    build_profile();
    build_audio();
    build_brightness();
    build_media();
    build_sysmon();

    Pulse::get().signal_changed().connect(sigc::mem_fun(*this, &ControlCenterPanel::update_audio));
    Brightness::get().signal_changed().connect(sigc::mem_fun(*this, &ControlCenterPanel::update_brightness));
    Mpris::get().signal_changed().connect(sigc::mem_fun(*this, &ControlCenterPanel::update_media));
    SystemStats::get().signal_changed().connect(sigc::mem_fun(*this, &ControlCenterPanel::update_sysmon));
    Config::get().signal_changed().connect(sigc::mem_fun(*this, &ControlCenterPanel::apply_config));
    apply_config();
    update_audio();
    update_brightness();
    update_media();
}

ControlCenterPanel::~ControlCenterPanel() {
    if (stats_registered_)
        SystemStats::get().unregister_consumer();
    player_menu_.unparent();
}

void ControlCenterPanel::set_open(bool open) {
    open_ = open;
    const bool want_stats = open && Config::get().control_center().show_sysmon;
    if (want_stats && !stats_registered_) {
        SystemStats::get().register_consumer();
        stats_registered_ = true;
    } else if (!want_stats && stats_registered_) {
        SystemStats::get().unregister_consumer();
        stats_registered_ = false;
    }
    uptime_timer_.disconnect();
    if (open) {
        Brightness::get().refresh();
        update_uptime();
        uptime_timer_ = Glib::signal_timeout().connect_seconds(
            [this] {
                update_uptime();
                return true;
            },
            60); // Noctalia refreshes the uptime every minute
        update_audio();
        update_media();
        update_sysmon();
    }
}

// -- profile card (Noctalia's ProfileCard): avatar, name + uptime, settings
// and power buttons (its close button dropped per user) ------------------------

void ControlCenterPanel::build_profile() {
    profile_card_.add_css_class("cc-card");
    profile_card_.set_size_request(-1, kCardProfile);
    // 41px avatar ringed in mPrimary
    avatar_ring_.add_css_class("cc-avatar-ring");
    avatar_ring_.set_size_request(41, 41);
    avatar_ring_.set_valign(Gtk::Align::CENTER);
    avatar_ring_.set_halign(Gtk::Align::CENTER);
    if (auto texture = load_avatar_texture(user_avatar_path(), 37)) {
        avatar_.set_paintable(texture);
        avatar_.set_content_fit(Gtk::ContentFit::COVER);
        avatar_.set_can_shrink(true);
        avatar_.set_size_request(37, 37);
        avatar_.add_css_class("cc-avatar");
        avatar_.set_overflow(Gtk::Overflow::HIDDEN);
        avatar_ring_.append(avatar_);
    } else {
        avatar_fallback_.set_text(kUser);
        avatar_fallback_.add_css_class("cc-avatar-fallback");
        avatar_fallback_.set_size_request(37, 37);
        avatar_ring_.append(avatar_fallback_);
    }
    profile_card_.append(avatar_ring_);

    auto* texts = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
    texts->set_valign(Gtk::Align::CENTER);
    texts->set_hexpand(true);
    name_.set_text(user_display_name());
    name_.add_css_class("cc-name");
    name_.set_xalign(0.0f);
    name_.set_ellipsize(Pango::EllipsizeMode::END);
    texts->append(name_);
    uptime_.add_css_class("cc-uptime");
    uptime_.set_xalign(0.0f);
    texts->append(uptime_);
    profile_card_.append(*texts);

    auto* buttons = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    buttons->set_valign(Gtk::Align::CENTER);
    style_round_button(settings_button_, settings_icon_, "cc-media-btn");
    settings_icon_.set_text(kSettings);
    settings_button_.set_tooltip_text("Settings");
    settings_button_.signal_clicked().connect([this] {
        request_close_.emit();
        open_settings();
    });
    buttons->append(settings_button_);
    style_round_button(power_button_, power_icon_, "cc-media-btn");
    power_icon_.set_text(kPower);
    power_button_.set_tooltip_text("Session menu");
    power_button_.signal_clicked().connect([this] {
        // Noctalia opens the session menu and closes the control center
        request_close_.emit();
        Glib::signal_idle().connect_once([] {
            if (auto app = Gio::Application::get_default())
                app->activate_action("session");
        });
    });
    buttons->append(power_button_);
    profile_card_.append(*buttons);
    append(profile_card_);
}

void ControlCenterPanel::update_uptime() {
    std::ifstream in("/proc/uptime");
    double seconds = 0;
    if (in >> seconds)
        uptime_.set_text("Uptime: " + vague_duration(static_cast<long>(seconds)));
}

void ControlCenterPanel::apply_config() {
    const auto& cfg = Config::get().control_center();
    audio_card_.set_visible(cfg.show_audio);
    brightness_card_.set_visible(cfg.show_brightness && Brightness::get().available());
    media_card_.set_visible(cfg.show_media);
    sysmon_card_.set_visible(cfg.show_sysmon);
    if (open_)
        set_open(true); // re-evaluates the stats registration
}

// -- audio card (Noctalia's AudioCard): output | input, each a small mute
// button + device name over a slider -----------------------------------------

void ControlCenterPanel::build_audio() {
    audio_card_.add_css_class("cc-card");
    audio_card_.set_size_request(-1, kCardAudio);
    audio_card_.set_homogeneous(true);
    for (auto* col : {&output_, &input_}) {
        col->box.set_hexpand(true);
        col->box.set_valign(Gtk::Align::CENTER);
        auto* header = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
        style_round_button(col->mute, col->mute_icon, "cc-mute-btn");
        header->append(col->mute);
        col->device.add_css_class("cc-device");
        col->device.set_xalign(0.0f);
        col->device.set_hexpand(true);
        col->device.set_ellipsize(Pango::EllipsizeMode::END);
        header->append(col->device);
        col->box.append(*header);
        col->scale.set_range(0, 100);
        col->scale.set_increments(1, 5);
        col->scale.set_draw_value(false);
        col->scale.add_css_class("cc-slider");
        col->box.append(col->scale);
        audio_card_.append(col->box);
    }
    output_.scale.signal_value_changed().connect([this] {
        if (updating_)
            return;
        Pulse::get().set_volume(output_.scale.get_value() / 100.0);
        output_.scale.set_tooltip_text(std::to_string(static_cast<int>(std::round(output_.scale.get_value()))) + "%");
    });
    input_.scale.signal_value_changed().connect([this] {
        if (updating_)
            return;
        Pulse::get().set_input_volume(input_.scale.get_value() / 100.0);
        input_.scale.set_tooltip_text(std::to_string(static_cast<int>(std::round(input_.scale.get_value()))) + "%");
    });
    output_.mute.signal_clicked().connect([] { Pulse::get().set_muted(!Pulse::get().muted()); });
    input_.mute.signal_clicked().connect([] { Pulse::get().set_input_muted(!Pulse::get().input_muted()); });
    // wheel: Noctalia's volumeStep (5%)
    for (auto* col : {&output_, &input_}) {
        auto scroll = Gtk::EventControllerScroll::create();
        scroll->set_flags(Gtk::EventControllerScroll::Flags::VERTICAL | Gtk::EventControllerScroll::Flags::DISCRETE);
        scroll->signal_scroll().connect(
            [col](double, double dy) {
                col->scale.set_value(std::clamp(col->scale.get_value() - dy * 5.0, 0.0, 100.0));
                return true;
            },
            false);
        col->box.add_controller(scroll);
    }
    append(audio_card_);
}

void ControlCenterPanel::update_audio() {
    auto& pulse = Pulse::get();
    updating_ = true;
    output_.box.set_opacity(pulse.available() ? 1.0 : 0.5);
    output_.box.set_sensitive(pulse.available());
    output_.device.set_text(pulse.available() ? pulse.description() : "No output device");
    output_.mute_icon.set_text(pulse.muted() ? kVolumeOff : kVolume);
    if (pulse.muted())
        output_.mute_icon.add_css_class("muted");
    else
        output_.mute_icon.remove_css_class("muted");
    output_.scale.set_value(std::round(pulse.volume() * 100.0));
    output_.scale.set_tooltip_text(std::to_string(static_cast<int>(std::round(pulse.volume() * 100.0))) + "%");

    input_.box.set_opacity(pulse.input_available() ? 1.0 : 0.5);
    input_.box.set_sensitive(pulse.input_available());
    input_.device.set_text(pulse.input_available() ? pulse.input_description() : "No input device");
    input_.mute_icon.set_text(pulse.input_muted() ? kMicOff : kMic);
    if (pulse.input_muted())
        input_.mute_icon.add_css_class("muted");
    else
        input_.mute_icon.remove_css_class("muted");
    input_.scale.set_value(std::round(pulse.input_volume() * 100.0));
    input_.scale.set_tooltip_text(std::to_string(static_cast<int>(std::round(pulse.input_volume() * 100.0))) + "%");
    updating_ = false;
}

// -- brightness card (Noctalia's BrightnessCard) ------------------------------

void ControlCenterPanel::build_brightness() {
    brightness_card_.add_css_class("cc-card");
    brightness_card_.set_size_request(-1, kCardBrightness);
    brightness_card_.set_valign(Gtk::Align::CENTER);
    auto* header = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    brightness_icon_.add_css_class("cc-brightness-icon");
    header->append(brightness_icon_);
    brightness_title_.set_text("Brightness");
    brightness_title_.add_css_class("cc-device");
    header->append(brightness_title_);
    brightness_percent_.add_css_class("cc-device");
    header->append(brightness_percent_);
    brightness_card_.append(*header);
    brightness_scale_.set_range(0, 100);
    brightness_scale_.set_increments(1, 5);
    brightness_scale_.set_draw_value(false);
    brightness_scale_.add_css_class("cc-slider");
    brightness_scale_.signal_value_changed().connect([this] {
        const int pct = static_cast<int>(std::round(brightness_scale_.get_value()));
        brightness_percent_.set_text(std::to_string(pct) + "%");
        brightness_scale_.set_tooltip_text(std::to_string(pct) + "%");
        if (updating_)
            return;
        // Noctalia: 100ms debounce before writing
        brightness_debounce_.disconnect();
        brightness_debounce_ = Glib::signal_timeout().connect(
            [this] {
                Brightness::get().set_fraction(brightness_scale_.get_value() / 100.0);
                return false;
            },
            100);
    });
    auto scroll = Gtk::EventControllerScroll::create();
    scroll->set_flags(Gtk::EventControllerScroll::Flags::VERTICAL | Gtk::EventControllerScroll::Flags::DISCRETE);
    scroll->signal_scroll().connect(
        [this](double, double dy) {
            brightness_scale_.set_value(std::clamp(brightness_scale_.get_value() - dy * 5.0, 0.0, 100.0));
            return true;
        },
        false);
    brightness_card_.add_controller(scroll);
    brightness_card_.append(brightness_scale_);
    append(brightness_card_);
}

void ControlCenterPanel::update_brightness() {
    auto& b = Brightness::get();
    updating_ = true;
    const double v = b.fraction();
    brightness_icon_.set_text(!b.available() ? kBrightnessLow : v <= 0.001 ? kSunOff : v <= 0.5 ? kBrightnessLow : kBrightnessHigh);
    brightness_scale_.set_value(std::round(v * 100.0));
    brightness_card_.set_visible(Config::get().control_center().show_brightness && b.available());
    updating_ = false;
}

// -- media card (Noctalia's MediaCard) ----------------------------------------

void ControlCenterPanel::build_media() {
    media_card_.add_css_class("cc-media");
    media_card_.set_size_request(-1, kCardMedia);
    media_card_.set_child(media_background_);

    media_content_.set_margin(9);
    // player selector (shown with more than one player)
    auto* selector_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    auto* caret = Gtk::make_managed<Gtk::Label>(kCaretDown);
    caret->add_css_class("cc-caret");
    selector_row->append(*caret);
    player_name_.add_css_class("cc-player-name");
    player_name_.set_xalign(0.0f);
    selector_row->append(player_name_);
    player_button_.set_child(*selector_row);
    player_button_.set_has_frame(false);
    player_button_.add_css_class("cc-player-btn");
    player_button_.set_halign(Gtk::Align::START);
    player_menu_.set_child(player_list_);
    player_menu_.set_parent(player_button_);
    player_menu_.set_has_arrow(false);
    player_menu_.add_css_class("session-popover");
    player_list_.add_css_class("session-list");
    player_button_.signal_clicked().connect([this] {
        while (auto* child = player_list_.get_first_child())
            player_list_.remove(*child);
        for (const auto& name : Mpris::get().player_names()) {
            const auto* p = Mpris::get().player(name);
            if (!p || !p->can_play)
                continue;
            auto* item = Gtk::make_managed<Gtk::Button>();
            auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
            auto* glyph = Gtk::make_managed<Gtk::Label>(kDisc);
            glyph->add_css_class("session-glyph");
            row->append(*glyph);
            auto* label = Gtk::make_managed<Gtk::Label>(p->identity);
            label->add_css_class("session-label");
            label->set_xalign(0.0f);
            row->append(*label);
            item->set_child(*row);
            item->add_css_class("session-item");
            item->set_has_frame(false);
            item->signal_clicked().connect([this, name] {
                player_menu_.popdown();
                Mpris::get().set_active(name);
            });
            player_list_.append(*item);
        }
        player_menu_.popup();
    });
    media_content_.append(player_button_);

    // empty state: the disc glyph, centered
    media_empty_.set_text(kDisc);
    media_empty_.add_css_class("cc-media-empty");
    media_empty_.set_vexpand(true);
    media_empty_.set_valign(Gtk::Align::CENTER);
    media_content_.append(media_empty_);

    // active state
    media_active_.set_vexpand(true);
    media_active_.set_valign(Gtk::Align::CENTER);
    title_.add_css_class("cc-title");
    title_.set_xalign(0.0f);
    title_.set_wrap(true);
    title_.set_lines(2);
    title_.set_ellipsize(Pango::EllipsizeMode::END);
    title_.set_max_width_chars(1);
    title_.set_hexpand(true);
    artist_.add_css_class("cc-artist");
    artist_.set_xalign(0.0f);
    artist_.set_ellipsize(Pango::EllipsizeMode::END);
    album_.add_css_class("cc-album");
    album_.set_xalign(0.0f);
    album_.set_ellipsize(Pango::EllipsizeMode::END);
    auto* meta = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    meta->append(title_);
    meta->append(artist_);
    meta->append(album_);
    media_active_.append(*meta);
    progress_.set_range(0, 1);
    progress_.set_increments(0.01, 0.1);
    progress_.set_draw_value(false);
    progress_.add_css_class("cc-slider");
    progress_.add_css_class("cc-progress");
    progress_.set_margin_top(6);
    progress_.signal_change_value().connect(
        [this](Gtk::ScrollType, double value) {
            // user seek: 75ms debounce while dragging, hold external updates
            seeking_ = true;
            seek_release_.disconnect();
            seek_release_ = Glib::signal_timeout().connect(
                [this] {
                    seeking_ = false;
                    update_media();
                    return false;
                },
                700);
            seek_debounce_.disconnect();
            seek_debounce_ = Glib::signal_timeout().connect(
                [this, value] {
                    if (const auto* p = Mpris::get().active(); p && p->length_us > 0 && p->can_seek)
                        Mpris::get().seek_to(p->bus_name, static_cast<gint64>(std::clamp(value, 0.0, 1.0) * p->length_us));
                    return false;
                },
                75);
            return false;
        },
        false);
    media_active_.append(progress_);
    auto* controls = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    controls->set_halign(Gtk::Align::CENTER);
    controls->set_margin_top(13);
    style_round_button(prev_, prev_icon_, "cc-media-btn");
    style_round_button(play_, play_icon_, "cc-media-btn");
    style_round_button(next_, next_icon_, "cc-media-btn");
    prev_icon_.set_text(kSkipBack);
    next_icon_.set_text(kSkipForward);
    prev_.signal_clicked().connect([] { if (const auto* p = Mpris::get().active()) Mpris::get().previous(p->bus_name); });
    play_.signal_clicked().connect([] { if (const auto* p = Mpris::get().active()) Mpris::get().play_pause(p->bus_name); });
    next_.signal_clicked().connect([] { if (const auto* p = Mpris::get().active()) Mpris::get().next(p->bus_name); });
    controls->append(prev_);
    controls->append(play_);
    controls->append(next_);
    media_active_.append(*controls);
    media_content_.append(media_active_);

    media_card_.add_overlay(media_content_);
    append(media_card_);
}

void ControlCenterPanel::update_media() {
    auto& mpris = Mpris::get();
    const auto* p = mpris.active();
    const bool active = p != nullptr && p->can_play;
    int players = 0;
    for (const auto& name : mpris.player_names())
        if (const auto* q = mpris.player(name); q && q->can_play)
            ++players;
    player_button_.set_visible(players > 1);
    player_name_.set_text(p ? p->identity : "");
    media_empty_.set_visible(!active);
    media_active_.set_visible(active);
    media_background_.set_art_url(active ? p->art_url : "");
    if (!active)
        return;
    std::string title = p->title;
    std::erase_if(title, [](char c) { return c == '\r' || c == '\n'; });
    title_.set_text(title);
    title_.set_visible(!title.empty());
    artist_.set_text(p->artist);
    artist_.set_visible(!p->artist.empty());
    album_.set_text(p->album);
    album_.set_visible(!p->album.empty());
    progress_.set_visible(p->length_us > 0);
    progress_.set_sensitive(p->length_us > 0 && p->can_seek);
    if (!seeking_ && p->length_us > 0) {
        updating_ = true;
        progress_.set_value(std::clamp(static_cast<double>(mpris.position(p->bus_name)) / p->length_us, 0.0, 1.0));
        updating_ = false;
    }
    play_icon_.set_text(p->playing() ? kPause : kPlay);
    prev_.set_visible(p->can_go_previous);
    play_.set_visible(p->can_play || p->can_pause);
    next_.set_visible(p->can_go_next);
}

// -- system monitor card (Noctalia's SystemMonitorCard) -----------------------

void ControlCenterPanel::build_sysmon() {
    sysmon_card_.add_css_class("cc-card");
    sysmon_card_.set_size_request(-1, kCardSysmon);
    sysmon_card_.set_homogeneous(true);
    cpu_stat_ = Gtk::make_managed<CircleStat>(kCpuUsage, "%");
    temp_stat_ = Gtk::make_managed<CircleStat>(kFlame, "°C");
    mem_stat_ = Gtk::make_managed<CircleStat>(kMemory, "%");
    disk_stat_ = Gtk::make_managed<CircleStat>(kStorage, "%");
    for (auto* stat : {cpu_stat_, temp_stat_, mem_stat_, disk_stat_}) {
        stat->set_hexpand(true);
        sysmon_card_.append(*stat);
    }
    append(sysmon_card_);
}

void ControlCenterPanel::update_sysmon() {
    auto& s = SystemStats::get();
    const double cpu = std::max(0.0, s.cpu_usage());
    cpu_stat_->set_ratio(cpu / 100.0, stat_color(cpu, 80, 90));
    cpu_stat_->set_tooltip_text("CPU usage: " + std::to_string(static_cast<int>(std::round(cpu))) + "%");
    // Noctalia normalises the temperature arc to a fixed 100 °C
    temp_stat_->set_ratio(s.cpu_temp() / 100.0, stat_color(s.cpu_temp(), 80, 90));
    temp_stat_->set_tooltip_text("CPU temp: " + std::to_string(s.cpu_temp()) + "°C");
    mem_stat_->set_ratio(s.mem_percent() / 100.0, stat_color(s.mem_percent(), 80, 90));
    mem_stat_->set_tooltip_text("Memory: " + std::to_string(s.mem_percent()) + "%");
    disk_stat_->set_ratio(s.disk_percent() / 100.0, stat_color(s.disk_percent(), 80, 90));
    disk_stat_->set_tooltip_text("Disk: " + std::to_string(s.disk_percent()) + "%\n" + s.disk_path());
}

} // namespace hyprshell
