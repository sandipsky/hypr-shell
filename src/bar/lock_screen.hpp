#pragma once

#include "services/pam_auth.hpp"

#include <giomm.h>
#include <gtkmm.h>

#include <string>
#include <vector>

typedef struct _GtkSessionLockInstance GtkSessionLockInstance;
typedef struct _GdkMonitor GdkMonitor;

namespace hyprshell {

class LockSurface;

// Lock screen — Noctalia's LockScreen + LockContext on ext-session-lock-v1
// (gtk4-layer-shell's GtkSessionLockInstance). One LockSurface window per
// monitor renders the UI; this object owns the lock, the shared password text
// and the PAM conversation (Noctalia's LockContext state machine: info /
// failure messages, waiting-for-password, unlock-in-progress) and answers
// request_lock() from the idle daemon, the session menus, `hypr-shell --lock`
// and logind's Lock signal (`loginctl lock-session`).
//
// HS_LOCK_PREVIEW=1 shows the UI as an ordinary overlay layer window instead
// of a real lock (Escape on the cover closes it) — for styling and for
// exercising PAM without the risk of locking yourself out.
class LockScreen {
public:
    LockScreen();
    ~LockScreen();

    LockScreen(const LockScreen&) = delete;
    LockScreen& operator=(const LockScreen&) = delete;

    void lock();
    bool locked() const { return locked_; }
    void open_preview();

    // -- LockContext state, read by the surfaces ---------------------------
    const std::string& text() const { return text_; }
    void set_text(const std::string& text);
    void try_unlock();
    bool unlock_in_progress() const { return unlock_in_progress_; }
    bool show_info() const { return show_info_; }
    bool show_failure() const { return show_failure_; }
    const std::string& info_message() const { return info_message_; }
    const std::string& error_message() const { return error_message_; }
    const std::string& display_name() const { return display_name_; }
    const std::string& avatar_path() const { return avatar_path_; }
    bool preview() const { return preview_ != nullptr; }
    // HS_LOCK_PREVIEW=2: surfaces open their session menu on map (dev hook)
    bool preview_session_menu() const { return g_strcmp0(g_getenv("HS_LOCK_PREVIEW"), "2") == 0; }
    void close_preview();

    sigc::signal<void()>& signal_changed() { return changed_; }

private:
    static void on_monitor(GtkSessionLockInstance*, GdkMonitor* monitor, gpointer data);
    static void on_locked(GtkSessionLockInstance*, gpointer data);
    static void on_failed(GtkSessionLockInstance*, gpointer data);
    static void on_unlocked(GtkSessionLockInstance*, gpointer data);

    void add_surface(const Glib::RefPtr<Gdk::Monitor>& monitor, bool preview);
    void reset_state();
    void notify() { changed_.emit(); }
    void on_pam_message(const std::string& message, bool is_error);
    void on_pam_response_required();
    void on_pam_completed(bool success, const std::string& message);
    void unlocked();
    void setup_logind();
    void prepare_wallpapers();
    void on_session_path(Glib::RefPtr<Gio::AsyncResult>& result);
    void set_locked_hint(bool locked);

    GtkSessionLockInstance* instance_ = nullptr;
    std::vector<LockSurface*> surfaces_; // owned by GTK; destroyed by the lock library
    LockSurface* preview_ = nullptr;
    bool locked_ = false;

    PamAuth pam_;
    std::string service_;
    std::string user_;
    std::string display_name_;
    std::string avatar_path_;

    std::string text_;
    bool unlock_in_progress_ = false;
    bool show_info_ = false;
    bool show_failure_ = false;
    std::string info_message_;
    std::string error_message_;
    sigc::signal<void()> changed_;

    Glib::RefPtr<Gio::DBus::Connection> system_bus_;
    std::string session_path_;
    guint lock_subscription_ = 0;
};

} // namespace hyprshell
