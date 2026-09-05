#include "services/mpris.hpp"

#include <algorithm>
#include <cctype>

namespace hyprshell {

namespace {

constexpr const char* kPrefix = "org.mpris.MediaPlayer2.";
constexpr const char* kPath = "/org/mpris/MediaPlayer2";
constexpr const char* kPlayerIface = "org.mpris.MediaPlayer2.Player";
constexpr const char* kAppIface = "org.mpris.MediaPlayer2";

template <typename T>
bool get_typed(const Glib::VariantBase& v, T& out) {
    try {
        out = Glib::VariantBase::cast_dynamic<Glib::Variant<T>>(v).get();
        return true;
    } catch (const std::bad_cast&) {
        return false;
    }
}

std::string join_strings(const std::vector<Glib::ustring>& list) {
    std::string out;
    for (const auto& s : list) {
        if (!out.empty())
            out += ", ";
        out += s.raw();
    }
    return out;
}

} // namespace

Mpris& Mpris::get() {
    static Mpris instance;
    return instance;
}

Mpris::Mpris() {
    Gio::DBus::Connection::get(Gio::DBus::BusType::SESSION, sigc::mem_fun(*this, &Mpris::on_bus));
}

void Mpris::on_bus(Glib::RefPtr<Gio::AsyncResult>& result) {
    try {
        bus_ = Gio::DBus::Connection::get_finish(result);
    } catch (const Glib::Error& e) {
        g_warning("mpris: no session bus: %s", e.what());
        return;
    }
    // players come and go: NameOwnerChanged for org.mpris.MediaPlayer2.*
    owner_subscription_ = bus_->signal_subscribe(
        [this](const Glib::RefPtr<Gio::DBus::Connection>&, const Glib::ustring&, const Glib::ustring&,
               const Glib::ustring&, const Glib::ustring&, const Glib::VariantContainerBase& params) {
            Glib::Variant<Glib::ustring> name, old_owner, new_owner;
            params.get_child(name, 0);
            params.get_child(old_owner, 1);
            params.get_child(new_owner, 2);
            const std::string n = name.get().raw();
            if (n.rfind(kPrefix, 0) != 0)
                return;
            if (new_owner.get().empty())
                remove_player(n);
            else if (old_owner.get().empty())
                add_player(n);
        },
        "org.freedesktop.DBus", "org.freedesktop.DBus", "NameOwnerChanged", "/org/freedesktop/DBus");
    list_players();
}

void Mpris::list_players() {
    bus_->call(
        "/org/freedesktop/DBus", "org.freedesktop.DBus", "ListNames", {},
        [this, alive = alive_](Glib::RefPtr<Gio::AsyncResult>& result) {
            if (!*alive)
                return;
            try {
                auto reply = bus_->call_finish(result);
                Glib::Variant<std::vector<Glib::ustring>> names;
                reply.get_child(names, 0);
                for (const auto& name : names.get())
                    if (name.raw().rfind(kPrefix, 0) == 0)
                        add_player(name.raw());
            } catch (const Glib::Error& e) {
                g_warning("mpris: ListNames failed: %s", e.what());
            }
        },
        "org.freedesktop.DBus");
}

void Mpris::add_player(const std::string& bus_name) {
    if (players_.count(bus_name))
        return;
    auto entry = std::make_unique<Entry>();
    entry->player.bus_name = bus_name;
    // identity fallback: the name after the prefix, minus ".instanceNNN"
    std::string ident = bus_name.substr(std::string(kPrefix).size());
    if (auto dot = ident.find('.'); dot != std::string::npos)
        ident = ident.substr(0, dot);
    if (!ident.empty())
        ident[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(ident[0])));
    entry->player.identity = ident;
    Entry* raw = entry.get();
    players_[bus_name] = std::move(entry);
    order_.push_back(bus_name);

    Gio::DBus::Proxy::create(
        bus_, bus_name, kPath, kPlayerIface,
        [this, bus_name, raw, alive = alive_](Glib::RefPtr<Gio::AsyncResult>& result) {
            if (!*alive || !players_.count(bus_name))
                return;
            try {
                raw->proxy = Gio::DBus::Proxy::create_finish(result);
            } catch (const Glib::Error& e) {
                g_warning("mpris: proxy for %s failed: %s", bus_name.c_str(), e.what());
                remove_player(bus_name);
                return;
            }
            raw->proxy->signal_properties_changed().connect(
                [this, raw](const Gio::DBus::Proxy::MapChangedProperties& changed,
                            const std::vector<Glib::ustring>&) {
                    for (const auto& [key, value] : changed)
                        apply_property(*raw, key, value);
                    raw->player.last_update_us = g_get_monotonic_time();
                    changed_.emit();
                });
            raw->proxy->signal_signal().connect(
                [this, raw](const Glib::ustring&, const Glib::ustring& signal_name,
                            const Glib::VariantContainerBase& params) {
                    if (signal_name == "Seeked" && params.get_n_children() > 0) {
                        Glib::Variant<gint64> pos;
                        params.get_child(pos, 0);
                        raw->player.position_us = pos.get();
                        raw->player.last_update_us = g_get_monotonic_time();
                        changed_.emit();
                    }
                });
            read_properties(*raw);
        });
    Gio::DBus::Proxy::create(
        bus_, bus_name, kPath, kAppIface,
        [this, bus_name, raw, alive = alive_](Glib::RefPtr<Gio::AsyncResult>& result) {
            if (!*alive || !players_.count(bus_name))
                return;
            try {
                raw->app_proxy = Gio::DBus::Proxy::create_finish(result);
            } catch (const Glib::Error&) {
                return;
            }
            read_app_properties(*raw);
        });
}

