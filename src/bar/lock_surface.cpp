#include "bar/lock_surface.hpp"

#include "bar/lock_screen.hpp"
#include "services/config.hpp"
#include "services/power_profiles.hpp"
#include "services/session.hpp"
#include "services/upower.hpp"

#include <gtk4-layer-shell.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace hyprshell {

namespace {

// Noctalia's Style constants used by LockScreenPanel
constexpr int kAnimationNormalMs = 300;   // Style.animationNormal (OutCubic)
constexpr int kStageShiftPx = 40;         // cover/login y offset while hidden
constexpr int kCountdownMs = 10000;       // general.lockScreenCountdownDuration
constexpr int kCountdownTickMs = 100;
constexpr int kBlinkMs = 530;
constexpr int kPillBottomMargin = 200;
constexpr int kCornerMargin = 28;
constexpr int kPowerButtonSize = 30;
constexpr int kSessionMenuWidth = 210;

// noctalia-tabler-icons glyphs (\u escapes, never literal PUA)
constexpr const char* kGlyphCircleKey = "\uF633";
constexpr const char* kGlyphAlertCircle = "\uEA05";
constexpr const char* kGlyphClock = "\uEA70";
constexpr const char* kGlyphClose = "\uEB55";
constexpr const char* kGlyphUser = "\uEB4D";
constexpr const char* kGlyphEye = "\uEA9A";
constexpr const char* kGlyphEyeOff = "\uECF0";
constexpr const char* kGlyphPower = "\uEB0D";
constexpr const char* kGlyphDot = "\uF671"; // circle-filled (per user: plain dots only)
constexpr int kAnimationFastMs = 150;        // Style.animationFast

// Segoe Fluent Icons battery glyphs (see bar/modules/battery.cpp)
constexpr const char* kBatteryLevels[] = {"\uE851", "\uE852", "\uE853", "\uE854", "\uE855",
                                          "\uE856", "\uE857", "\uE858", "\uE859", "\uE83F"};
constexpr const char* kBatteryChargingFrame = "\uE85A"; // BatteryCharging0 (bolt)
constexpr const char* kBatterySaverFrame = "\uE863";    // BatterySaver0 (leaf)

// the lock screen's session menu, in Noctalia's order
constexpr const char* kMenuKeys[] = {"suspend", "logout", "hibernate", "reboot", "shutdown"};

const SessionAction* find_action(const char* key) {
    for (const auto& action : kSessionActions)
        if (g_strcmp0(action.key, key) == 0)
            return &action;
    return nullptr;
}

double ease_out_cubic(double t) {
    const double u = 1.0 - t;
    return 1.0 - u * u * u;
}

Gtk::Label* glyph_label(const char* glyph, const char* css_class) {
    auto* label = Gtk::make_managed<Gtk::Label>(glyph);
    label->add_css_class("lock-glyph");
    if (css_class != nullptr)
        label->add_css_class(css_class);
    return label;
}

} // namespace

