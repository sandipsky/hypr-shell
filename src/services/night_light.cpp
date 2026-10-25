#include "services/night_light.hpp"

#include "services/config.hpp"

#include <algorithm>
#include <csignal>

namespace hyprshell {

namespace {

constexpr int kMaxCrashes = 5; // Noctalia's _maxCrashes
constexpr unsigned kRestartMs = 2000;

} // namespace

NightLight& NightLight::get() {
    static NightLight instance;
    return instance;
}

int NightLight::time_to_minutes(const std::string& time) {
    if (time.size() < 5 || time[2] != ':')
        return -1;
    const int h = std::atoi(time.substr(0, 2).c_str());
    const int m = std::atoi(time.substr(3, 2).c_str());
    if (h < 0 || h > 23 || m < 0 || m > 59)
        return -1;
    return h * 60 + m;
}

NightLight::NightLight() {
    dry_run_ = g_getenv("HS_NIGHT_LIGHT_DRY_RUN") != nullptr;
    const auto& cfg = Config::get().night_light();
    cfg_enabled_ = cfg.enabled;
    cfg_forced_ = cfg.forced;
    cfg_temp_ = cfg.night_temp;
    cfg_sunrise_ = cfg.manual_sunrise;
    cfg_sunset_ = cfg.manual_sunset;
    Config::get().signal_changed().connect(sigc::mem_fun(*this, &NightLight::on_config_changed));
    watch_resume();
    apply();
}

// -- config -------------------------------------------------------------------

void NightLight::on_config_changed() {
    const auto& cfg = Config::get().night_light();
    const bool relevant = cfg.enabled != cfg_enabled_ || cfg.forced != cfg_forced_ ||
                          cfg.manual_sunrise != cfg_sunrise_ || cfg.manual_sunset != cfg_sunset_;
    const bool temp_changed = cfg.night_temp != cfg_temp_;
    cfg_enabled_ = cfg.enabled;
    cfg_forced_ = cfg.forced;
    cfg_temp_ = cfg.night_temp;
    cfg_sunrise_ = cfg.manual_sunrise;
    cfg_sunset_ = cfg.manual_sunset;
    if (relevant) {
        temp_debounce_.disconnect();
        apply();
    } else if (temp_changed) {
        // the settings slider streams values while dragging — restart
        // hyprsunset once it settles
        temp_debounce_.disconnect();
        temp_debounce_ = Glib::signal_timeout().connect(
            [this] {
                apply();
                return false;
            },
            300);
    }
}

// -- schedule -----------------------------------------------------------------

std::pair<int, int> NightLight::schedule_times() const {
    const auto& cfg = Config::get().night_light();
    int sunrise = time_to_minutes(cfg.manual_sunrise);
    int sunset = time_to_minutes(cfg.manual_sunset);
    if (sunrise < 0)
        sunrise = 6 * 60 + 30;
    if (sunset < 0)
        sunset = 18 * 60 + 30;
    return {sunrise, sunset};
}

bool NightLight::is_night() const {
    const auto now = Glib::DateTime::create_now_local();
    const int now_min = now.get_hour() * 60 + now.get_minute();
    const auto [sunrise, sunset] = schedule_times();
    if (sunset < sunrise) // inverted: night is [sunset, sunrise)
        return now_min >= sunset && now_min < sunrise;
    return now_min >= sunset || now_min < sunrise; // normal: [sunset, midnight) ∪ [0, sunrise)
}

int NightLight::ms_until_next_boundary() const {
    const auto now = Glib::DateTime::create_now_local();
    const int now_min = now.get_hour() * 60 + now.get_minute();
    const auto [sunrise, sunset] = schedule_times();
    const int target = is_night() ? sunrise : sunset;
    int diff = target - now_min;
    if (diff <= 0)
        diff += 1440;
    return diff * 60 * 1000 - now.get_second() * 1000 - static_cast<int>(now.get_microsecond() / 1000);
}

void NightLight::apply(bool force) {
    const auto& cfg = Config::get().night_light();
    boundary_timer_.disconnect();
    if (!cfg.enabled) {
        restart_timer_.disconnect();
        crash_count_ = 0;
        night_phase_ = false;
        last_command_.clear();
        stop_process();
        return;
    }
    if (!cfg.forced) { // every non-forced mode is scheduled by us
        if (force)
            last_command_.clear(); // make apply_schedule() restart the process
        crash_count_ = force ? crash_count_ : 0;
        restart_timer_.disconnect();
        apply_schedule();
        return;
    }
    // forced: the night temperature right now, whatever the time
    night_phase_ = true;
    const std::vector<std::string> command = {"hyprsunset", "-t", std::to_string(cfg.night_temp)};
    if (force || command != last_command_ || !running_) {
        last_command_ = command;
        start_process(cfg.night_temp);
    }
}

void NightLight::apply_schedule() {
    const auto& cfg = Config::get().night_light();
    const bool night = is_night();
    night_phase_ = night;
    if (!night) {
        // day phase at the neutral 6500 K: no filter process needed
        last_command_.clear();
        stop_process();
        g_message("night light: day phase — hyprsunset stopped");
    } else {
        const std::vector<std::string> command = {"hyprsunset", "-t", std::to_string(cfg.night_temp)};
        if (command != last_command_ || !running_) {
            last_command_ = command;
            start_process(cfg.night_temp);
        }
        g_message("night light: night phase — hyprsunset at %d K", cfg.night_temp);
    }
    const int ms = std::max(ms_until_next_boundary(), 1000);
    boundary_timer_ = Glib::signal_timeout().connect(
        [this] {
            g_message("night light: schedule boundary reached");
            apply_schedule();
            return false;
        },
        static_cast<unsigned>(ms));
    g_message("night light: next boundary in %d s", ms / 1000);
}

// -- process ------------------------------------------------------------------

// Only one CTM manager may run per compositor ("A CTM manager is already
// running" and hyprsunset exits), so a stale hyprsunset/wlsunset — a previous
// shell's, or another shell's night light — is killed before ours starts.
void NightLight::start_process(int temperature) {
    stop_process();
    const unsigned generation = ++generation_;
    if (dry_run_) {
        g_message("night light (dry run): would run hyprsunset -t %d", temperature);
        running_ = true;
        changed_.emit();
        return;
    }
    // pkill returns before the victim has released the CTM, so wait (up to
    // 2 s) until no hyprsunset/wlsunset is left, then 300 ms more — Hyprland
    // frees the CTM slot a little after the client is gone, and a start inside
    // that window dies with "already running". Exit 0 = none was running,
    // 2 = killed and gone, 1 = still alive after the wait
    static const char* kKillScript =
        "pkill -x hyprsunset; a=$?; pkill -x wlsunset; b=$?; "
        "[ $a -ne 0 ] && [ $b -ne 0 ] && exit 0; "
        "for i in $(seq 1 40); do pgrep -x hyprsunset >/dev/null || pgrep -x wlsunset >/dev/null || { sleep 0.3; exit 2; }; "
        "sleep 0.05; done; exit 1";
    try {
        auto killer = Gio::Subprocess::create({"sh", "-c", kKillScript}, Gio::Subprocess::Flags::NONE);
        killer->wait_async(
            [this, killer, temperature, generation,
             alive = alive_](Glib::RefPtr<Gio::AsyncResult>& result) {
                if (!*alive)
                    return;
                try {
                    killer->wait_finish(result);
                } catch (const Glib::Error&) {
                }
                const int status = killer->get_if_exited() ? killer->get_exit_status() : -1;
                if (status == 2)
                    g_message("night light: killed a stale night light process");
                else if (status == 1)
                    g_warning("night light: a stale night light process is still running");
                spawn(temperature, generation);
            },
            {});
    } catch (const Glib::Error&) {
        spawn(temperature, generation);
    }
}

void NightLight::spawn(int temperature, unsigned generation) {
    if (generation != generation_)
        return; // stopped or restarted meanwhile
    try {
        proc_ = Gio::Subprocess::create({"hyprsunset", "-t", std::to_string(temperature)},
                                        Gio::Subprocess::Flags::NONE);
    } catch (const Glib::Error& e) {
        g_warning("night light: cannot start hyprsunset: %s", e.what());
        proc_.reset();
        running_ = false;
        changed_.emit();
        return;
    }
    running_ = true;
    g_message("night light: hyprsunset started at %d K", temperature);
    auto proc = proc_;
    proc->wait_async(
        [this, proc, alive = alive_](Glib::RefPtr<Gio::AsyncResult>& result) {
            if (!*alive)
                return;
            try {
                proc->wait_finish(result);
            } catch (const Glib::Error&) {
            }
            on_process_exited(proc);
        },
        {});
    changed_.emit();
}

void NightLight::stop_process() {
    ++generation_; // cancels a pending spawn
    if (proc_) {
        auto proc = proc_;
        proc_.reset(); // on_process_exited() sees it is no longer current
        proc->send_signal(SIGTERM);
    }
    if (running_) {
        running_ = false;
        changed_.emit();
    }
}

// Noctalia's runner.onExited: an unexpected exit while we want the filter is a
// crash — restart after 2 s, up to 5 times
void NightLight::on_process_exited(const Glib::RefPtr<Gio::Subprocess>& proc) {
    if (proc != proc_)
        return; // we stopped it ourselves
    proc_.reset();
    running_ = false;
    changed_.emit();
    const auto& cfg = Config::get().night_light();
    const bool wanted = cfg.enabled && (cfg.forced || night_phase_);
    if (!wanted) {
        g_message("night light: hyprsunset exited (%s)", cfg.enabled ? "day phase" : "disabled");
        crash_count_ = 0;
        return;
    }
    if (++crash_count_ > kMaxCrashes) {
        g_warning("night light: hyprsunset crashed %d times, giving up", kMaxCrashes);
        return;
    }
    g_warning("night light: hyprsunset exited unexpectedly, restarting in 2 s (attempt %d/%d)",
              crash_count_, kMaxCrashes);
    restart_timer_.disconnect();
    restart_timer_ = Glib::signal_timeout().connect(
        [this] {
            if (Config::get().night_light().enabled && !running_)
                apply(true);
            return false;
        },
        kRestartMs);
}

// -- resume -------------------------------------------------------------------

// Noctalia re-applies after a system resume (the gamma table is gone), then
// once more 2 s later in case the compositor was not ready yet.
void NightLight::watch_resume() {
    Gio::DBus::Connection::get(
        Gio::DBus::BusType::SYSTEM, [this, alive = alive_](Glib::RefPtr<Gio::AsyncResult>& result) {
            if (!*alive)
                return;
            try {
                system_bus_ = Gio::DBus::Connection::get_finish(result);
            } catch (const Glib::Error& e) {
                g_warning("night light: no system bus (%s) — no resume handling", e.what());
                return;
            }
            sleep_subscription_ = system_bus_->signal_subscribe(
                [this](const Glib::RefPtr<Gio::DBus::Connection>&, const Glib::ustring&,
                       const Glib::ustring&, const Glib::ustring&, const Glib::ustring&,
                       const Glib::VariantContainerBase& params) {
                    bool sleeping = true;
                    if (params.get_n_children() > 0) {
                        Glib::Variant<bool> v;
                        params.get_child(v, 0);
                        sleeping = v.get();
                    }
                    if (sleeping || !Config::get().night_light().enabled)
                        return;
                    g_message("night light: system resumed — re-applying");
                    apply(true);
                    resume_retry_.disconnect();
                    resume_retry_ = Glib::signal_timeout().connect(
                        [this] {
                            apply(true);
                            return false;
                        },
                        kRestartMs);
                },
                "org.freedesktop.login1", "org.freedesktop.login1.Manager", "PrepareForSleep",
                "/org/freedesktop/login1");
        });
}

} // namespace hyprshell
