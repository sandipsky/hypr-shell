#include "services/system_stats.hpp"

#include <glibmm.h>

#include <sys/statvfs.h>

#include <algorithm>
#include <cmath>
#include <sstream>

namespace hyprshell {

namespace {

bool read_file(const std::string& path, std::string& out) {
    try {
        out = Glib::file_get_contents(path);
        return true;
    } catch (const Glib::Error&) {
        return false;
    }
}

bool starts_with(const std::string& s, const char* prefix) {
    return s.rfind(prefix, 0) == 0;
}

bool ends_with(const std::string& s, const char* suffix) {
    const std::string suf = suffix;
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

} // namespace

SystemStats& SystemStats::get() {
    static SystemStats instance;
    return instance;
}

SystemStats::SystemStats() = default;

void SystemStats::register_consumer() {
    if (consumers_++ == 0)
        start();
}

void SystemStats::unregister_consumer() {
    if (consumers_ > 0 && --consumers_ == 0)
        stop();
}

void SystemStats::start() {
    if (!probed_)
        probe_temperature_sensor();
    have_prev_ = false; // Noctalia resets the differential state on (re)start
    sample_cpu();
    sample_memory();
    sample_disk();
    cpu_timer_ = Glib::signal_timeout().connect_seconds(
        [this] {
            sample_cpu();
            return true;
        },
        1);
    mem_timer_ = Glib::signal_timeout().connect_seconds(
        [this] {
            sample_memory();
            return true;
        },
        5);
    disk_timer_ = Glib::signal_timeout().connect_seconds(
        [this] {
            sample_disk();
            return true;
        },
        30);
}

void SystemStats::stop() {
    cpu_timer_.disconnect();
    mem_timer_.disconnect();
    disk_timer_.disconnect();
}

// /proc/stat "cpu" line: usage = (Δtotal - Δidle) / Δtotal, idle = idle + iowait
void SystemStats::sample_cpu() {
    std::string stat;
    if (read_file("/proc/stat", stat)) {
        std::istringstream in(stat);
        std::string label;
        unsigned long long f[10] = {};
        in >> label;
        for (auto& v : f)
            in >> v;
        const unsigned long long idle = f[3] + f[4];
        unsigned long long total = 0;
        for (auto v : f)
            total += v;
        if (have_prev_ && total > prev_total_) {
            const double d_total = static_cast<double>(total - prev_total_);
            const double d_idle = static_cast<double>(idle - prev_idle_);
            cpu_usage_ = std::round(((d_total - d_idle) / d_total) * 1000.0) / 10.0;
        }
        prev_total_ = total;
        prev_idle_ = idle;
        have_prev_ = true;
    }
    cpu_temp_ = read_temperature();
    changed_.emit();
}

void SystemStats::sample_memory() {
    std::string meminfo;
    if (!read_file("/proc/meminfo", meminfo))
        return;
    unsigned long long total = 0, available = 0;
    std::istringstream in(meminfo);
    std::string line;
    while (std::getline(in, line)) {
        if (starts_with(line, "MemTotal:"))
            total = std::stoull(line.substr(9));
        else if (starts_with(line, "MemAvailable:"))
            available = std::stoull(line.substr(13));
    }
    if (total == 0)
        return;
    const unsigned long long used = total > available ? total - available : 0;
    mem_used_gb_ = std::round(used / 1048576.0 * 10.0) / 10.0;
    mem_percent_ = static_cast<int>(std::round(used * 100.0 / total));
    changed_.emit();
}

// df's pcent: used / (used + available), rounded up
void SystemStats::sample_disk() {
    struct statvfs vfs{};
    if (statvfs(disk_path_.c_str(), &vfs) != 0)
        return;
    const double size = static_cast<double>(vfs.f_blocks) * vfs.f_frsize;
    const double avail = static_cast<double>(vfs.f_bavail) * vfs.f_frsize;
    const double free_root = static_cast<double>(vfs.f_bfree) * vfs.f_frsize;
    const double used = size - free_root;
    const double denom = used + avail;
    disk_percent_ = denom > 0 ? static_cast<int>(std::ceil(used * 100.0 / denom)) : 0;
    changed_.emit();
}

// Noctalia: hwmon coretemp / k10temp / zenpower, else cpu*thermal zones
void SystemStats::probe_temperature_sensor() {
    probed_ = true;
    for (int i = 0; i < 16; ++i) {
        const std::string dir = "/sys/class/hwmon/hwmon" + std::to_string(i);
        std::string name;
        if (!read_file(dir + "/name", name))
            continue;
        while (!name.empty() && (name.back() == '\n' || name.back() == ' '))
            name.pop_back();
        if (name == "coretemp") {
            sensor_ = Sensor::Coretemp;
            sensor_dir_ = dir;
            return;
        }
        if (name == "k10temp" || name == "zenpower") {
            sensor_ = Sensor::K10Temp;
            sensor_dir_ = dir;
            return;
        }
    }
    for (int i = 0; i < 20; ++i) {
        const std::string zone = "/sys/class/thermal/thermal_zone" + std::to_string(i);
        std::string type;
        if (!read_file(zone + "/type", type))
            continue;
        while (!type.empty() && (type.back() == '\n' || type.back() == ' '))
            type.pop_back();
        if (starts_with(type, "cpu") && ends_with(type, "thermal"))
            cpu_zones_.push_back(zone + "/temp");
    }
    if (!cpu_zones_.empty())
        sensor_ = Sensor::ThermalZones;
}

int SystemStats::read_temperature() const {
    std::string value;
    switch (sensor_) {
    case Sensor::K10Temp:
        if (read_file(sensor_dir_ + "/temp1_input", value))
            return static_cast<int>(std::round(std::atof(value.c_str()) / 1000.0));
        return 0;
    case Sensor::Coretemp: {
        double sum = 0;
        int count = 0;
        for (int i = 1; i <= 20; ++i)
            if (read_file(sensor_dir_ + "/temp" + std::to_string(i) + "_input", value)) {
                sum += std::atof(value.c_str()) / 1000.0;
                ++count;
            }
        return count > 0 ? static_cast<int>(std::round(sum / count)) : 0;
    }
    case Sensor::ThermalZones: {
        double hottest = 0;
        for (const auto& zone : cpu_zones_)
            if (read_file(zone, value))
                hottest = std::max(hottest, std::atof(value.c_str()) / 1000.0);
        return static_cast<int>(std::round(hottest));
    }
    case Sensor::None:
        return 0;
    }
    return 0;
}

} // namespace hyprshell