LockSurface::LockSurface(LockScreen& screen, const Glib::RefPtr<Gdk::Monitor>& monitor,
                         bool preview)
    : screen_(screen), monitor_(monitor), preview_(preview) {
    set_decorated(false);
    add_css_class("lock-surface");

    if (preview_) {
        // dev mode: an ordinary fullscreen overlay window, no session lock
        auto* window = GTK_WINDOW(gobj());
        gtk_layer_init_for_window(window);
        gtk_layer_set_namespace(window, "hypr-shell-lock-preview");
        gtk_layer_set_layer(window, GTK_LAYER_SHELL_LAYER_OVERLAY);
        gtk_layer_set_keyboard_mode(window, GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
        gtk_layer_set_exclusive_zone(window, -1);
        if (monitor_)
            gtk_layer_set_monitor(window, monitor_->gobj());
        for (auto edge : {GTK_LAYER_SHELL_EDGE_TOP, GTK_LAYER_SHELL_EDGE_BOTTOM,
                          GTK_LAYER_SHELL_EDGE_LEFT, GTK_LAYER_SHELL_EDGE_RIGHT})
            gtk_layer_set_anchor(window, edge, true);
    }

    // background: wallpaper, then Noctalia's shadow gradient
    root_.set_child(background_);
    gradient_.add_css_class("lock-gradient");
    gradient_.set_can_target(false);
    root_.add_overlay(gradient_);

    // hidden password input: focused, invisible, 1x1 in the top-left corner
    input_.set_visibility(false);
    input_.add_css_class("lock-hidden-input");
    input_.set_size_request(1, 1);
    input_.set_halign(Gtk::Align::START);
    input_.set_valign(Gtk::Align::START);
    input_.set_opacity(0.0);
    input_.set_can_target(false);
    input_.signal_changed().connect([this] {
        if (syncing_)
            return;
        screen_.set_text(input_.get_text());
    });
    root_.add_overlay(input_);

    build_cover();
    build_login();
    build_pills();
    build_bottom_right();
    build_session_menu();

    root_.signal_get_child_position().connect(
        sigc::mem_fun(*this, &LockSurface::on_child_position), false);
    set_child(root_);

    // click anywhere on the cover reveals the login stage
    auto click = Gtk::GestureClick::create();
    click->set_propagation_phase(Gtk::PropagationPhase::BUBBLE);
    click->signal_released().connect([this](int, double x, double y) {
        // click outside the open session menu closes it (the menu's own
        // buttons and the power button consume their clicks before this)
        if (session_menu_open_) {
            graphene_rect_t bounds;
            const graphene_point_t point =
                GRAPHENE_POINT_INIT(static_cast<float>(x), static_cast<float>(y));
            const bool inside =
                gtk_widget_compute_bounds(GTK_WIDGET(session_menu_.gobj()), GTK_WIDGET(gobj()),
                                          &bounds) &&
                graphene_rect_contains_point(&bounds, &point);
            if (!inside)
                set_session_menu_open(false);
        }
        if (stage_ == Stage::Cover)
            set_stage(Stage::Login);
        focus_input();
    });
    add_controller(click);

    // Noctalia's hover workaround: cursor movement re-focuses the input
    auto motion = Gtk::EventControllerMotion::create();
    motion->signal_motion().connect([this](double, double) {
        if (!input_.has_focus())
            focus_input();
    });
    add_controller(motion);

    auto key = Gtk::EventControllerKey::create();
    key->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    key->signal_key_pressed().connect(sigc::mem_fun(*this, &LockSurface::on_key_pressed),
                                      false);
    add_controller(key);

    service_connections_.push_back(
        screen_.signal_changed().connect(sigc::mem_fun(*this, &LockSurface::refresh)));
    service_connections_.push_back(
        Config::get().signal_changed().connect(sigc::mem_fun(*this, &LockSurface::apply_config)));
    service_connections_.push_back(
        UPower::get().signal_changed().connect(sigc::mem_fun(*this, &LockSurface::update_battery)));
    service_connections_.push_back(PowerProfiles::get().signal_changed().connect(
        sigc::mem_fun(*this, &LockSurface::update_battery)));

    clock_timer_ = Glib::signal_timeout().connect(
        [this] {
            update_clock();
            return true;
        },
        1000);
    blink_timer_ = Glib::signal_timeout().connect(
        [this] {
            blink_off_ = !blink_off_;
            for (Gtk::Box* caret : {&caret_left_, &caret_right_}) {
                if (blink_off_)
                    caret->add_css_class("off");
                else
                    caret->remove_css_class("off");
            }
            return true;
        },
        kBlinkMs);

    signal_map().connect([this] {
        focus_input();
        start_entrance();
        if (preview_ && screen_.preview_session_menu()) {
            set_stage(Stage::Login);
            set_session_menu_open(true);
        }
    });

    apply_config();
    update_clock();
    update_battery();
    refresh();
    cover_box_.set_opacity(1.0);
    login_box_.set_opacity(0.0);
    login_box_.set_can_target(false);
}

LockSurface::~LockSurface() {
    for (auto& connection : service_connections_)
        connection.disconnect();
    clock_timer_.disconnect();
    blink_timer_.disconnect();
    countdown_timer_.disconnect();
}

// -- building --------------------------------------------------------------

void LockSurface::build_cover() {
    cover_box_.add_css_class("lock-cover");
    time_label_.add_css_class("lock-time");
    date_label_.add_css_class("lock-date");
    cover_box_.append(time_label_);
    cover_box_.append(date_label_);
    root_.add_overlay(cover_box_);
}

void LockSurface::build_login() {
    login_box_.add_css_class("lock-login");

    // avatar: 130 ring around a 124 circle — the image pre-scaled and
    // center-cropped to exactly 124px (a Gtk::Picture would grow to the
    // file's natural size), or Noctalia's "person" glyph fallback
    avatar_ring_.add_css_class("lock-avatar-ring");
    avatar_ring_.set_size_request(130, 130);
    avatar_ring_.set_halign(Gtk::Align::CENTER);
    avatar_ring_.set_valign(Gtk::Align::CENTER);
    avatar_.add_css_class("lock-avatar");
    avatar_.set_size_request(124, 124);
    avatar_.set_can_shrink(false);
    avatar_.set_overflow(Gtk::Overflow::HIDDEN);
    avatar_.set_halign(Gtk::Align::CENTER);
    avatar_.set_valign(Gtk::Align::CENTER);
    avatar_fallback_.set_text(kGlyphUser);
    avatar_fallback_.add_css_class("lock-glyph");
    avatar_fallback_.add_css_class("lock-avatar-fallback");
    avatar_fallback_.set_size_request(124, 124);
    avatar_fallback_.set_halign(Gtk::Align::CENTER);
    avatar_fallback_.set_valign(Gtk::Align::CENTER);
    auto avatar_texture = load_avatar(screen_.avatar_path(), 124);
    if (avatar_texture) {
        avatar_.set_paintable(avatar_texture);
        avatar_ring_.append(avatar_);
    } else {
        avatar_ring_.append(avatar_fallback_);
    }
    login_box_.append(avatar_ring_);

    name_label_.set_text(screen_.display_name());
    name_label_.add_css_class("lock-name");
    name_label_.set_halign(Gtk::Align::CENTER);
    login_box_.append(name_label_);

    // password field: 200x30 pill, dots/plain text + caret, eye + submit
    password_field_.add_css_class("lock-password");
    password_field_.set_size_request(200, 30);
    password_field_.set_halign(Gtk::Align::CENTER);
    password_field_.set_margin_top(18);

    auto* content = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
    content->set_margin_start(16);
    content->set_valign(Gtk::Align::CENTER);
    content->set_hexpand(true);

    caret_left_.add_css_class("lock-caret");
    caret_left_.set_size_request(2, 20);
    caret_left_.set_valign(Gtk::Align::CENTER);
    content->append(caret_left_);

    // Noctalia clips the dots at 110px (newest hidden), caret at the end
    visual_host_.set_policy(Gtk::PolicyType::EXTERNAL, Gtk::PolicyType::NEVER);
    visual_host_.set_propagate_natural_width(true);
    visual_host_.set_max_content_width(110);
    visual_host_.set_has_frame(false);
    visual_host_.add_css_class("lock-visual-host");
    visual_host_.set_valign(Gtk::Align::CENTER);
    dots_label_.add_css_class("lock-glyph");
    dots_label_.add_css_class("lock-dots");
    dots_label_.set_xalign(0.0f);
    plain_label_.add_css_class("lock-plain");
    plain_label_.set_xalign(0.0f);
    plain_label_.set_ellipsize(Pango::EllipsizeMode::END);
    visual_box_.append(dots_label_);
    visual_box_.append(plain_label_);
    visual_host_.set_child(visual_box_);
    content->append(visual_host_);

    caret_right_.add_css_class("lock-caret");
    caret_right_.set_size_request(2, 20);
    caret_right_.set_valign(Gtk::Align::CENTER);
    content->append(caret_right_);
    password_field_.append(*content);

    eye_glyph_.set_text(kGlyphEye);
    eye_glyph_.add_css_class("lock-glyph");
    eye_button_.set_child(eye_glyph_);
    eye_button_.add_css_class("lock-eye");
    eye_button_.set_size_request(26, 26); // fits the 30px field without growing it
    eye_button_.set_valign(Gtk::Align::CENTER);
    eye_button_.set_margin_end(6);
    eye_button_.set_can_focus(false);
    eye_button_.set_cursor(Gdk::Cursor::create("pointer"));
    eye_button_.signal_clicked().connect([this] {
        password_visible_ = !password_visible_;
        update_password_visuals();
        focus_input();
    });
    password_field_.append(eye_button_);

    // (Noctalia's arrow submit button was dropped per user — Enter submits)
    login_box_.append(password_field_);
    root_.add_overlay(login_box_);
}

// PreserveAspectCrop at `size` px: scale so the shorter side is `size`, then
// cut the centered square. Small file, read synchronously at lock time.
Glib::RefPtr<Gdk::Texture> LockSurface::load_avatar(const std::string& path, int size) {
    if (path.empty())
        return {};
    try {
        int image_w = 0, image_h = 0;
        if (gdk_pixbuf_get_file_info(path.c_str(), &image_w, &image_h) == nullptr ||
            image_w <= 0 || image_h <= 0)
            return {};
        auto pixbuf = image_w >= image_h ? Gdk::Pixbuf::create_from_file(path, -1, size, true)
                                         : Gdk::Pixbuf::create_from_file(path, size, -1, true);
        if (!pixbuf)
            return {};
        const int w = pixbuf->get_width();
        const int h = pixbuf->get_height();
        const int side = std::min({w, h, size});
        auto square = Gdk::Pixbuf::create_subpixbuf(pixbuf, (w - side) / 2, (h - side) / 2,
                                                    side, side);
        return Gdk::Texture::create_for_pixbuf(square);
    } catch (const Glib::Error& e) {
        g_warning("lock screen: cannot load avatar %s: %s", path.c_str(), e.what());
        return {};
    }
}

void LockSurface::build_pills() {
    auto setup_pill = [this](Gtk::Box& pill, const char* kind) {
        pill.add_css_class("lock-pill");
        pill.add_css_class(kind);
        pill.set_halign(Gtk::Align::CENTER);
        pill.set_valign(Gtk::Align::END);
        pill.set_margin_bottom(kPillBottomMargin);
        pill.set_size_request(-1, 50);
        pill.set_opacity(0.0); // faded in/out, never unmapped
        pill.set_can_target(false);
        root_.add_overlay(pill);
    };

    setup_pill(info_pill_, "info");
    info_pill_.append(*glyph_label(kGlyphCircleKey, "lock-pill-icon"));
    info_text_.add_css_class("lock-pill-text");
    info_pill_.append(info_text_);

    setup_pill(error_pill_, "error");
    error_pill_.append(*glyph_label(kGlyphAlertCircle, "lock-pill-icon"));
    error_text_.add_css_class("lock-pill-text");
    error_pill_.append(error_text_);

    setup_pill(countdown_pill_, "countdown");
    countdown_pill_.append(*glyph_label(kGlyphClock, "lock-pill-icon"));
    countdown_text_.add_css_class("lock-pill-text");
    countdown_text_.add_css_class("bold");
    countdown_text_.set_hexpand(true);
    countdown_text_.set_xalign(0.0f);
    countdown_pill_.append(countdown_text_);
    countdown_cancel_.set_child(*glyph_label(kGlyphClose, nullptr));
    countdown_cancel_.add_css_class("lock-cancel");
    countdown_cancel_.set_size_request(32, 32);
    countdown_cancel_.set_valign(Gtk::Align::CENTER);
    countdown_cancel_.set_can_focus(false);
    countdown_cancel_.set_tooltip_text("Cancel timer");
    countdown_cancel_.set_cursor(Gdk::Cursor::create("pointer"));
    countdown_cancel_.signal_clicked().connect([this] { cancel_timer(); });
    countdown_pill_.append(countdown_cancel_);
}

void LockSurface::build_bottom_right() {
    bottom_right_.set_halign(Gtk::Align::END);
    bottom_right_.set_valign(Gtk::Align::END);
    bottom_right_.set_margin_end(kCornerMargin);
    bottom_right_.set_margin_bottom(kCornerMargin);

    // battery: win11 glyph (fill under an optional bolt/leaf frame) + percent
    battery_box_.add_css_class("lock-battery");
    battery_fill_.add_css_class("icon");
    battery_fill_.add_css_class("icon-fill");
    battery_frame_.add_css_class("icon");
    battery_overlay_.set_child(battery_fill_);
    battery_overlay_.add_overlay(battery_frame_);
    battery_overlay_.set_valign(Gtk::Align::CENTER);
    battery_box_.append(battery_overlay_);
    battery_percent_.add_css_class("lock-battery-text");
    battery_percent_.set_valign(Gtk::Align::CENTER);
    battery_box_.append(battery_percent_);
    bottom_right_.append(battery_box_);

    power_button_.set_child(*glyph_label(kGlyphPower, nullptr));
    power_button_.add_css_class("lock-power");
    power_button_.set_size_request(kPowerButtonSize, kPowerButtonSize);
    power_button_.set_valign(Gtk::Align::CENTER);
    power_button_.set_can_focus(false);
    power_button_.set_cursor(Gdk::Cursor::create("pointer"));
    power_button_.signal_clicked().connect([this] {
        set_session_menu_open(!session_menu_open_);
        focus_input();
    });
    bottom_right_.append(power_button_);
    root_.add_overlay(bottom_right_);
}

void LockSurface::build_session_menu() {
    session_menu_.add_css_class("lock-session-menu");
    session_menu_.set_halign(Gtk::Align::END);
    session_menu_.set_valign(Gtk::Align::END);
    session_menu_.set_margin_end(kCornerMargin);
    session_menu_.set_margin_bottom(kCornerMargin + kPowerButtonSize + 9);
    session_menu_.set_size_request(kSessionMenuWidth, -1);
    session_menu_.set_opacity(0.0);
    session_menu_.set_can_target(false);

    const auto& cfg = Config::get().session();
    for (const char* key : kMenuKeys) {
        const SessionAction* action = find_action(key);
        if (action == nullptr)
            continue;
        // Noctalia's showHibernateOnLockScreen ≙ our session.items.hibernate
        if (g_strcmp0(key, "hibernate") == 0 && !cfg.item_enabled(key, action->default_on))
            continue;
        auto* button = Gtk::make_managed<Gtk::Button>();
        button->add_css_class("lock-session-item");
        if (action->destructive)
            button->add_css_class("destructive");
        button->set_can_focus(false);
        button->set_size_request(-1, 36);
        button->set_cursor(Gdk::Cursor::create("pointer"));
        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 9);
        row->append(*glyph_label(action->glyph, "lock-session-glyph"));
        auto* label = Gtk::make_managed<Gtk::Label>(action->label);
        label->add_css_class("lock-session-label");
        label->set_xalign(0.0f);
        label->set_hexpand(true);
        label->set_ellipsize(Pango::EllipsizeMode::END);
        row->append(*label);
        button->set_child(*row);
        button->signal_clicked().connect([this, action] {
            set_session_menu_open(false);
            start_timer(*action);
            focus_input();
        });
        session_menu_.append(*button);
    }
    root_.add_overlay(session_menu_);
}