void Mpris::remove_player(const std::string& bus_name) {
    players_.erase(bus_name);
    order_.erase(std::remove(order_.begin(), order_.end(), bus_name), order_.end());
    if (active_ == bus_name)
        active_.clear();
    if (players_.empty())
        poll_.disconnect();
    changed_.emit();
}

void Mpris::read_properties(Entry& entry) {
    for (const char* key : {"PlaybackStatus", "Metadata", "CanPlay", "CanPause", "CanGoNext", "CanGoPrevious",
                            "CanSeek", "CanControl", "Volume", "Position"}) {
        Glib::VariantBase value;
        entry.proxy->get_cached_property(value, key);
        if (value)
            apply_property(entry, key, value);
    }
    entry.player.last_update_us = g_get_monotonic_time();
    if (entry.player.playing())
        poll_position();
    changed_.emit();
}

void Mpris::read_app_properties(Entry& entry) {
    Glib::VariantBase value;
    entry.app_proxy->get_cached_property(value, "Identity");
    Glib::ustring identity;
    if (value && get_typed(value, identity) && !identity.empty())
        entry.player.identity = identity.raw();
    changed_.emit();
}

void Mpris::apply_property(Entry& entry, const Glib::ustring& key, const Glib::VariantBase& value) {
    auto& p = entry.player;
    if (key == "PlaybackStatus") {
        Glib::ustring s;
        if (get_typed(value, s))
            p.playback_status = s.raw();
        if (p.playing() && !poll_.connected())
            poll_position();
    } else if (key == "Metadata") {
        try {
            auto dict = Glib::VariantBase::cast_dynamic<Glib::Variant<std::map<Glib::ustring, Glib::VariantBase>>>(value).get();
            p.title = p.artist = p.album = p.art_url = p.track_id = "";
            p.length_us = 0;
            for (const auto& [k, v] : dict) {
                Glib::ustring s;
                std::vector<Glib::ustring> list;
                gint64 i64 = 0;
                if (k == "xesam:title" && get_typed(v, s))
                    p.title = s.raw();
                else if (k == "xesam:album" && get_typed(v, s))
                    p.album = s.raw();
                else if (k == "xesam:artist") {
                    if (get_typed(v, list))
                        p.artist = join_strings(list);
                    else if (get_typed(v, s))
                        p.artist = s.raw();
                } else if (k == "mpris:artUrl" && get_typed(v, s))
                    p.art_url = s.raw();
                else if (k == "mpris:length") {
                    if (get_typed(v, i64))
                        p.length_us = i64;
                    else {
                        guint64 u = 0;
                        if (get_typed(v, u))
                            p.length_us = static_cast<gint64>(u);
                    }
                } else if (k == "mpris:trackid") {
                    p.track_id = v.print(false).raw();
                    // objectpath prints as '/x/y'
                    if (p.track_id.size() >= 2 && p.track_id.front() == '\'')
                        p.track_id = p.track_id.substr(1, p.track_id.size() - 2);
                }
            }
        } catch (const std::bad_cast&) {
        }
    } else if (key == "Position") {
        gint64 pos = 0;
        if (get_typed(value, pos))
            p.position_us = pos;
    } else if (key == "Volume") {
        double v = 1.0;
        if (get_typed(value, v))
            p.volume = v;
    } else {
        bool b = false;
        if (!get_typed(value, b))
            return;
        if (key == "CanPlay")
            p.can_play = b;
        else if (key == "CanPause")
            p.can_pause = b;
        else if (key == "CanGoNext")
            p.can_go_next = b;
        else if (key == "CanGoPrevious")
            p.can_go_previous = b;
        else if (key == "CanSeek")
            p.can_seek = b;
        else if (key == "CanControl")
            p.can_control = b;
    }
}

void Mpris::register_consumer() {
    if (consumers_++ == 0) {
        fetch_positions();
        poll_position();
    }
}

