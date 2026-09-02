#include "services/apps.hpp"

#include <giomm/desktopappinfo.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <map>

using json = nlohmann::json;

namespace hyprshell {

namespace {

std::string lowercase(const std::string& text) {
    std::string out = text;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return out;
}

std::string executable_basename(const Glib::RefPtr<Gio::AppInfo>& info) {
    std::string cmd = info->get_executable();
    if (cmd.empty())
        cmd = info->get_commandline();
    if (cmd.empty())
        return "";
    // strip a leading path and any arguments
    const auto space = cmd.find(' ');
    if (space != std::string::npos)
        cmd = cmd.substr(0, space);
    const auto slash = cmd.rfind('/');
    if (slash != std::string::npos)
        cmd = cmd.substr(slash + 1);
    return cmd;
}

} // namespace

Apps& Apps::get() {
    static Apps instance;
    return instance;
}

Apps::Apps() {
    pins_path_ = Glib::build_filename(Glib::get_user_cache_dir(), "hypr-shell",
                                      "pinned_apps.json");
    load_pins();
    reload();

    // fires when desktop entries change on disk (installs/uninstalls)
    monitor_ = Gio::AppInfoMonitor::get();
    monitor_->signal_changed().connect([this] {
        reload();
        changed_.emit();
    });
}

void Apps::reload() {
    entries_.clear();

    // Noctalia's duplicate handling: same desktop id with the same executable
    // is a true duplicate (skip), a different executable is a variant (keep).
    std::map<std::string, std::string> seen; // id -> exec
    for (const auto& info : Gio::AppInfo::get_all()) {
        if (!info || !info->should_show())
            continue;

        Entry entry;
        entry.id = info->get_id();
        entry.name = info->get_name();
        if (entry.id.empty())
            entry.id = entry.name;
        entry.exec_name = executable_basename(info);

        if (auto it = seen.find(entry.id); it != seen.end()) {
            if (it->second == entry.exec_name)
                continue; // true duplicate
            entry.id += "_" + entry.exec_name;
        }
        seen[entry.id] = entry.exec_name;

        auto desktop = std::dynamic_pointer_cast<Gio::DesktopAppInfo>(info);
        if (desktop)
            entry.description = desktop->get_generic_name();
        if (entry.description.empty())
            entry.description = info->get_description();
        entry.icon = info->get_icon();
        entry.info = info;
        entries_.push_back(std::move(entry));
    }
}

void Apps::launch(const Entry& entry) {
    try {
        entry.info->launch(std::vector<Glib::RefPtr<Gio::File>>());
    } catch (const Glib::Error& e) {
        g_warning("launch %s failed: %s", entry.id.c_str(), e.what());
    }
}

bool Apps::is_pinned(const std::string& id) const {
    const std::string id_lc = lowercase(id);
    return std::any_of(pinned_.begin(), pinned_.end(),
                       [&](const std::string& p) { return lowercase(p) == id_lc; });
}

void Apps::toggle_pinned(const std::string& id) {
    const std::string id_lc = lowercase(id);
    auto it = std::find_if(pinned_.begin(), pinned_.end(), [&](const std::string& p) {
        return lowercase(p) == id_lc;
    });
    if (it != pinned_.end())
        pinned_.erase(it);
    else
        pinned_.push_back(id);
    save_pins();
    changed_.emit();
}

void Apps::load_pins() {
    // synchronous like Config's initial read: tiny local file, read once
    std::string data;
    try {
        data = Glib::file_get_contents(pins_path_);
    } catch (const Glib::Error&) {
        return; // no pins yet
    }
    const json j = json::parse(data, nullptr, /*allow_exceptions=*/false);
    if (!j.is_array())
        return;
    for (const auto& entry : j)
        if (entry.is_string())
            pinned_.push_back(entry.get<std::string>());
}

void Apps::save_pins() {
    auto dir = Gio::File::create_for_path(Glib::path_get_dirname(pins_path_));
    try {
        dir->make_directory_with_parents();
    } catch (const Glib::Error&) {
        // exists
    }
    const auto data = std::make_shared<std::string>(json(pinned_).dump(2) + "\n");
    auto file = Gio::File::create_for_path(pins_path_);
    file->replace_contents_async(
        [file, data](Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                std::string etag;
                file->replace_contents_finish(result, etag);
            } catch (const Glib::Error& e) {
                g_warning("saving pinned apps failed: %s", e.what());
            }
        },
        *data, /*etag=*/"", /*make_backup=*/false, Gio::File::CreateFlags::NONE);
}

double fuzzy_score(const std::string& query_lc, const std::string& text) {
    if (query_lc.empty())
        return -1.0;
    const std::string text_lc = lowercase(text);
    if (text_lc.empty())
        return -1.0;

    if (text_lc == query_lc)
        return 1.0;
    if (text_lc.rfind(query_lc, 0) == 0)
        return 0.95;
    if (const auto pos = text_lc.find(query_lc); pos != std::string::npos) {
        // word-boundary substring beats a mid-word one
        const bool boundary = pos > 0 && !std::isalnum(static_cast<unsigned char>(
                                             text_lc[pos - 1]));
        const double base = boundary ? 0.85 : 0.70;
        return base - std::min(0.1, 0.005 * static_cast<double>(pos));
    }

    // subsequence: all query chars in order; compact spans score higher
    std::size_t first = std::string::npos, last = 0, qi = 0;
    for (std::size_t i = 0; i < text_lc.size() && qi < query_lc.size(); ++i) {
        if (text_lc[i] == query_lc[qi]) {
            if (first == std::string::npos)
                first = i;
            last = i;
            ++qi;
        }
    }
    if (qi != query_lc.size())
        return -1.0;
    const double span = static_cast<double>(last - first + 1);
    return 0.30 + 0.25 * (static_cast<double>(query_lc.size()) / span);
}

} // namespace hyprshell