// -- layout / stages --------------------------------------------------------

// Noctalia positions the cover column at 38% and the login column at 42% of
// the height (centered on their own height), each shifted 40px toward the
// stage it fades from.
bool LockSurface::on_child_position(Gtk::Widget* widget, Gdk::Rectangle& allocation) {
    if (widget != &cover_box_ && widget != &login_box_)
        return false;
    int min_w = 0, nat_w = 0, min_h = 0, nat_h = 0, baseline_min = 0, baseline_nat = 0;
    widget->measure(Gtk::Orientation::HORIZONTAL, -1, min_w, nat_w, baseline_min, baseline_nat);
    widget->measure(Gtk::Orientation::VERTICAL, nat_w, min_h, nat_h, baseline_min, baseline_nat);
    const int width = root_.get_width();
    const int height = root_.get_height();
    const bool cover = widget == &cover_box_;
    const double fraction = cover ? 0.38 : 0.42;
    const double offset = cover ? -kStageShiftPx * progress_ : kStageShiftPx * (1.0 - progress_);
    double y = height * fraction - nat_h / 2.0 + offset;
    // appear: Noctalia's column starts at y = -h/2 (its parent has no height
    // yet) and its Behavior on y slides it down into place
    if (cover)
        y = -nat_h / 2.0 + (y + nat_h / 2.0) * entrance_;
    allocation.set_x((width - nat_w) / 2);
    allocation.set_y(static_cast<int>(std::lround(y)));
    allocation.set_width(nat_w);
    allocation.set_height(nat_h);
    return true;
}

