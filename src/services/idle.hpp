#pragma once

#include <glibmm.h>
#include <sigc++/sigc++.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct wl_registry;
struct wl_seat;
struct ext_idle_notifier_v1;
struct ext_idle_notification_v1;

namespace hyprshell {

// Idle daemon — Noctalia's IdleService on ext-idle-notify-v1. Three stages
// (idle.* timeouts in seconds, 0 = disabled): screen off (DPMS), lock,
// suspend. Each stage first shows a fade-to-black grace overlay for
// idle.fade_duration seconds; any input (the compositor's `resumed`) cancels
// it. Stages reached while a fade runs are queued and run once it completes.
// Optional per-stage commands and custom timeout/command pairs come from the
// same config object. A 1s heartbeat notification tracks idle seconds. The
// compositor honors idle inhibitors for us (v1 notifications).
//
// Until the phase-5 lock screen exists, "lock" means `loginctl lock-session`.
// HS_IDLE_DRY_RUN=1 logs every action instead of running it.
class Idle {
public:
    enum class Stage { None, ScreenOff, Lock, Suspend };

    static Idle& get();

    Idle(const Idle&) = delete;
    Idle& operator=(const Idle&) = delete;

    bool available() const { return notifier_ != nullptr; }
    int idle_seconds() const { return idle_seconds_; }
    Stage fade_pending() const { return fade_pending_; }
    // Fade overlay state changed (fade_pending() == None means hide).
    sigc::signal<void()>& signal_fade_changed() { return fade_changed_; }

    // Dev hooks: drive a stage as if the compositor reported the seat idle,
    // then resumed — without touching the real idle state.
    void simulate_idled(Stage stage);
    void simulate_resumed();
    static Stage stage_from_key(const std::string& key); // "screen_off" | "lock" | "suspend"

private:
    struct Monitor; // one ext_idle_notification_v1 + its callbacks

    Idle();

    static const char* stage_name(Stage stage);
    static void on_global(void* data, wl_registry* registry, uint32_t name,
                          const char* interface, uint32_t version);
    static void on_global_remove(void* data, wl_registry* registry, uint32_t name);
    static void on_monitor_idled(void* data, ext_idle_notification_v1* notification);
    static void on_monitor_resumed(void* data, ext_idle_notification_v1* notification);

    std::unique_ptr<Monitor> make_monitor(int timeout_s, std::function<void()> on_idled,
                                          std::function<void()> on_resumed);
    void apply_config();
    void set_monitor(std::unique_ptr<Monitor>& slot, Stage stage, int timeout_s);
    void ensure_heartbeat();
    void on_heartbeat_resumed();
    void apply_custom_monitors();

    bool stage_enabled(Stage stage) const;
    void on_idle(Stage stage);
    void cancel_fade();
    void restore_monitors();
    void execute_action(Stage stage);
    void run_next_queued_stage();
    void run_command(const std::string& command);
    void lock();
    void suspend();

    wl_registry* registry_ = nullptr;
    wl_seat* seat_ = nullptr;
    ext_idle_notifier_v1* notifier_ = nullptr;

    std::unique_ptr<Monitor> screen_off_, lock_, suspend_, heartbeat_;
    std::vector<std::unique_ptr<Monitor>> custom_;

    Stage fade_pending_ = Stage::None;
    std::vector<Stage> queued_;
    bool screen_off_active_ = false;
    bool dry_run_ = false;
    int idle_seconds_ = 0;

    sigc::connection grace_timer_;   // fade duration → execute
    sigc::connection cleanup_timer_; // 500ms after executing → clear fade, next stage
    sigc::connection idle_counter_;  // 1s ticks while the heartbeat says idle
    sigc::connection suspend_timer_; // lock-before-suspend delay
    sigc::signal<void()> fade_changed_;
};

} // namespace hyprshell
