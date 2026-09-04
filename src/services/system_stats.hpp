#pragma once

#include <sigc++/sigc++.h>

#include <string>
#include <vector>

namespace hyprshell {

// CPU usage / temperature, memory and disk usage for the control center's
// system monitor card (Noctalia's SystemStatService, the subset the card
// shows). Polls only while a consumer is registered (the card while its
// panel is open): CPU + temperature every 1 s, memory every 5 s, disk every
// 30 s, each read immediately on activation. /proc and sysfs reads are
// microsecond-fast, so they are done inline like Config's first read.
class SystemStats {
public:
    static SystemStats& get();

    SystemStats(const SystemStats&) = delete;
    SystemStats& operator=(const SystemStats&) = delete;

    void register_consumer();
    void unregister_consumer();

    double cpu_usage() const { return cpu_usage_; }     // percent, -1 until two samples exist
    int cpu_temp() const { return cpu_temp_; }          // °C, 0 = unavailable
    int mem_percent() const { return mem_percent_; }    // used = MemTotal - MemAvailable
    double mem_used_gb() const { return mem_used_gb_; } // GiB
    int disk_percent() const { return disk_percent_; }  // df-style, for disk_path()
    const std::string& disk_path() const { return disk_path_; }

    sigc::signal<void()>& signal_changed() { return changed_; }

private:
    SystemStats();
    void start();
    void stop();
    void sample_cpu();
    void sample_memory();
    void sample_disk();
    void probe_temperature_sensor();
    int read_temperature() const;

    int consumers_ = 0;
    sigc::connection cpu_timer_, mem_timer_, disk_timer_;

    // /proc/stat differential state
    unsigned long long prev_total_ = 0, prev_idle_ = 0;
    bool have_prev_ = false;
    double cpu_usage_ = -1;
    int cpu_temp_ = 0;
    int mem_percent_ = 0;
    double mem_used_gb_ = 0;
    int disk_percent_ = 0;
    std::string disk_path_ = "/";

    enum class Sensor { None, Coretemp, K10Temp, ThermalZones };
    Sensor sensor_ = Sensor::None;
    std::string sensor_dir_;
    std::vector<std::string> cpu_zones_;
    bool probed_ = false;
    sigc::signal<void()> changed_;
};

} // namespace hyprshell