void LockSurface::set_stage(Stage stage) {
    if (stage == stage_)
        return;
    stage_ = stage;
    set_session_menu_open(false);
    anim_from_ = progress_;
    anim_to_ = stage == Stage::Login ? 1.0 : 0.0;
    anim_start_us_ = 0;
    cover_box_.set_visible(true);
    login_box_.set_visible(true);
    cover_box_.set_can_target(stage == Stage::Cover);
    login_box_.set_can_target(stage == Stage::Login);
    if (!animating_) {
        animating_ = true;
        add_tick_callback([this](const Glib::RefPtr<Gdk::FrameClock>&) {
            on_stage_animation_tick();
            return animating_;
        });
    }
    if (stage == Stage::Login)
        focus_input();
}

void LockSurface::on_stage_animation_tick() {
    const gint64 now = g_get_monotonic_time();
    if (anim_start_us_ == 0)
        anim_start_us_ = now;
    const double t = std::min(1.0, static_cast<double>(now - anim_start_us_) /
                                       (kAnimationNormalMs * 1000.0));
    progress_ = anim_from_ + (anim_to_ - anim_from_) * ease_out_cubic(t);
    cover_box_.set_opacity(1.0 - progress_);
    login_box_.set_opacity(progress_);
    root_.queue_allocate();
    if (t >= 1.0) {
        animating_ = false;
        cover_box_.set_visible(progress_ < 1.0);
        login_box_.set_visible(progress_ > 0.0);
    }
}

