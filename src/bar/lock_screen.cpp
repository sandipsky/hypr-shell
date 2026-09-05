#include "bar/lock_screen.hpp"

#include "bar/lock_background.hpp"
#include "bar/lock_surface.hpp"
#include "services/config.hpp"
#include "services/session.hpp"
#include "services/user_info.hpp"

#include <gtk4-session-lock.h>

#include <unistd.h>

#include <algorithm>

namespace hyprshell {

LockScreen::LockScreen() {
    service_ = PamAuth::detect_service();
    user_ = Glib::get_user_name();
    // Noctalia's HostService.displayName (GECOS, else the login) and
    // general.avatarImage (~/.face here; HS_LOCK_AVATAR overrides, an
    // unreadable path shows the bundled fallback picture) — see
    // services/user_info.hpp, shared with the control center's profile card
    display_name_ = user_display_name();
    avatar_path_ = user_avatar_path();
    g_message("lock screen: PAM service %s, user %s", service_.c_str(), user_.c_str());

    pam_.signal_message().connect(sigc::mem_fun(*this, &LockScreen::on_pam_message));
    pam_.signal_response_required().connect(
        sigc::mem_fun(*this, &LockScreen::on_pam_response_required));
    pam_.signal_completed().connect(sigc::mem_fun(*this, &LockScreen::on_pam_completed));

    if (gtk_session_lock_is_supported()) {
        instance_ = gtk_session_lock_instance_new();
        g_signal_connect(instance_, "monitor", G_CALLBACK(&LockScreen::on_monitor), this);
        g_signal_connect(instance_, "locked", G_CALLBACK(&LockScreen::on_locked), this);
        g_signal_connect(instance_, "failed", G_CALLBACK(&LockScreen::on_failed), this);
        g_signal_connect(instance_, "unlocked", G_CALLBACK(&LockScreen::on_unlocked), this);
    } else {
        g_warning("lock screen: ext-session-lock-v1 is not supported by this compositor");
    }

    signal_lock_requested().connect(sigc::mem_fun(*this, &LockScreen::lock));
    setup_logind();

    // decode + blur the wallpaper for every monitor now, not at lock time
    prepare_wallpapers();
    Config::get().signal_changed().connect(sigc::mem_fun(*this, &LockScreen::prepare_wallpapers));
    if (auto monitors = Gdk::Display::get_default()->get_monitors())
        monitors->signal_items_changed().connect(
            [this](guint, guint, guint) { prepare_wallpapers(); });
}

void LockScreen::prepare_wallpapers() {
    const auto& cfg = Config::get().lock_screen();
    auto& cache = LockWallpaperCache::get();
    cache.retain(cfg.background, cfg.blur); // frees textures of a previous image / blur
    if (cfg.background.empty())
        return;
    auto monitors = Gdk::Display::get_default()->get_monitors();
    if (!monitors)
        return;
    for (guint i = 0; i < monitors->get_n_items(); ++i) {
        auto monitor = std::dynamic_pointer_cast<Gdk::Monitor>(monitors->get_object(i));
        if (!monitor)
            continue;
        Gdk::Rectangle geometry;
        monitor->get_geometry(geometry);
        cache.prepare(cfg.background, geometry.get_width(), geometry.get_height(),
                      std::max(1, monitor->get_scale_factor()), cfg.blur);
    }
}

LockScreen::~LockScreen() {
    if (system_bus_ && lock_subscription_ != 0)
        system_bus_->signal_unsubscribe(lock_subscription_);
    if (instance_ != nullptr) {
        if (gtk_session_lock_instance_is_locked(instance_))
            gtk_session_lock_instance_unlock(instance_);
        g_object_unref(instance_);
    }
}

// -- locking ----------------------------------------------------------------

void LockScreen::lock() {
    if (instance_ == nullptr) {
        g_warning("lock screen: cannot lock — session lock unsupported");
        return;
    }
    if (locked_ || gtk_session_lock_instance_is_locked(instance_))
        return;
    if (preview_ != nullptr)
        close_preview();
    reset_state();
    g_message("lock screen: locking");
    if (!gtk_session_lock_instance_lock(instance_))
        g_warning("lock screen: lock request failed immediately");
}

void LockScreen::open_preview() {
    if (preview_ != nullptr || locked_)
        return;
    reset_state();
    auto display = Gdk::Display::get_default();
    auto monitors = display->get_monitors();
    Glib::RefPtr<Gdk::Monitor> monitor;
    if (monitors && monitors->get_n_items() > 0)
        monitor = std::dynamic_pointer_cast<Gdk::Monitor>(monitors->get_object(0));
    preview_ = Gtk::make_managed<LockSurface>(*this, monitor, /*preview=*/true);
    preview_->signal_destroy().connect([this] { preview_ = nullptr; });
    preview_->present();
}

void LockScreen::close_preview() {
    if (preview_ == nullptr)
        return;
    auto* window = preview_;
    preview_ = nullptr;
    window->destroy();
    reset_state();
}

void LockScreen::reset_state() {
    text_.clear();
    unlock_in_progress_ = false;
    show_info_ = false;
    show_failure_ = false;
    info_message_.clear();
    error_message_.clear();
    notify();
}

void LockScreen::add_surface(const Glib::RefPtr<Gdk::Monitor>& monitor, bool preview) {
    auto* surface = Gtk::make_managed<LockSurface>(*this, monitor, preview);
    surfaces_.push_back(surface);
    surface->signal_destroy().connect([this, surface] {
        surfaces_.erase(std::remove(surfaces_.begin(), surfaces_.end(), surface),
                        surfaces_.end());
    });
    gtk_session_lock_instance_assign_window_to_monitor(instance_, GTK_WINDOW(surface->gobj()),
                                                       monitor->gobj());
    surface->present();
}

void LockScreen::on_monitor(GtkSessionLockInstance*, GdkMonitor* monitor, gpointer data) {
    auto* self = static_cast<LockScreen*>(data);
    self->add_surface(Glib::wrap(monitor, /*take_copy=*/true), false);
}

void LockScreen::on_locked(GtkSessionLockInstance*, gpointer data) {
    auto* self = static_cast<LockScreen*>(data);
    self->locked_ = true;
    g_message("lock screen: locked");
    set_session_locked(true);
    self->set_locked_hint(true);
}

void LockScreen::on_failed(GtkSessionLockInstance*, gpointer data) {
    auto* self = static_cast<LockScreen*>(data);
    self->locked_ = false;
    g_warning("lock screen: could not acquire the session lock (another locker running?)");
    set_session_locked(false);
}

void LockScreen::on_unlocked(GtkSessionLockInstance*, gpointer data) {
    auto* self = static_cast<LockScreen*>(data);
    self->locked_ = false;
    g_message("lock screen: unlocked");
    set_session_locked(false);
    self->set_locked_hint(false);
    self->reset_state();
}

// -- LockContext ------------------------------------------------------------

void LockScreen::set_text(const std::string& text) {
    if (text == text_)
        return;
    text_ = text;
    if (!text_.empty()) {
        show_info_ = false;
        show_failure_ = false;
    }
    notify();
}

// Noctalia's tryUnlock: answer a waiting prompt, else start a transaction —
// a non-empty password answers PAM's first prompt straight away.
void LockScreen::try_unlock() {
    if (pam_.waiting_for_response()) {
        pam_.respond(text_);
        unlock_in_progress_ = true;
        show_info_ = false;
        notify();
        return;
    }
    if (pam_.active())
        return;
    g_message("lock screen: starting PAM authentication for %s", user_.c_str());
    pam_.start(service_, user_, text_);
    if (!text_.empty()) {
        unlock_in_progress_ = true;
        notify();
    }
}

void LockScreen::on_pam_message(const std::string& message, bool is_error) {
    g_message("lock screen: PAM message: %s%s", message.c_str(), is_error ? " (error)" : "");
    if (is_error) {
        error_message_ = message;
        show_failure_ = true;
        show_info_ = false;
    } else {
        info_message_ = message;
        show_info_ = true;
        show_failure_ = false;
    }
    notify();
}

void LockScreen::on_pam_response_required() {
    if (!text_.empty()) {
        // typed after the transaction started: answer with it
        pam_.respond(text_);
        unlock_in_progress_ = true;
    } else {
        info_message_ = "Password";
        show_info_ = true;
        show_failure_ = false;
    }
    notify();
}

void LockScreen::on_pam_completed(bool success, const std::string& message) {
    g_message("lock screen: PAM completed: %s", success ? "success" : message.c_str());
    unlock_in_progress_ = false;
    if (success) {
        unlocked();
        return;
    }
    text_.clear();
    error_message_ = "Authentication failed";
    show_failure_ = true;
    show_info_ = false;
    notify();
}

void LockScreen::unlocked() {
    text_.clear();
    show_info_ = false;
    show_failure_ = false;
    notify();
    if (preview_ != nullptr) {
        // dev mode: a successful authentication closes the preview, delayed
        // like Noctalia's unloadAfterUnlockTimer
        Glib::signal_timeout().connect_once([this] { close_preview(); }, 250);
        return;
    }
    if (instance_ != nullptr && gtk_session_lock_instance_is_locked(instance_))
        gtk_session_lock_instance_unlock(instance_);
}

// -- logind: `loginctl lock-session` and the LockedHint ------------------------

void LockScreen::setup_logind() {
    Gio::DBus::Connection::get(
        Gio::DBus::BusType::SYSTEM, [this](Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                system_bus_ = Gio::DBus::Connection::get_finish(result);
            } catch (const Glib::Error& e) {
                g_warning("lock screen: system bus unavailable: %s", e.what());
                return;
            }
            // Our session: by PID, else by $XDG_SESSION_ID (a shell started
            // from a terminal scope is not in the graphical session's cgroup).
            const char* session_id = g_getenv("XDG_SESSION_ID");
            if (session_id != nullptr && *session_id != '\0') {
                system_bus_->call(
                    "/org/freedesktop/login1", "org.freedesktop.login1.Manager", "GetSession",
                    Glib::Variant<std::tuple<Glib::ustring>>::create({session_id}),
                    sigc::mem_fun(*this, &LockScreen::on_session_path), "org.freedesktop.login1");
            } else {
                system_bus_->call(
                    "/org/freedesktop/login1", "org.freedesktop.login1.Manager",
                    "GetSessionByPID",
                    Glib::Variant<std::tuple<guint32>>::create({static_cast<guint32>(getpid())}),
                    sigc::mem_fun(*this, &LockScreen::on_session_path), "org.freedesktop.login1");
            }
        });
}

