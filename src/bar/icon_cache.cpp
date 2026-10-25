#include "bar/icon_cache.hpp"

namespace hyprshell {

namespace {
constexpr int kPerIdle = 4; // ~20 ms of rasterizing per callback keeps the bar responsive
} // namespace

IconCache& IconCache::get() {
    static IconCache instance;
    return instance;
}

std::string IconCache::key(const Glib::RefPtr<Gio::Icon>& icon, int px) const {
    return icon->to_string() + "@" + std::to_string(px) + "x" + std::to_string(scale_);
}

Glib::RefPtr<Gdk::Paintable> IconCache::find(const Glib::RefPtr<Gio::Icon>& icon, int px) const {
    if (!icon)
        return {};
    auto it = cache_.find(key(icon, px));
    return it == cache_.end() ? Glib::RefPtr<Gdk::Paintable>() : it->second;
}

void IconCache::request(Gtk::Widget& context, const std::vector<Glib::RefPtr<Gio::Icon>>& icons,
                        int px) {
    theme_ = Gtk::IconTheme::get_for_display(context.get_display());
    scale_ = context.get_scale_factor();
    direction_ = context.get_direction();
    for (const auto& icon : icons) {
        if (!icon)
            continue;
        const std::string k = key(icon, px);
        if (cache_.count(k) || skipped_.count(k) || !queued_.insert(k).second)
            continue;
        queue_.push_back({icon, px});
    }
    if (!queue_.empty() && !idle_.connected())
        idle_ = Glib::signal_idle().connect(sigc::mem_fun(*this, &IconCache::work),
                                            Glib::PRIORITY_DEFAULT_IDLE);
}

bool IconCache::work() {
    bool rendered = false;
    for (int n = 0; n < kPerIdle && !queue_.empty(); ++n) {
        const Job job = std::move(queue_.front());
        queue_.pop_front();
        const std::string k = key(job.icon, job.px);
        queued_.erase(k);
        if (cache_.count(k) || skipped_.count(k))
            continue;
        auto icon = theme_->lookup_icon(job.icon, job.px, scale_, direction_, Gtk::IconLookupFlags());
        if (!icon || icon->is_symbolic()) {
            skipped_.insert(k);
            continue;
        }
        // snapshotting decodes the file and rasterizes it at px × scale; the
        // resulting paintable is a render node holding that texture
        auto snapshot = Gtk::Snapshot::create();
        icon->snapshot(snapshot, job.px, job.px);
        cache_[k] = snapshot->to_paintable(
            Gdk::Graphene::Size(static_cast<float>(job.px), static_cast<float>(job.px)));
        rendered = true;
    }
    if (rendered)
        rendered_.emit();
    return !queue_.empty();
}

} // namespace hyprshell