// -- fades ------------------------------------------------------------------

void LockSurface::fade(Gtk::Widget& widget, double to, int ms) {
    if (to > 0.0)
        widget.set_can_target(true); // clickable as soon as it starts appearing
    if (widget.get_opacity() == to) {
        fades_.erase(&widget);
        widget.set_can_target(to > 0.0);
        return;
    }
    fades_[&widget] = Fade{widget.get_opacity(), to, ms, 0};
    if (!fading_) {
        fading_ = true;
        add_tick_callback([this](const Glib::RefPtr<Gdk::FrameClock>&) {
            on_fade_tick();
            return fading_;
        });
    }
}

void LockSurface::on_fade_tick() {
    const gint64 now = g_get_monotonic_time();
    for (auto it = fades_.begin(); it != fades_.end();) {
        Fade& f = it->second;
        if (f.start_us == 0)
            f.start_us = now;
        const double t = std::min(1.0, static_cast<double>(now - f.start_us) / (f.ms * 1000.0));
        it->first->set_opacity(f.from + (f.to - f.from) * ease_out_cubic(t));
        if (t >= 1.0) {
            it->first->set_can_target(f.to > 0.0);
            it = fades_.erase(it);
        } else {
            ++it;
        }
    }
    fading_ = !fades_.empty();
}

