#include "settings/about_page.hpp"

#include <nlohmann/json.hpp>

#include <sys/utsname.h>

#include <dirent.h>

#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace hyprshell::settings {
namespace {

#ifndef HS_VERSION
#define HS_VERSION "unknown"
#endif

std::string read_trimmed(const std::string& path) {
    gchar* data = nullptr;
    gsize len = 0;
    if (!g_file_get_contents(path.c_str(), &data, &len, nullptr))
        return "";
    std::string text(data, len);
    g_free(data);
    const auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
        return "";
    const auto end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

// DMI strings the firmware left unfilled.
bool dmi_placeholder(const std::string& value) {
    static const char* const kJunk[] = {"To be filled", "To Be Filled", "Default string",
                                        "System Product Name", "System manufacturer",
                                        "Not Specified", "Not Applicable", "None", "N/A"};
    if (value.empty())
        return true;
    for (const char* junk : kJunk)
        if (value.find(junk) != std::string::npos)
            return true;
    return false;
}

std::string regex_replace(const std::string& text, const char* pattern, const char* with) {
    GError* error = nullptr;
    GRegex* regex = g_regex_new(pattern, static_cast<GRegexCompileFlags>(0),
                                static_cast<GRegexMatchFlags>(0), &error);
    if (regex == nullptr) {
        g_warning("bad regex %s: %s", pattern, error->message);
        g_error_free(error);
        return text;
    }
    gchar* out = g_regex_replace(regex, text.c_str(), -1, 0, with,
                                 static_cast<GRegexMatchFlags>(0), nullptr);
    g_regex_unref(regex);
    if (out == nullptr)
        return text;
    std::string result(out);
    g_free(out);
    return result;
}

// GNOME's prettify_info: trademark glyphs, drop the marketing filler.
std::string prettify(std::string text) {
    struct Rule {
        const char* pattern;
        const char* with;
    };
    static const Rule kRules[] = {
        {"\\(R\\)", "®"},
        {"\\(TM\\)", "™"},
        {"\\(tm\\)", "™"},
        {"Intel Corporation", "Intel®"},
        {"NVIDIA Corporation", "NVIDIA"},
        {"Advanced Micro Devices, Inc\\. \\[AMD/ATI\\]", "AMD"},
        {"Advanced Micro Devices, Inc\\.", "AMD"},
        {"Advanced Micro Devices", "AMD"},
        {"Technologies, Inc\\.", ""},
        {"\\bProcessor\\b", ""},
        {"\\bCPU\\b", ""},
        {"\\s*@\\s*[0-9.]+\\s*[GM]Hz", ""},
        {"\\d+-Core", ""},
        {"\\s+", " "},
        {"^\\s+|\\s+$", ""},
    };
    for (const auto& rule : kRules)
        text = regex_replace(text, rule.pattern, rule.with);
    return text;
}

std::string device_name() {
    // hostnamectl's pretty name when set, else the static host name
    const std::string info = read_trimmed("/etc/machine-info");
    std::istringstream lines(info);
    for (std::string line; std::getline(lines, line);) {
        if (line.rfind("PRETTY_HOSTNAME=", 0) != 0)
            continue;
        std::string value = line.substr(strlen("PRETTY_HOSTNAME="));
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
            value = value.substr(1, value.size() - 2);
        if (!value.empty())
            return value;
    }
    return g_get_host_name();
}

std::string hardware_model() {
    const std::string vendor = read_trimmed("/sys/devices/virtual/dmi/id/sys_vendor");
    std::string model = read_trimmed("/sys/devices/virtual/dmi/id/product_family");
    if (dmi_placeholder(model))
        model = read_trimmed("/sys/devices/virtual/dmi/id/product_name");
    std::string text;
    if (!dmi_placeholder(vendor))
        text = vendor;
    if (!dmi_placeholder(model)) {
        if (!text.empty())
            text += ' ';
        text += model;
    }
    return text.empty() ? "Unknown" : prettify(text);
}

std::string memory_total() {
    std::ifstream meminfo("/proc/meminfo");
    for (std::string line; std::getline(meminfo, line);) {
        if (line.rfind("MemTotal:", 0) != 0)
            continue;
        const guint64 kib = g_ascii_strtoull(line.c_str() + strlen("MemTotal:"), nullptr, 10);
        gchar* text = g_format_size_full(kib * 1024, G_FORMAT_SIZE_IEC_UNITS);
        std::string result(text);
        g_free(text);
        return result;
    }
    return "Unknown";
}

std::string processor() {
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string name;
    for (std::string line; std::getline(cpuinfo, line);) {
        // x86 says "model name"; arm64 boards say "Hardware" / "Processor"
        if (line.rfind("model name", 0) == 0 || line.rfind("Hardware", 0) == 0) {
            const auto colon = line.find(':');
            if (colon != std::string::npos) {
                name = line.substr(colon + 1);
                break;
            }
        }
    }
    name = prettify(name);
    if (name.empty())
        name = "Unknown";
    return name + " × " + std::to_string(g_get_num_processors());
}

// pci.ids lookup (hwdata) for a vendor/device pair; empty when unknown.
std::pair<std::string, std::string> pci_names(const std::string& vendor_hex,
                                              const std::string& device_hex) {
    static const char* const kPaths[] = {"/usr/share/hwdata/pci.ids", "/usr/share/misc/pci.ids",
                                         "/usr/share/pci.ids"};
    for (const char* path : kPaths) {
        std::ifstream ids(path);
        if (!ids)
            continue;
        std::string vendor_name;
        bool in_vendor = false;
        for (std::string line; std::getline(ids, line);) {
            if (line.empty() || line[0] == '#')
                continue;
            if (line[0] != '\t') {
                in_vendor = line.rfind(vendor_hex, 0) == 0;
                if (in_vendor) {
                    const auto sp = line.find_first_not_of(" \t", vendor_hex.size());
                    vendor_name = sp == std::string::npos ? "" : line.substr(sp);
                } else if (!vendor_name.empty()) {
                    break; // left the vendor block
                }
            } else if (in_vendor && line[1] != '\t' && line.compare(1, 4, device_hex) == 0) {
                const auto sp = line.find_first_not_of(" \t", 5);
                return {vendor_name, sp == std::string::npos ? "" : line.substr(sp)};
            }
        }
        if (!vendor_name.empty())
            return {vendor_name, ""};
    }
    static const std::pair<const char*, const char*> kVendors[] = {
        {"8086", "Intel®"}, {"10de", "NVIDIA"}, {"1002", "AMD"}, {"1022", "AMD"}};
    for (const auto& [id, name] : kVendors)
        if (vendor_hex == id)
            return {name, ""};
    return {"", ""};
}

// "CometLake-H GT2 [UHD Graphics]" → "UHD Graphics (CometLake-H GT2)"
std::string reorder_bracket(const std::string& name) {
    const auto open = name.find('[');
    const auto close = name.rfind(']');
    if (open == std::string::npos || close == std::string::npos || close < open)
        return name;
    const std::string inner = name.substr(open + 1, close - open - 1);
    std::string outer = name.substr(0, open) + name.substr(close + 1);
    outer = regex_replace(outer, "^\\s+|\\s+$", "");
    if (inner.empty())
        return outer;
    return outer.empty() ? inner : inner + " (" + outer + ")";
}

std::vector<std::string> graphics() {
    std::vector<std::string> gpus;
    std::vector<std::string> seen;
    DIR* dir = opendir("/sys/class/drm");
    if (dir == nullptr)
        return gpus;
    std::vector<std::string> cards;
    while (dirent* entry = readdir(dir)) {
        const std::string name = entry->d_name;
        if (name.rfind("card", 0) == 0 && name.find('-') == std::string::npos)
            cards.push_back(name);
    }
    closedir(dir);
    std::sort(cards.begin(), cards.end());
    for (const std::string& card : cards) {
        const std::string device = "/sys/class/drm/" + card + "/device";
        gchar* real = realpath(device.c_str(), nullptr);
        if (real == nullptr)
            continue;
        const std::string key(real);
        free(real);
        if (std::find(seen.begin(), seen.end(), key) != seen.end())
            continue;
        seen.push_back(key);
        const std::string pci_class = read_trimmed(device + "/class");
        if (pci_class.rfind("0x03", 0) != 0) // display controllers only
            continue;
        std::string vendor = read_trimmed(device + "/vendor");
        std::string dev = read_trimmed(device + "/device");
        if (vendor.rfind("0x", 0) == 0)
            vendor = vendor.substr(2);
        if (dev.rfind("0x", 0) == 0)
            dev = dev.substr(2);
        auto [vendor_name, device_name] = pci_names(vendor, dev);
        std::string text;
        if (!vendor_name.empty())
            text = prettify(vendor_name);
        if (!device_name.empty()) {
            if (!text.empty())
                text += ' ';
            text += reorder_bracket(prettify(device_name));
        } else {
            text += (text.empty() ? "" : " ") + std::string("device ") + vendor + ":" + dev;
        }
        gpus.push_back(text);
    }
    return gpus;
}

std::string disk_capacity() {
    guint64 bytes = 0;
    DIR* dir = opendir("/sys/block");
    if (dir == nullptr)
        return "Unknown";
    while (dirent* entry = readdir(dir)) {
        const std::string name = entry->d_name;
        static const char* const kVirtual[] = {"loop", "zram", "ram", "dm-", "md", "sr", "fd", "."};
        bool skip = false;
        for (const char* prefix : kVirtual)
            if (name.rfind(prefix, 0) == 0)
                skip = true;
        if (skip)
            continue;
        const std::string base = "/sys/block/" + name;
        if (read_trimmed(base + "/removable") == "1")
            continue;
        bytes += g_ascii_strtoull(read_trimmed(base + "/size").c_str(), nullptr, 10) * 512;
    }
    closedir(dir);
    if (bytes == 0)
        return "Unknown";
    gchar* text = g_format_size(bytes);
    std::string result(text);
    g_free(text);
    return result;
}

std::string firmware_version() {
    const std::string version = read_trimmed("/sys/devices/virtual/dmi/id/bios_version");
    return dmi_placeholder(version) ? "" : version;
}

std::string os_name() {
    gchar* name = g_get_os_info(G_OS_INFO_KEY_PRETTY_NAME);
    if (name == nullptr)
        name = g_get_os_info(G_OS_INFO_KEY_NAME);
    std::string result = name != nullptr ? name : "Linux";
    g_free(name);
    return result;
}

std::string os_type() {
    return sizeof(void*) == 8 ? "64-bit" : "32-bit";
}

std::string kernel_version() {
    utsname uts{};
    if (uname(&uts) != 0)
        return "Unknown";
    return std::string(uts.sysname) + " " + uts.release;
}

std::string windowing_system() {
    GdkDisplay* display = gdk_display_get_default();
    if (display == nullptr)
        return "Unknown";
    const std::string type = G_OBJECT_TYPE_NAME(display);
    if (type.find("Wayland") != std::string::npos)
        return "Wayland";
    if (type.find("X11") != std::string::npos)
        return "X11";
    return type;
}

GtkWidget* property_row(GtkWidget* group, const char* title, const std::string& value) {
    GtkWidget* row = adw_action_row_new();
    gtk_widget_add_css_class(row, "property");
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    adw_action_row_set_subtitle(ADW_ACTION_ROW(row), value.c_str());
    adw_action_row_set_subtitle_selectable(ADW_ACTION_ROW(row), TRUE);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), row);
    return row;
}

