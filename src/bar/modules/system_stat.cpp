#include "bar/modules/system_stat.hpp"

#include "services/config.hpp"
#include "services/system_stats.hpp"

#include <glibmm.h>

#include <cmath>
#include <iomanip>
#include <string>

namespace hyprshell {

namespace {

// noctalia-tabler-icons glyphs — the control center's system monitor uses the same three
constexpr const char* kCpuGlyph = "勺";     // cpu
constexpr const char* kMemoryGlyph = "";  // device-desktop-analytics (Noctalia's memory icon)
constexpr const char* kStorageGlyph = ""; // database

const char* css_class_for(SystemStatModule::Kind kind) {
    switch (kind) {
    case SystemStatModule::Kind::Cpu:
        return "cpu";
    case SystemStatModule::Kind::Memory:
        return "memory";
    case SystemStatModule::Kind::Disk:
        return "disk";
    }
    return "cpu";
}

const char* glyph_for(SystemStatModule::Kind kind) {
    switch (kind) {
    case SystemStatModule::Kind::Cpu:
        return kCpuGlyph;
    case SystemStatModule::Kind::Memory:
        return kMemoryGlyph;
    case SystemStatModule::Kind::Disk:
        return kStorageGlyph;
    }
    return kCpuGlyph;
}

const Config::StatModule& settings_for(SystemStatModule::Kind kind) {
    const auto& cfg = Config::get();
    switch (kind) {
    case SystemStatModule::Kind::Cpu:
        return cfg.cpu_module();
    case SystemStatModule::Kind::Memory:
        return cfg.memory_module();
    case SystemStatModule::Kind::Disk:
        return cfg.disk_module();
    }
    return cfg.cpu_module();
}

std::string percent_text(double value) {
    if (value < 0)
        return "…"; // no sample yet (CPU needs two)
    return std::to_string(static_cast<int>(std::lround(value))) + "%";
}

std::string gb(double value) {
    return Glib::ustring::format(std::fixed, std::setprecision(1), value) + " GB";
}

} // namespace

SystemStatModule::SystemStatModule(Kind kind) : Gtk::Box(Gtk::Orientation::HORIZONTAL, 4), kind_(kind) {
    add_css_class("module");
    add_css_class("sys-stat");
    add_css_class(css_class_for(kind));
    icon_.set_text(glyph_for(kind));
    icon_.add_css_class("icon");
    text_.add_css_class("stat-text");
    append(icon_);
    append(text_);

    // poll only while on a mapped bar — a disabled module is never parented,
    // and a hidden bar (bar.visibility = hidden) unmaps everything
    signal_map().connect([this] {
        if (!registered_) {
            registered_ = true;
            SystemStats::get().register_consumer();
            update();
        }
    });
    signal_unmap().connect([this] {
        if (registered_) {
            registered_ = false;
            SystemStats::get().unregister_consumer();
        }
    });

    SystemStats::get().signal_changed().connect(sigc::mem_fun(*this, &SystemStatModule::update));
    Config::get().signal_changed().connect(sigc::mem_fun(*this, &SystemStatModule::apply_config));
    apply_config();
}

SystemStatModule::~SystemStatModule() {
    if (registered_)
        SystemStats::get().unregister_consumer();
}

void SystemStatModule::apply_config() {
    const auto& cfg = Config::get();
    const auto& settings = settings_for(kind_);
    const bool vertical = cfg.bar_vertical();
    set_orientation(vertical ? Gtk::Orientation::VERTICAL : Gtk::Orientation::HORIZONTAL);
    set_spacing(vertical ? 0 : 4);
    text_.set_visible(settings.show_text);
    // order the two labels: text before (left / above) or after (right / below) the icon
    const bool text_first = settings.text_position == Config::StatModule::TextPosition::Before;
    if (text_first)
        reorder_child_after(icon_, text_);
    else
        reorder_child_after(text_, icon_);
    if (kind_ == Kind::Disk)
        SystemStats::get().set_disk_path(cfg.disk_path());
    update();
}

void SystemStatModule::update() {
    auto& stats = SystemStats::get();
    switch (kind_) {
    case Kind::Cpu: {
        const double usage = stats.cpu_usage();
        text_.set_text(percent_text(usage));
        std::string tip = "CPU: " + percent_text(usage);
        if (stats.cpu_temp() > 0)
            tip += "  ·  " + std::to_string(stats.cpu_temp()) + " °C";
        const auto& cores = stats.core_usage();
        for (std::size_t i = 0; i < cores.size(); ++i)
            tip += "\nCore " + std::to_string(i) + ": " + percent_text(cores[i]);
        set_tooltip_text(tip);
        break;
    }
    case Kind::Memory: {
        text_.set_text(percent_text(stats.mem_percent()));
        set_tooltip_text("Memory: " + gb(stats.mem_used_gb()) + " / " + gb(stats.mem_total_gb()) +
                         " (" + percent_text(stats.mem_percent()) + ")");
        break;
    }
    case Kind::Disk: {
        const int percent = stats.disk_percent();
        const std::string path = stats.disk_path();
        if (percent < 0) {
            text_.set_text("–");
            set_tooltip_text("Disk (" + path + "): not available");
        } else {
            text_.set_text(percent_text(percent));
            set_tooltip_text("Disk (" + path + "): " + gb(stats.disk_used_gb()) + " / " +
                             gb(stats.disk_total_gb()) + " (" + percent_text(percent) + ")");
        }
        break;
    }
    }
}

} // namespace hyprshell