void LockSurface::start_entrance() {
    if (entering_ || entrance_ >= 1.0)
        return;
    entering_ = true;
    entrance_start_us_ = 0;
    add_tick_callback([this](const Glib::RefPtr<Gdk::FrameClock>&) {
        const gint64 now = g_get_monotonic_time();
        if (entrance_start_us_ == 0)
            entrance_start_us_ = now;
        const double t = std::min(1.0, static_cast<double>(now - entrance_start_us_) /
                                           (kAnimationNormalMs * 1000.0));
        entrance_ = ease_out_cubic(t);
        root_.queue_allocate();
        entering_ = t < 1.0;
        return entering_;
    });
}

bool LockSurface::on_key_pressed(guint keyval, guint, Gdk::ModifierType) {
    const bool enter = keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter;
    const bool escape = keyval == GDK_KEY_Escape;
    g_debug("lock screen: key %s (stage %s)", gdk_keyval_name(keyval),
            stage_ == Stage::Cover ? "cover" : "login");
    if (stage_ == Stage::Cover) {
        if (escape && preview_) {
            screen_.close_preview();
            return true;
        }
        // any key reveals the login stage; printable ones still reach the input
        set_stage(Stage::Login);
        if (enter || escape)
            return true;
    }
    if (enter) {
        screen_.try_unlock();
        return true;
    }
    if (escape) {
        if (timer_active_) {
            cancel_timer();
        } else {
            screen_.set_text("");
            set_stage(Stage::Cover);
        }
        return true;
    }
    focus_input();
    return false;
}

