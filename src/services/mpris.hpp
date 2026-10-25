#pragma once

#include <giomm.h>
#include <sigc++/sigc++.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace hyprshell {

// MPRIS media players on the session bus (org.mpris.MediaPlayer2.*): one
// proxy pair per player, metadata / playback status / capabilities from
// PropertiesChanged, plus the Seeked signal. Position has no change
// notification, so it is polled once a second — but only while a consumer
// (the control center's media card) is registered; position() extrapolates
// from the last known value in between. `active()` is the player shown.
class Mpris {
public:
    struct Player {
        std::string bus_name;
        std::string identity;      // "Chrome", "Spotify", ...
        std::string title, artist, album;
        std::string art_url;        // file:// or http(s)://
        gint64 length_us = 0;       // 0 = unknown
        gint64 position_us = 0;     // last known, extrapolated by the poll
        std::string playback_status; // Playing | Paused | Stopped
        bool can_play = false, can_pause = false, can_go_next = false, can_go_previous = false,
             can_seek = false, can_control = false;
        double volume = 1.0;
        std::string track_id;
        gint64 last_update_us = 0; // monotonic time of the last status/position update
        bool playing() const { return playback_status == "Playing"; }
    };

    static Mpris& get();

    Mpris(const Mpris&) = delete;
    Mpris& operator=(const Mpris&) = delete;

    bool available() const { return !players_.empty(); }
    const std::vector<std::string>& player_names() const { return order_; }
    const Player* player(const std::string& bus_name) const;
    // the player to show: the one playing, else the most recently active one
    const Player* active() const;
    void set_active(const std::string& bus_name); // user pick (kept while it exists)

    void play_pause(const std::string& bus_name);
    void next(const std::string& bus_name);
    void previous(const std::string& bus_name);
    void seek_to(const std::string& bus_name, gint64 position_us); // SetPosition
    void set_volume(const std::string& bus_name, double volume);

    // current playback position (extrapolated while playing)
    gint64 position(const std::string& bus_name) const;

    // something shows positions: poll them while at least one consumer is registered
    void register_consumer();
    void unregister_consumer();

    sigc::signal<void()>& signal_changed() { return changed_; }

private:
    struct Entry {
        Player player;
        Glib::RefPtr<Gio::DBus::Proxy> proxy;      // org.mpris.MediaPlayer2.Player
        Glib::RefPtr<Gio::DBus::Proxy> app_proxy;  // org.mpris.MediaPlayer2
    };

    Mpris();
    void on_bus(Glib::RefPtr<Gio::AsyncResult>& result);
    void list_players();
    void add_player(const std::string& bus_name);
    void remove_player(const std::string& bus_name);
    void read_properties(Entry& entry);
    void read_app_properties(Entry& entry);
    void apply_property(Entry& entry, const Glib::ustring& key, const Glib::VariantBase& value);
    void poll_position();
    void fetch_positions();
    void call(const std::string& bus_name, const char* method, const Glib::VariantContainerBase& params = {});

    Glib::RefPtr<Gio::DBus::Connection> bus_;
    guint owner_subscription_ = 0;
    std::map<std::string, std::unique_ptr<Entry>> players_;
    std::vector<std::string> order_; // insertion order
    std::string active_;             // user pick or auto
    int consumers_ = 0;
    sigc::connection poll_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    sigc::signal<void()> changed_;
};

} // namespace hyprshell