void LockScreen::on_session_path(Glib::RefPtr<Gio::AsyncResult>& result) {
    try {
        auto reply = system_bus_->call_finish(result);
        Glib::Variant<Glib::DBusObjectPathString> path;
        reply.get_child(path, 0);
        session_path_ = path.get();
    } catch (const Glib::Error& e) {
        g_warning("lock screen: cannot resolve the logind session: %s", e.what());
        return;
    }
    g_debug("lock screen: logind session %s", session_path_.c_str());
    lock_subscription_ = system_bus_->signal_subscribe(
        [this](const Glib::RefPtr<Gio::DBus::Connection>&, const Glib::ustring&,
               const Glib::ustring&, const Glib::ustring&, const Glib::ustring&,
               const Glib::VariantContainerBase&) {
            g_message("lock screen: logind Lock signal");
            lock();
        },
        "org.freedesktop.login1", "org.freedesktop.login1.Session", "Lock", session_path_);
}

void LockScreen::set_locked_hint(bool locked) {
    if (!system_bus_ || session_path_.empty())
        return;
    system_bus_->call(
        session_path_, "org.freedesktop.login1.Session", "SetLockedHint",
        Glib::Variant<std::tuple<bool>>::create({locked}),
        [this](Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                system_bus_->call_finish(result);
            } catch (const Glib::Error& e) {
                g_debug("lock screen: SetLockedHint failed: %s", e.what());
            }
        },
        "org.freedesktop.login1");
}

} // namespace hyprshell