void LockSurface::focus_input() {
    if (!input_.has_focus())
        input_.grab_focus();
}

// -- state → widgets --------------------------------------------------------

void LockSurface::refresh() {
    const std::string& text = screen_.text();
    if (input_.get_text() != text) {
        syncing_ = true;
        input_.set_text(text);
        syncing_ = false;
    }
    const bool busy = screen_.unlock_in_progress();
    input_.set_editable(!busy);
    eye_button_.set_sensitive(!busy);
    update_password_visuals();
    update_pills();
}

void LockSurface::update_password_visuals() {
    const std::string text = input_.get_text();
    const gsize length = g_utf8_strlen(text.c_str(), -1);
    const bool has_text = length > 0;

    std::string dots;
    for (gsize i = 0; i < length; ++i)
        dots += kGlyphDot;
    dots_label_.set_text(dots);
    plain_label_.set_text(text);
    dots_label_.set_visible(has_text && !password_visible_);
    plain_label_.set_visible(has_text && password_visible_);
    visual_host_.set_visible(has_text);
    caret_left_.set_visible(!has_text);
    caret_right_.set_visible(has_text);
    eye_button_.set_visible(has_text);
    eye_glyph_.set_text(password_visible_ ? kGlyphEyeOff : kGlyphEye);
}

void LockSurface::update_pills() {
    const bool countdown = timer_active_;
    info_text_.set_text(screen_.info_message());
    error_text_.set_text(screen_.error_message().empty() ? "Authentication failed"
                                                         : screen_.error_message());
    fade(info_pill_, !countdown && screen_.show_info() && !screen_.info_message().empty() ? 1 : 0,
         kAnimationNormalMs);
    fade(error_pill_, !countdown && screen_.show_failure() ? 1 : 0, kAnimationNormalMs);
    fade(countdown_pill_, countdown ? 1 : 0, kAnimationNormalMs);
}

