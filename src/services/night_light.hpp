#pragma once

#include <giomm.h>
#include <sigc++/sigc++.h>

#include <string>
#include <vector>

namespace hyprshell {

// Night light (Noctalia's NightLightService): runs `hyprsunset -t <K>` while
// it is night — or always, when forced — and stops it during the day (the
// day temperature is a fixed neutral 6500 K). hyprsunset has no scheduling
// of its own, so the service computes the sunrise/sunset boundaries itself
// from the configured times. Only one CTM manager may run per compositor, so
// every start first kills a stale hyprsunset/wlsunset (a previous shell, or
// another shell's daemon); crashes restart hyprsunset after 2 s (5 attempts)
// and a system resume re-applies it (logind PrepareForSleep).
class NightLight {
public:
    static NightLight& get();

    NightLight(const NightLight&) = delete;
    NightLight& operator=(const NightLight&) = delete;

    bool active() const { return running_; } // a filter process is up
    bool night() const { return night_phase_; }
    sigc::signal<void()>& signal_changed() { return changed_; }

    // "HH:MM" → minutes since midnight (-1 when malformed)
    static int time_to_minutes(const std::string& time);

private:
    NightLight();

    void on_config_changed();
    void apply(bool force = false);
    void apply_schedule();
    std::pair<int, int> schedule_times() const; // sunrise, sunset (minutes)
    bool is_night() const;
    int ms_until_next_boundary() const;
    void start_process(int temperature);
    void spawn(int temperature, unsigned generation);
    void stop_process();
    void on_process_exited(const Glib::RefPtr<Gio::Subprocess>& proc);
    void watch_resume();

    Glib::RefPtr<Gio::Subprocess> proc_;
    std::vector<std::string> last_command_;
    unsigned generation_ = 0; // bumps on every start/stop: a pending spawn checks it
    bool running_ = false;
    bool night_phase_ = false;
    int crash_count_ = 0;
    sigc::connection boundary_timer_;
    sigc::connection restart_timer_;
    sigc::connection resume_retry_;
    sigc::connection temp_debounce_;
    bool dry_run_ = false;

    // last seen config, to react only to relevant changes
    bool cfg_enabled_ = false, cfg_forced_ = false;
    int cfg_temp_ = 0;
    std::string cfg_sunrise_, cfg_sunset_;

    Glib::RefPtr<Gio::DBus::Connection> system_bus_;
    guint sleep_subscription_ = 0;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    sigc::signal<void()> changed_;
};

} // namespace hyprshell
