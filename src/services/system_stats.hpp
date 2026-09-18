#pragma once

#include <sigc++/sigc++.h>

#include <string>
#include <vector>

namespace hyprshell {

// CPU usage / temperature, memory and disk usage for the control center's
// system monitor card (Noctalia's SystemStatService, the subset the card
// shows) and the cpu / memory / disk bar modules. Polls only while a
// consumer is registered (the card while its panel is open, a bar module
// while it is on the bar): CPU + temperature every 1 s, memory every 5 s,
// disk every 30 s, each read immediately on activation. /proc and sysfs
// reads are microsecond-fast, so they are done inline like Config's first read.
class SystemStats {
public:
    static SystemStats& get();

    SystemStats(const SystemStats&) = delete;
    SystemStats& operator=(const SystemStats&) = delete;

    void register_consumer();
    void unregister_consumer();

    double cpu_usage() const { return cpu_usage_; }     // percent, -1 until two samples exist
    // per-core usage in /proc/stat order (cpu0, cpu1, …), same -1 rule
    const std::vector<double>& core_usage() const { return core_usage_; }
    int cpu_temp() const { return cpu_temp_; }          // °C, 0 = unavailable
    int mem_percent() const { return mem_percent_; }    // used = MemTotal - MemAvailable
    double mem_used_gb() const { return mem_used_gb_; }   // GiB
    double mem_total_gb() const { return mem_total_gb_; } // GiB
    int disk_percent() const { return disk_percent_; }  // df-style, for disk_path(); -1 = path unreadable
    double disk_used_gb() const { return disk_used_gb_; }   // GB (SI, like df -H)
    double disk_total_gb() const { return disk_total_gb_; } // GB
    const std::string& disk_path() const { return disk_path_; }
    // mount point (or any path on the filesystem) the disk figures describe;
    // bar.disk.path — resampled at once when it changes
    void set_disk_path(const std::string& path);

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

    // /proc/stat differential state (aggregate line, then one pair per core)
    unsigned long long prev_total_ = 0, prev_idle_ = 0;
    std::vector<std::pair<unsigned long long, unsigned long long>> prev_cores_;
    bool have_prev_ = false;
    double cpu_usage_ = -1;
    std::vector<double> core_usage_;
    int cpu_temp_ = 0;
    int mem_percent_ = 0;
    double mem_used_gb_ = 0;
    double mem_total_gb_ = 0;
    int disk_percent_ = 0;
    double disk_used_gb_ = 0;
    double disk_total_gb_ = 0;
    std::string disk_path_ = "/";

    enum class Sensor { None, Coretemp, K10Temp, ThermalZones };
    Sensor sensor_ = Sensor::None;
    std::string sensor_dir_;
    std::vector<std::string> cpu_zones_;
    bool probed_ = false;
    sigc::signal<void()> changed_;
};

} // namespace hyprshell
