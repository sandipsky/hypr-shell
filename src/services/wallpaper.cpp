#include "services/wallpaper.hpp"

#include "services/config.hpp"
#include "services/wallpaper_files.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <functional>
#include <fstream>
#include <random>

namespace hyprshell {

namespace {

using json = nlohmann::json;

std::string lowercase(std::string s) {
    for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

Wallpaper& Wallpaper::get() {
    static Wallpaper instance;
    return instance;
}

Wallpaper::Wallpaper() {
    const auto dir = Glib::build_filename(Glib::get_user_cache_dir(), "hypr-shell");
    g_mkdir_with_parents(dir.c_str(), 0700);
    state_path_ = Glib::build_filename(dir, "wallpaper.json");

    // restore the last shown image (a tiny local file, read once like Config's)
    std::ifstream in(state_path_);
    if (in) {
        try {
            const json state = json::parse(in);
            current_ = state.value("current", "");
            if (auto used = state.find("used"); used != state.end() && used->is_array())
                for (const auto& entry : *used)
                    if (entry.is_string())
                        used_random_.push_back(entry.get<std::string>());
        } catch (const json::exception& e) {
            g_warning("wallpaper: invalid %s: %s", state_path_.c_str(), e.what());
        }
    }
    const auto& cfg = Config::get().wallpaper();
    config_current_ = cfg.current;
    slideshow_ = cfg.slideshow;
    order_ = static_cast<int>(cfg.slideshow_order);
    if (current_.empty() || !Glib::file_test(current_, Glib::FileTest::IS_REGULAR))
        current_ = config_current_;
    loaded_ = true;

    Config::get().signal_changed().connect(sigc::mem_fun(*this, &Wallpaper::apply_config));
    apply_config();
}

bool Wallpaper::is_image_file(const std::string& name) {
    return is_wallpaper_image(name);
}

void Wallpaper::apply_config() {
    const auto& cfg = Config::get().wallpaper();

    // the settings app's pick, adopted only when it changes (a slideshow pick
    // must survive unrelated config saves)
    if (cfg.current != config_current_) {
        config_current_ = cfg.current;
        if (!config_current_.empty())
            set_current(config_current_);
    }

    if (cfg.directory != directory_) {
        directory_ = cfg.directory;
        dir_monitor_.reset();
        if (!directory_.empty()) {
            try {
                dir_monitor_ = Gio::File::create_for_path(directory_)->monitor_directory();
                dir_monitor_->signal_changed().connect(
                    [this](const Glib::RefPtr<Gio::File>&, const Glib::RefPtr<Gio::File>&,
                           Gio::FileMonitor::Event) {
                        // editors write in bursts — coalesce
                        rescan_debounce_.disconnect();
                        rescan_debounce_ = Glib::signal_timeout().connect(
                            [this] {
                                scan();
                                return false;
                            },
                            300);
                    });
            } catch (const Glib::Error& e) {
                g_warning("wallpaper: cannot watch %s: %s", directory_.c_str(), e.what());
            }
        }
        scan();
    }
    restart_timer();

    // Noctalia: enabling the schedule or changing its order also changes the
    // wallpaper right away
    const bool order_changed = static_cast<int>(cfg.slideshow_order) != order_;
    order_ = static_cast<int>(cfg.slideshow_order);
    const bool turned_on = cfg.slideshow && !slideshow_;
    slideshow_ = cfg.slideshow;
    if (cfg.slideshow && (turned_on || order_changed))
        next();
}

// Async directory listing → sorted image paths; the first scan also picks a
// wallpaper when nothing is chosen yet, and the slideshow needs the list.
void Wallpaper::scan() {
    if (scan_cancellable_)
        scan_cancellable_->cancel();
    if (directory_.empty()) {
        images_.clear();
        images_changed_.emit();
        return;
    }
    scan_cancellable_ = Gio::Cancellable::create();
    auto cancellable = scan_cancellable_;
    auto dir = Gio::File::create_for_path(directory_);
    dir->enumerate_children_async(
        [this, dir, cancellable, alive = alive_](Glib::RefPtr<Gio::AsyncResult>& result) {
            if (!*alive || cancellable->is_cancelled())
                return;
            Glib::RefPtr<Gio::FileEnumerator> enumerator;
            try {
                enumerator = dir->enumerate_children_finish(result);
            } catch (const Glib::Error& e) {
                g_message("wallpaper: cannot list %s: %s", directory_.c_str(), e.what());
                images_.clear();
                images_changed_.emit();
                return;
            }
            auto found = std::make_shared<std::vector<std::string>>();
            // recursive lambda via shared_ptr so each batch schedules the next
            auto step = std::make_shared<std::function<void()>>();
            *step = [this, enumerator, found, step, cancellable, alive] {
                enumerator->next_files_async(
                    [this, enumerator, found, step, cancellable,
                     alive](Glib::RefPtr<Gio::AsyncResult>& res) {
                        if (!*alive || cancellable->is_cancelled())
                            return;
                        std::vector<Glib::RefPtr<Gio::FileInfo>> batch;
                        try {
                            batch = enumerator->next_files_finish(res);
                        } catch (const Glib::Error& e) {
                            g_warning("wallpaper: listing failed: %s", e.what());
                            return;
                        }
                        if (batch.empty()) {
                            try {
                                enumerator->close(); // local dir, nothing left to read
                            } catch (const Glib::Error&) {
                            }
                            std::sort(found->begin(), found->end(),
                                      [](const std::string& a, const std::string& b) {
                                          return lowercase(Glib::path_get_basename(a)) <
                                                 lowercase(Glib::path_get_basename(b));
                                      });
                            images_ = std::move(*found);
                            images_changed_.emit();
                            if (current_.empty() && !images_.empty())
                                set_current(Config::get().wallpaper().slideshow_order ==
                                                    Config::Wallpaper::Order::Random
                                                ? pick_next()
                                                : images_.front(),
                                            false);
                            return;
                        }
                        for (const auto& info : batch) {
                            if (info->get_file_type() != Gio::FileType::REGULAR || info->is_hidden())
                                continue;
                            if (is_image_file(info->get_name()))
                                found->push_back(Glib::build_filename(directory_, info->get_name()));
                        }
                        (*step)();
                    },
                    cancellable, 50);
            };
            (*step)();
        },
        cancellable, "standard::name,standard::type,standard::is-hidden");
}

void Wallpaper::set_current(const std::string& path, bool animate) {
    if (path.empty() || path == current_)
        return;
    const std::string previous = current_;
    current_ = path;
    save_state();
    restart_timer();
    current_changed_.emit(previous, animate);
}

// Noctalia's setAlphabeticalWallpaper / _pickUnusedRandom: alphabetical steps
// through the sorted list (wrapping); random is a shuffle bag — every image
// once before any repeats, never the same one twice in a row.
std::string Wallpaper::pick_next() {
    if (images_.empty())
        return {};
    if (Config::get().wallpaper().slideshow_order == Config::Wallpaper::Order::Alphabetical) {
        auto it = std::find(images_.begin(), images_.end(), current_);
        if (it == images_.end() || ++it == images_.end())
            return images_.front();
        return *it;
    }
    const auto in_list = [this](const std::string& path) {
        return std::find(images_.begin(), images_.end(), path) != images_.end();
    };
    std::erase_if(used_random_, [&](const std::string& p) { return !in_list(p); });
    const auto unused_of = [this](const std::vector<std::string>& used) {
        std::vector<std::string> unused;
        for (const auto& path : images_)
            if (std::find(used.begin(), used.end(), path) == used.end())
                unused.push_back(path);
        return unused;
    };
    auto unused = unused_of(used_random_);
    if (unused.empty()) {
        // bag exhausted: start over, keeping only the last pick out
        if (!used_random_.empty())
            used_random_ = {used_random_.back()};
        unused = unused_of(used_random_);
        if (unused.empty())
            unused = images_;
    }
    static std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<std::size_t> dist(0, unused.size() - 1);
    const auto pick = unused[dist(rng)];
    used_random_.push_back(pick);
    return pick;
}

void Wallpaper::next() {
    const auto pick = pick_next();
    if (!pick.empty())
        set_current(pick);
}

void Wallpaper::restart_timer() {
    timer_.disconnect();
    const auto& cfg = Config::get().wallpaper();
    if (!cfg.slideshow || directory_.empty())
        return;
    timer_ = Glib::signal_timeout().connect_seconds(
        [this] {
            next();
            return true;
        },
        static_cast<unsigned>(std::max(10, cfg.slideshow_interval_s)));
}

void Wallpaper::save_state() {
    if (!loaded_)
        return;
    const json state = {{"current", current_}, {"used", used_random_}};
    auto file = Gio::File::create_for_path(state_path_);
    auto contents = std::make_shared<std::string>(state.dump(2) + "\n");
    file->replace_contents_async(
        [file, contents](Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                file->replace_contents_finish(result);
            } catch (const Glib::Error& e) {
                g_warning("wallpaper: cannot save state: %s", e.what());
            }
        },
        *contents, std::string(), false, Gio::File::CreateFlags::PRIVATE);
}

} // namespace hyprshell