// `hyprctl -j version` → the "version" field, delivered to the row later.
void fetch_hyprland_version(GtkWidget* row) {
    if (g_getenv("HYPRLAND_INSTANCE_SIGNATURE") == nullptr) {
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), "Not running");
        return;
    }
    GError* error = nullptr;
    GSubprocess* proc = g_subprocess_new(
        static_cast<GSubprocessFlags>(G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                      G_SUBPROCESS_FLAGS_STDERR_SILENCE),
        &error, "hyprctl", "-j", "version", nullptr);
    if (proc == nullptr) {
        g_debug("hyprctl not available: %s", error->message);
        g_error_free(error);
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), "Unknown");
        return;
    }
    g_subprocess_communicate_utf8_async(
        proc, nullptr, nullptr,
        +[](GObject* source, GAsyncResult* result, gpointer row_ptr) {
            GtkWidget* row = static_cast<GtkWidget*>(row_ptr);
            gchar* stdout_text = nullptr;
            std::string version = "Unknown";
            if (g_subprocess_communicate_utf8_finish(G_SUBPROCESS(source), result, &stdout_text,
                                                     nullptr, nullptr) &&
                stdout_text != nullptr) {
                const auto parsed = nlohmann::json::parse(stdout_text, nullptr, false);
                if (parsed.is_object()) {
                    if (parsed.contains("version") && parsed["version"].is_string())
                        version = parsed["version"].get<std::string>();
                    else if (parsed.contains("tag") && parsed["tag"].is_string())
                        version = parsed["tag"].get<std::string>();
                    if (parsed.value("dirty", false))
                        version += " (dirty)";
                }
            }
            g_free(stdout_text);
            adw_action_row_set_subtitle(ADW_ACTION_ROW(row), version.c_str());
            g_object_unref(row);
            g_object_unref(source);
        },
        g_object_ref(row));
}