void Mpris::unregister_consumer() {
    if (consumers_ > 0 && --consumers_ == 0)
        poll_.disconnect();
}

// Position is a plain property (no change notifications): re-read it every
// second for playing players while something displays it
void Mpris::poll_position() {
    if (poll_.connected() || consumers_ == 0)
        return;
    poll_ = Glib::signal_timeout().connect_seconds(
        [this] {
            fetch_positions();
            return consumers_ > 0 &&
                   std::any_of(players_.begin(), players_.end(),
                               [](const auto& e) { return e.second->proxy && e.second->player.playing(); });
        },
        1);
}

void Mpris::fetch_positions() {
    for (auto& [name, entry] : players_) {
        if (!entry->proxy || !entry->player.playing())
            continue;
        Entry* raw = entry.get();
        const std::string bus_name = name;
        bus_->call(
            kPath, "org.freedesktop.DBus.Properties", "Get",
            Glib::VariantContainerBase::create_tuple(
                {Glib::Variant<Glib::ustring>::create(kPlayerIface),
                 Glib::Variant<Glib::ustring>::create("Position")}),
            [this, raw, bus_name, alive = alive_](Glib::RefPtr<Gio::AsyncResult>& result) {
                if (!*alive || !players_.count(bus_name))
                    return;
                try {
                    auto reply = bus_->call_finish(result);
                    Glib::Variant<Glib::VariantBase> boxed;
                    reply.get_child(boxed, 0);
                    gint64 pos = 0;
                    if (get_typed(boxed.get(), pos)) {
                        raw->player.position_us = pos;
                        raw->player.last_update_us = g_get_monotonic_time();
                        changed_.emit();
                    }
                } catch (const Glib::Error&) {
                }
            },
            bus_name);
    }
}

const Mpris::Player* Mpris::player(const std::string& bus_name) const {
    const auto it = players_.find(bus_name);
    return it == players_.end() ? nullptr : &it->second->player;
}

const Mpris::Player* Mpris::active() const {
    if (!active_.empty())
        if (const auto* p = player(active_))
            return p;
    const Player* best = nullptr;
    for (const auto& name : order_) {
        const auto* p = player(name);
        if (!p)
            continue;
        if (p->playing())
            return p;
        if (!best || p->last_update_us > best->last_update_us)
            best = p;
    }
    return best;
}

void Mpris::set_active(const std::string& bus_name) {
    active_ = bus_name;
    changed_.emit();
}

gint64 Mpris::position(const std::string& bus_name) const {
    const auto* p = player(bus_name);
    if (!p)
        return 0;
    gint64 pos = p->position_us;
    if (p->playing() && p->last_update_us > 0)
        pos += g_get_monotonic_time() - p->last_update_us;
    if (p->length_us > 0)
        pos = std::clamp<gint64>(pos, 0, p->length_us);
    return std::max<gint64>(pos, 0);
}

void Mpris::call(const std::string& bus_name, const char* method, const Glib::VariantContainerBase& params) {
    const auto it = players_.find(bus_name);
    if (it == players_.end() || !it->second->proxy)
        return;
    it->second->proxy->call(method, [](Glib::RefPtr<Gio::AsyncResult>&) {}, params);
}

void Mpris::play_pause(const std::string& bus_name) { call(bus_name, "PlayPause"); }
void Mpris::next(const std::string& bus_name) { call(bus_name, "Next"); }
void Mpris::previous(const std::string& bus_name) { call(bus_name, "Previous"); }

void Mpris::seek_to(const std::string& bus_name, gint64 position_us) {
    const auto* p = player(bus_name);
    if (!p || p->track_id.empty())
        return;
    call(bus_name, "SetPosition",
         Glib::VariantContainerBase::create_tuple(
             {Glib::Variant<Glib::DBusObjectPathString>::create(p->track_id),
              Glib::Variant<gint64>::create(position_us)}));
    if (auto it = players_.find(bus_name); it != players_.end()) {
        it->second->player.position_us = position_us;
        it->second->player.last_update_us = g_get_monotonic_time();
    }
    changed_.emit();
}

void Mpris::set_volume(const std::string& bus_name, double volume) {
    const auto it = players_.find(bus_name);
    if (it == players_.end() || !it->second->proxy)
        return;
    bus_->call(kPath, "org.freedesktop.DBus.Properties", "Set",
               Glib::VariantContainerBase::create_tuple(
                   {Glib::Variant<Glib::ustring>::create(kPlayerIface),
                    Glib::Variant<Glib::ustring>::create("Volume"),
                    Glib::Variant<Glib::VariantBase>::create(Glib::Variant<double>::create(volume))}),
               [](Glib::RefPtr<Gio::AsyncResult>&) {}, bus_name);
}

} // namespace hyprshell