void LockSurface::update_clock() {
    auto now = Glib::DateTime::create_now_local();
    // Noctalia's cover clock is 12-hour without a leading zero: h:mm
    int hour = now.get_hour() % 12;
    if (hour == 0)
        hour = 12;
    time_label_.set_text(Glib::ustring::compose("%1:%2", hour, now.format("%M")));
    date_label_.set_text(now.format("%A, %B %-d")); // "dddd, MMMM d"
}

void LockSurface::update_battery() {
    auto& upower = UPower::get();
    battery_box_.set_visible(upower.available());
    if (!upower.available())
        return;
    const double pct = upower.percentage();
    const int idx = std::clamp(static_cast<int>(std::ceil(pct / 10.0)) - 1, 0, 9);
    battery_fill_.set_text(kBatteryLevels[idx]);
    const bool plugged = upower.plugged();
    const bool saver = !plugged && PowerProfiles::get().saver();
    battery_frame_.set_text(plugged ? kBatteryChargingFrame : kBatterySaverFrame);
    battery_frame_.set_visible(plugged || saver);
    if (plugged)
        battery_box_.add_css_class("charging");
    else
        battery_box_.remove_css_class("charging");
    if (saver)
        battery_box_.add_css_class("saver");
    else
        battery_box_.remove_css_class("saver");
    battery_percent_.set_text(Glib::ustring::compose("%1%%", static_cast<int>(std::lround(pct))));
}

void LockSurface::apply_config() {
    const auto& cfg = Config::get().lock_screen();
    int width = 0, height = 0, scale = 1;
    if (monitor_) {
        Gdk::Rectangle geometry;
        monitor_->get_geometry(geometry);
        width = geometry.get_width();
        height = geometry.get_height();
        scale = std::max(1, monitor_->get_scale_factor());
    }
    background_.set_blur(cfg.blur);
    background_.set_image(cfg.background, width, height, scale);
}

// -- session menu + countdown ------------------------------------------------

void LockSurface::set_session_menu_open(bool open) {
    session_menu_open_ = open;
    fade(session_menu_, open ? 1.0 : 0.0, kAnimationFastMs);
}

// Noctalia's startTimer: first click arms a 10s countdown (second click on the
// same action fires immediately); the pill shows the remaining seconds.
void LockSurface::start_timer(const SessionAction& action) {
    if (timer_active_ && pending_action_ == &action) {
        execute_action(action);
        return;
    }
    pending_action_ = &action;
    time_remaining_ms_ = kCountdownMs;
    timer_active_ = true;
    countdown_timer_.disconnect();
    countdown_timer_ = Glib::signal_timeout().connect(
        [this] {
            time_remaining_ms_ -= kCountdownTickMs;
            if (time_remaining_ms_ <= 0) {
                if (pending_action_ != nullptr)
                    execute_action(*pending_action_);
                return false;
            }
            update_pills();
            countdown_text_.set_text(Glib::ustring::compose(
                "%1 in %2 seconds...", pending_action_->label,
                (time_remaining_ms_ + 999) / 1000));
            return true;
        },
        kCountdownTickMs);
    countdown_text_.set_text(
        Glib::ustring::compose("%1 in %2 seconds...", action.label, kCountdownMs / 1000));
    update_pills();
}

void LockSurface::cancel_timer() {
    countdown_timer_.disconnect();
    timer_active_ = false;
    pending_action_ = nullptr;
    time_remaining_ms_ = 0;
    update_pills();
}

void LockSurface::execute_action(const SessionAction& action) {
    countdown_timer_.disconnect();
    run_session_action(action);
    cancel_timer();
}

} // namespace hyprshell
