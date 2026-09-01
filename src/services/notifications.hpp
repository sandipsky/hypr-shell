#pragma once

#include <giomm.h>
#include <sigc++/sigc++.h>

#include <string>
#include <vector>

namespace hyprshell {

// org.freedesktop.Notifications daemon (phase 3) + notification history,
// a port of Noctalia's NotificationService.
//
// Owns the well-known bus name with default flags, so if another daemon
// (mako, dunst, Noctalia) is running we queue behind it and take over the
// moment it exits — available() flips then. History is kept for the panel
// either way; it just stays empty while someone else receives notifications.
//
// Popups: up to 5 at once, per-urgency countdowns (progress ticks at 50ms,
// hover pauses), replacement by replaces_id, duplicate-content dedup, DND
// suppression, per-app rules (block/hide/mute) and optional sounds (paplay).
// The popup windows themselves live in bar/notification_popup.*.
class NotificationService {
public:
    struct Action {
        std::string key; // identifier sent back via ActionInvoked
        std::string text;
    };

    struct Notification {
        std::string id;          // content+time hash, stable across restarts
        guint32 original_id = 0; // the u32 returned to the sender
        std::string app_name;    // prettified (Noctalia's getAppName)
        std::string summary;     // sanitized Pango markup
        std::string body;        // sanitized Pango markup
        int urgency = 1;         // 0 low / 1 normal / 2 critical
        gint64 timestamp_ms = 0;
        std::string image;       // file path or themed icon name ("" = none)
        bool image_from_cache = false; // file is ours — delete with the entry
        std::string desktop_entry;     // hint, icon fallback for the panel
        std::vector<Action> actions;
    };

    // A live popup: the notification plus its countdown state (Noctalia's
    // popupState metadata). progress runs 1 → 0; duration_ms -1 never expires.
    struct Popup {
        Notification n;
        gint64 start_ms = 0;
        gint64 duration_ms = 8000;
        bool paused = false;
        gint64 pause_start_ms = 0;
        double progress = 1.0;
    };

    static NotificationService& get();

    NotificationService(const NotificationService&) = delete;
    NotificationService& operator=(const NotificationService&) = delete;

    // True while we own the bus name (we are THE notification daemon).
    bool available() const { return available_; }

    const std::vector<Notification>& history() const { return history_; }

    // Runtime-only, like Noctalia's doNotDisturb: history still records while
    // enabled; it will gate popups once those exist.
    bool do_not_disturb() const { return do_not_disturb_; }
    void set_do_not_disturb(bool dnd);

    // Unread = entries newer than the last time the panel was opened.
    gint64 last_seen_ms() const { return last_seen_ms_; }
    void update_last_seen();
    int unread_count() const;

    void remove_from_history(const std::string& id);
    void clear_history();

    // Emits ActionInvoked(original_id, key) — we are the daemon, so this IS
    // the invocation. Returns false when it can't possibly reach the app.
    bool invoke_action(const std::string& id, const std::string& key);

    // Noctalia's focusSenderWindow: fuzzy-match the app name against Hyprland
    // clients' classes and focus the best match.
    void focus_sender_window(const std::string& app_name);

    // -- popups (newest first, capped at 5) -----------------------------------
    const std::vector<Popup>& popups() const { return popups_; }
    void dismiss_popup(const std::string& id); // popup only — history untouched
    void pause_popup(const std::string& id);   // hover holds the countdown
    void resume_popup(const std::string& id);

    // History, DND or daemon availability changed.
    sigc::signal<void()>& signal_changed() { return changed_; }
    // Popups added/removed/replaced.
    sigc::signal<void()>& signal_popups_changed() { return popups_changed_; }
    // Countdown tick — cheap redraw of the progress bars.
    sigc::signal<void()>& signal_popup_progress() { return popup_progress_; }

private:
    NotificationService();

    void on_bus_acquired(const Glib::RefPtr<Gio::DBus::Connection>& connection,
                         const Glib::ustring& name);
    void on_method_call(const Glib::RefPtr<Gio::DBus::Connection>& connection,
                        const Glib::ustring& sender, const Glib::ustring& object_path,
                        const Glib::ustring& interface_name,
                        const Glib::ustring& method_name,
                        const Glib::VariantContainerBase& parameters,
                        const Glib::RefPtr<Gio::DBus::MethodInvocation>& invocation);
    void handle_notify(const Glib::VariantContainerBase& parameters,
                       const Glib::RefPtr<Gio::DBus::MethodInvocation>& invocation);

    // image-data hint (iiibiiay) → downscaled PNG in image_dir_; "" on failure
    std::string decode_image_data(const Glib::VariantBase& value, const std::string& id);
    void add_to_history(Notification n);
    void delete_cached_image(const Notification& n);
    void load_history();
    void save_history(); // debounced
    void perform_save_history();

    // popups
    void show_popup(const Notification& n, gint32 expire_timeout_ms, bool muted);
    gint64 popup_duration_ms(int urgency, gint32 expire_timeout_ms) const;
    void ensure_popup_timer();
    bool on_popup_tick(); // progress + expiry, Noctalia's updateAllProgress
    void play_sound(const Notification& n);

    // notifications.enabled: release / (re)acquire the bus name
    void apply_enabled();
    std::string evaluate_rules(const Notification& n) const; // ""/block/hide/mute

    bool available_ = false;
    bool do_not_disturb_ = false;
    bool config_dnd_ = false; // last seen notifications.do_not_disturb
    guint32 next_id_ = 1;
    gint64 last_seen_ms_ = 0;
    std::vector<Notification> history_; // newest first, capped at 100
    std::string state_path_;            // ~/.cache/hypr-shell/notifications.json
    std::string image_dir_;             // ~/.cache/hypr-shell/notifications/

    std::vector<Popup> popups_; // newest first, capped at 5
    gint64 last_sound_ms_ = 0;  // 100ms sound rate limit
    std::string default_sound_; // bundled notification-generic.wav, resolved once

    guint own_name_id_ = 0;
    guint registration_id_ = 0;
    Glib::RefPtr<Gio::DBus::Connection> connection_;
    Glib::RefPtr<Gio::DBus::NodeInfo> node_info_;
    Gio::DBus::InterfaceVTable vtable_;
    sigc::connection save_timer_;
    sigc::connection popup_timer_;
    sigc::signal<void()> changed_;
    sigc::signal<void()> popups_changed_;
    sigc::signal<void()> popup_progress_;
};

} // namespace hyprshell