void fill_about_page(GtkWidget* page) {
    GtkWidget* hardware = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(hardware), "System");
    property_row(hardware, "Device Name", device_name());
    property_row(hardware, "Hardware Model", hardware_model());
    property_row(hardware, "Memory", memory_total());
    property_row(hardware, "Processor", processor());
    const std::vector<std::string> gpus = graphics();
    if (gpus.size() == 1) {
        property_row(hardware, "Graphics", gpus.front());
    } else {
        for (size_t i = 0; i < gpus.size(); ++i) {
            const std::string title = "Graphics " + std::to_string(i + 1);
            property_row(hardware, title.c_str(), gpus[i]);
        }
    }
    property_row(hardware, "Disk Capacity", disk_capacity());
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(hardware));

    GtkWidget* software = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(software), "Software");
    const std::string firmware = firmware_version();
    if (!firmware.empty())
        property_row(software, "Firmware Version", firmware);
    property_row(software, "OS Name", os_name());
    property_row(software, "OS Type", os_type());
    property_row(software, "Kernel Version", kernel_version());
    property_row(software, "Windowing System", windowing_system());
    GtkWidget* hypr_row = property_row(software, "Hyprland Version", "…");
    fetch_hyprland_version(hypr_row);
    property_row(software, "hypr-shell Version", HS_VERSION);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(software));
}

} // namespace

GtkWidget* build_about_page() {
    GtkWidget* page = adw_preferences_page_new();
    // The facts are gathered after the first frame (pci.ids alone is a 1.4 MB
    // scan, ~40 ms): the page is rarely the one a launch is for, and the rows
    // exist long before anyone can type a search.
    g_idle_add_full(
        G_PRIORITY_LOW,
        +[](gpointer data) -> gboolean {
            fill_about_page(static_cast<GtkWidget*>(data));
            return G_SOURCE_REMOVE;
        },
        g_object_ref(page), g_object_unref);
    return page;
}

} // namespace hyprshell::settings
