#include "services/apps.hpp"

#include <giomm/desktopappinfo.h>
#include <glibmm/regex.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <map>
#include <string_view>

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
    lookup_cache_.clear();

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
        if (desktop) {
            entry.description = desktop->get_generic_name();
            entry.wm_class = desktop->get_startup_wm_class();
            if (desktop->has_key("Icon"))
                entry.icon_name = desktop->get_string("Icon");
        }
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
    const std::string key = normalize_app_id(id);
    return std::any_of(pinned_.begin(), pinned_.end(),
                       [&](const std::string& p) { return normalize_app_id(p) == key; });
}

void Apps::toggle_pinned(const std::string& id) {
    const std::string key = normalize_app_id(id);
    auto it = std::find_if(pinned_.begin(), pinned_.end(), [&](const std::string& p) {
        return normalize_app_id(p) == key;
    });
    if (it != pinned_.end())
        pinned_.erase(it);
    else
        pinned_.push_back(id);
    save_pins();
    changed_.emit();
}

void Apps::set_pinned(std::vector<std::string> ids) {
    if (ids == pinned_)
        return;
    pinned_ = std::move(ids);
    save_pins();
    changed_.emit();
}

const Apps::Entry* Apps::find_by_id(const std::string& id) const {
    const std::string key = normalize_app_id(id);
    if (key.empty())
        return nullptr;
    for (const auto& entry : entries_)
        if (normalize_app_id(entry.id) == key)
            return &entry;
    return nullptr;
}

// -- window class → desktop entry (Noctalia's ThemeIcons lookup chain) --------

namespace {

// Noctalia's manual overrides for tricky apps (ThemeIcons.substitutions)
const std::map<std::string, std::string> kSubstitutions = {
    {"code-url-handler", "visual-studio-code"},
    {"Code", "visual-studio-code"},
    {"gnome-tweaks", "org.gnome.tweaks"},
    {"pavucontrol-qt", "pavucontrol"},
    {"wps", "wps-office2019-kprometheus"},
    {"wpsoffice", "wps-office2019-kprometheus"},
    {"footclient", "foot"},
};

// ThemeIcons.regexSubstitutions
struct RegexSubstitution {
    Glib::RefPtr<Glib::Regex> regex;
    const char* replace;
};
const std::vector<RegexSubstitution>& regex_substitutions() {
    static const std::vector<RegexSubstitution> subs = {
        {Glib::Regex::create("^steam_app_(\\d+)$"), "steam_icon_\\1"},
        {Glib::Regex::create("Minecraft.*"), "minecraft-launcher"},
        {Glib::Regex::create(".*polkit.*"), "system-lock-screen"},
        {Glib::Regex::create("gcr.prompter"), "system-lock-screen"},
    };
    return subs;
}

std::string strip_separators(const std::string& text) {
    std::string out;
    for (char c : lowercase(text))
        if (c != '.' && c != '-' && c != '_')
            out += c;
    return out;
}

std::string reverse_domain_tail(const std::string& text) {
    const auto dot = text.rfind('.');
    return dot == std::string::npos ? text : text.substr(dot + 1);
}

} // namespace

std::string normalize_app_id(const std::string& id) {
    std::string out = id;
    const auto begin = out.find_first_not_of(" \t\n\r");
    if (begin == std::string::npos)
        return "";
    const auto end = out.find_last_not_of(" \t\n\r");
    out = lowercase(out.substr(begin, end - begin + 1));
    constexpr std::string_view kSuffix = ".desktop";
    if (out.size() > kSuffix.size() &&
        out.compare(out.size() - kSuffix.size(), kSuffix.size(), kSuffix) == 0)
        out.erase(out.size() - kSuffix.size());
    return out;
}

const Apps::Entry* Apps::lookup_for_class(const std::string& app_id) {
    if (app_id.empty())
        return nullptr;
    if (auto it = lookup_cache_.find(app_id); it != lookup_cache_.end())
        return it->second;
    const Entry* entry = find_app_entry(app_id, 0);
    lookup_cache_[app_id] = entry;
    return entry;
}

const Apps::Entry* Apps::find_app_entry(const std::string& str, int depth) {
    if (str.empty() || depth > 4)
        return nullptr;

    // checkHeuristic — Quickshell's DesktopEntries.heuristicLookup: the id
    // itself, then StartupWMClass, case-insensitively
    if (const Entry* e = find_by_id(str))
        return e;
    {
        const std::string lc = lowercase(str);
        for (const auto& entry : entries_)
            if (!entry.wm_class.empty() && lowercase(entry.wm_class) == lc)
                return &entry;
    }

    // checkSubstitutions
    {
        auto it = kSubstitutions.find(str);
        if (it == kSubstitutions.end())
            it = kSubstitutions.find(lowercase(str));
        if (it != kSubstitutions.end() && it->second != str)
            if (const Entry* e = find_app_entry(it->second, depth + 1))
                return e;
    }

    // checkRegex
    for (const auto& sub : regex_substitutions()) {
        const std::string replaced =
            sub.regex->replace(str.c_str(), 0, sub.replace, Glib::Regex::MatchFlags::DEFAULT);
        if (replaced != str)
            if (const Entry* e = find_app_entry(replaced, depth + 1))
                return e;
    }

    // checkSimpleTransforms
    {
        const std::string lc = lowercase(str);
        std::string hyphen_norm = lc, us_to_hyphen = lc, hyphen_to_us = lc;
        std::replace_if(hyphen_norm.begin(), hyphen_norm.end(),
                        [](unsigned char c) { return std::isspace(c); }, '-');
        std::replace(us_to_hyphen.begin(), us_to_hyphen.end(), '_', '-');
        std::replace(hyphen_to_us.begin(), hyphen_to_us.end(), '-', '_');
        const std::string tail = reverse_domain_tail(str);
        for (const auto& variant : {str, lc, tail, lowercase(tail), hyphen_norm,
                                    us_to_hyphen, hyphen_to_us})
            if (!variant.empty())
                if (const Entry* e = find_by_id(variant))
                    return e;
    }

    // checkFuzzySearch — ids first (with a -→_ retry), then icons, then names
    {
        const auto best = [this](const std::string& query,
                                 std::string Entry::*field) -> const Entry* {
            const std::string q = lowercase(query);
            const Entry* found = nullptr;
            double best_score = 0.0;
            for (const auto& entry : entries_) {
                // Noctalia's ids carry no ".desktop" — compare without it
                const double score = fuzzy_score(
                    q, field == &Entry::id ? normalize_app_id(entry.id) : entry.*field);
                if (score > best_score) {
                    best_score = score;
                    found = &entry;
                }
            }
            return found;
        };
        if (const Entry* e = best(str, &Entry::id))
            return e;
        std::string underscored = lowercase(str);
        std::replace(underscored.begin(), underscored.end(), '-', '_');
        if (underscored != str)
            if (const Entry* e = best(underscored, &Entry::id))
                return e;
        if (const Entry* e = best(str, &Entry::icon_name))
            return e;
        if (const Entry* e = best(str, &Entry::name))
            return e;
    }

    // checkCleanMatch — aggressive fallback: strip all separators
    if (str.size() > 3) {
        const std::string clean = strip_separators(str);
        for (const auto& entry : entries_) {
            const std::string clean_id = strip_separators(normalize_app_id(entry.id));
            if (clean_id.find(clean) != std::string::npos ||
                clean.find(clean_id) != std::string::npos)
                return &entry;
        }
    }
    return nullptr;
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
