#include "services/notifications.hpp"

#include "services/config.hpp"
#include "services/hyprland.hpp"
#include "services/pulse.hpp"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <nlohmann/json.hpp>
#include <pango/pango.h>

#include <algorithm>
#include <cstring>
#include <regex>

using json = nlohmann::json;

namespace hyprshell {

namespace {

constexpr int kMaxHistory = 100;   // Noctalia's maxHistory
constexpr int kMaxPopups = 5;      // Noctalia's maxPopups
constexpr unsigned kSaveDelayMs = 200;
constexpr unsigned kPopupTickMs = 50;    // Noctalia's progress timer
constexpr gint64 kMinSoundIntervalMs = 100; // sound spam rate limit
constexpr int kMaxImagePx = 96;    // image-data hints get downscaled to this

constexpr const char* kBusName = "org.freedesktop.Notifications";
constexpr const char* kObjectPath = "/org/freedesktop/Notifications";

constexpr const char* kIntrospectionXml = R"xml(
<node>
  <interface name='org.freedesktop.Notifications'>
    <method name='Notify'>
      <arg type='s' name='app_name' direction='in'/>
      <arg type='u' name='replaces_id' direction='in'/>
      <arg type='s' name='app_icon' direction='in'/>
      <arg type='s' name='summary' direction='in'/>
      <arg type='s' name='body' direction='in'/>
      <arg type='as' name='actions' direction='in'/>
      <arg type='a{sv}' name='hints' direction='in'/>
      <arg type='i' name='expire_timeout' direction='in'/>
      <arg type='u' name='id' direction='out'/>
    </method>
    <method name='CloseNotification'>
      <arg type='u' name='id' direction='in'/>
    </method>
    <method name='GetCapabilities'>
      <arg type='as' name='capabilities' direction='out'/>
    </method>
    <method name='GetServerInformation'>
      <arg type='s' name='name' direction='out'/>
      <arg type='s' name='vendor' direction='out'/>
      <arg type='s' name='version' direction='out'/>
      <arg type='s' name='spec_version' direction='out'/>
    </method>
    <signal name='NotificationClosed'>
      <arg type='u' name='id'/>
      <arg type='u' name='reason'/>
    </signal>
    <signal name='ActionInvoked'>
      <arg type='u' name='id'/>
      <arg type='s' name='action_key'/>
    </signal>
  </interface>
</node>)xml";

template <typename T>
T variant_child(const Glib::VariantContainerBase& tuple, gsize index) {
    return Glib::VariantBase::cast_dynamic<Glib::Variant<T>>(tuple.get_child(index)).get();
}

std::string escape_text(const std::string& text) {
    gchar* escaped = g_markup_escape_text(text.c_str(), -1);
    std::string result = escaped;
    g_free(escaped);
    return result;
}

std::string ascii_lower(std::string s) {
    for (auto& c : s)
        c = static_cast<char>(g_ascii_tolower(c));
    return s;
}

// Noctalia's processNotificationText, targeting Pango markup: <b>/<i>/<u>
// survive, <br> becomes a newline, every other tag is dropped (attributes
// included) and all remaining text is escaped. keep_tags=false strips even
// the allowed tags — the fallback when a sender's markup is unbalanced.
std::string sanitize_markup(const std::string& text, bool keep_tags) {
    std::string result;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t lt = text.find('<', pos);
        if (lt == std::string::npos) {
            result += escape_text(text.substr(pos));
            break;
        }
        result += escape_text(text.substr(pos, lt - pos));
        const std::size_t gt = text.find('>', lt);
        if (gt == std::string::npos) {
            result += escape_text(text.substr(lt));
            break;
        }
        std::string content = text.substr(lt + 1, gt - lt - 1);
        const bool closing = !content.empty() && content.front() == '/';
        if (closing)
            content.erase(0, 1);
        const std::string name =
            ascii_lower(content.substr(0, content.find_first_of(" \t/")));
        if (keep_tags && (name == "b" || name == "i" || name == "u"))
            result += closing ? "</" + name + ">" : "<" + name + ">";
        else if (name == "br" && !closing)
            result += "\n";
        pos = gt + 1;
    }
    return result;
}

std::string sanitize_notification_text(const std::string& text) {
    std::string result = sanitize_markup(text, /*keep_tags=*/true);
    // Unbalanced sender markup must not break the label — fall back to plain.
    if (!pango_parse_markup(result.c_str(), -1, 0, nullptr, nullptr, nullptr, nullptr))
        result = sanitize_markup(text, /*keep_tags=*/false);
    return result;
}

// Noctalia's getAppName: strip reverse-DNS prefixes, split camel case,
// drop app/desktop/flatpak suffixes, capitalize.
std::string prettify_app_name(std::string name) {
    // trim
    const auto begin = name.find_first_not_of(" \t");
    if (begin == std::string::npos)
        return "Unknown";
    name = name.substr(begin, name.find_last_not_of(" \t") - begin + 1);

    auto last_segment = [](const std::string& s, bool skip_numeric) {
        std::vector<std::string> parts;
        std::size_t start = 0;
        for (std::size_t dot; (dot = s.find('.', start)) != std::string::npos; start = dot + 1)
            parts.push_back(s.substr(start, dot - start));
        parts.push_back(s.substr(start));
        std::string part = parts.back();
        auto bad = [&](const std::string& p) {
            if (p.empty() || p == "app" || p == "desktop")
                return true;
            return skip_numeric &&
                   std::all_of(p.begin(), p.end(),
                               [](char c) { return g_ascii_isdigit(c); });
        };
        if (bad(part) && parts.size() >= 2)
            part = parts[parts.size() - 2];
        return part.empty() ? parts.front() : part;
    };

    if (name.find('.') != std::string::npos) {
        const bool reverse_dns = name.rfind("com.", 0) == 0 || name.rfind("org.", 0) == 0 ||
                                 name.rfind("io.", 0) == 0 || name.rfind("net.", 0) == 0;
        name = last_segment(name, /*skip_numeric=*/!reverse_dns);
    }

    if (name.empty())
        return "Unknown";
    std::string display;
    display += static_cast<char>(g_ascii_toupper(name[0]));
    for (std::size_t i = 1; i < name.size(); ++i) {
        if (g_ascii_islower(name[i - 1]) && g_ascii_isupper(name[i]))
            display += ' ';
        display += name[i];
    }
    for (const char* suffix : {"app", "desktop", "flatpak"}) {
        const std::size_t len = strlen(suffix);
        if (display.size() > len &&
            ascii_lower(display.substr(display.size() - len)) == suffix)
            display = display.substr(0, display.size() - len);
    }
    while (!display.empty() && display.back() == ' ')
        display.pop_back();
    return display.empty() ? "Unknown" : display;
}

std::string content_id(const std::string& summary, const std::string& body,
                       const std::string& app, gint64 time_ms) {
    const std::string key = summary + "\x1f" + body + "\x1f" + app + "\x1f" +
                            std::to_string(time_ms);
    gchar* sum = g_compute_checksum_for_string(G_CHECKSUM_SHA256, key.c_str(), -1);
    std::string result = sum;
    g_free(sum);
    return result;
}

void write_file_async(const std::string& path, std::string contents) {
    auto bytes = Glib::Bytes::create(contents.data(), contents.size());
    auto file = Gio::File::create_for_path(path);
    file->replace_contents_bytes_async(
        [file, bytes, path](const Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                std::string etag;
                file->replace_contents_finish(result, etag);
            } catch (const Glib::Error& e) {
                g_warning("notifications: writing %s failed: %s", path.c_str(),
                          e.what());
            }
        },
        bytes, /*etag=*/"", /*make_backup=*/false,
        Gio::File::CreateFlags::REPLACE_DESTINATION);
}

} // namespace

NotificationService& NotificationService::get() {
    static NotificationService instance;
    return instance;
}

NotificationService::NotificationService()
    : vtable_(sigc::mem_fun(*this, &NotificationService::on_method_call)) {
    const std::string cache_dir =
        Glib::build_filename(Glib::get_user_cache_dir(), "hypr-shell");
    state_path_ = Glib::build_filename(cache_dir, "notifications.json");
    image_dir_ = Glib::build_filename(cache_dir, "notifications");

    // Like Config's initial read: tiny local file, wanted before the first
    // frame so the unread badge doesn't flash. Saves are async + debounced.
    load_history();

    // Bundled default notification sound (Noctalia's asset), installed to
    // <datadir>/hypr-shell/sounds; ~/.local/share is g_get_user_data_dir().
    for (std::string dir : {std::string(Glib::get_user_data_dir())}) {
        const auto path =
            Glib::build_filename(dir, "hypr-shell", "sounds", "notification-generic.wav");
        if (Glib::file_test(path, Glib::FileTest::EXISTS)) {
            default_sound_ = path;
            break;
        }
    }
    if (default_sound_.empty())
        for (const auto& dir : Glib::get_system_data_dirs()) {
            const auto path = Glib::build_filename(dir, "hypr-shell", "sounds",
                                                   "notification-generic.wav");
            if (Glib::file_test(path, Glib::FileTest::EXISTS)) {
                default_sound_ = path;
                break;
            }
        }

    node_info_ = Gio::DBus::NodeInfo::create_for_xml(kIntrospectionXml);
    config_dnd_ = Config::get().notifications().do_not_disturb;
    do_not_disturb_ = config_dnd_;
    apply_enabled();
    Config::get().signal_changed().connect(
        sigc::mem_fun(*this, &NotificationService::apply_enabled));
}

// notifications.enabled, like Noctalia's updateNotificationServer(): disabling
// releases the bus name (another daemon can take it), enabling (re)claims it.
// Also adopts notifications.do_not_disturb on value-change edges only, so a
// runtime toggle (bell right click) survives unrelated config reloads.
void NotificationService::apply_enabled() {
    const bool config_dnd = Config::get().notifications().do_not_disturb;
    if (config_dnd != config_dnd_) {
        config_dnd_ = config_dnd;
        set_do_not_disturb(config_dnd);
    }

    const bool enabled = Config::get().notifications().enabled;
    if (enabled && own_name_id_ == 0) {
        own_name_id_ = Gio::DBus::own_name(
            Gio::DBus::BusType::SESSION, kBusName,
            sigc::mem_fun(*this, &NotificationService::on_bus_acquired),
            [this](const Glib::RefPtr<Gio::DBus::Connection>&, const Glib::ustring&) {
                available_ = true;
                g_message("notifications: acquired %s", kBusName);
                changed_.emit();
            },
            [this](const Glib::RefPtr<Gio::DBus::Connection>&, const Glib::ustring&) {
                if (!available_)
                    g_warning("notifications: %s is owned by another daemon "
                              "(mako/dunst/Noctalia?) — queued to take over when it exits",
                              kBusName);
                available_ = false;
                changed_.emit();
            });
    } else if (!enabled && own_name_id_ != 0) {
        Gio::DBus::unown_name(own_name_id_);
        own_name_id_ = 0;
        available_ = false;
        if (!popups_.empty()) {
            popups_.clear();
            popups_changed_.emit();
        }
        changed_.emit();
    }
}

void NotificationService::on_bus_acquired(
    const Glib::RefPtr<Gio::DBus::Connection>& connection, const Glib::ustring&) {
    connection_ = connection;
    if (registration_id_ != 0)
        return; // the object survives an unown/own cycle on the same connection
    try {
        registration_id_ = connection->register_object(
            kObjectPath, node_info_->lookup_interface(), vtable_);
    } catch (const Glib::Error& e) {
        g_warning("notifications: register_object failed: %s", e.what());
    }
}

void NotificationService::on_method_call(
    const Glib::RefPtr<Gio::DBus::Connection>&, const Glib::ustring&,
    const Glib::ustring&, const Glib::ustring&, const Glib::ustring& method_name,
    const Glib::VariantContainerBase& parameters,
    const Glib::RefPtr<Gio::DBus::MethodInvocation>& invocation) {
    if (method_name == "Notify") {
        handle_notify(parameters, invocation);
    } else if (method_name == "CloseNotification") {
        guint32 id = 0;
        try {
            id = variant_child<guint32>(parameters, 0);
        } catch (const std::exception&) {
        }
        invocation->return_value(Glib::VariantContainerBase());
        for (const auto& popup : popups_)
            if (popup.n.original_id == id) {
                dismiss_popup(popup.n.id);
                break;
            }
        if (connection_ && id != 0)
            connection_->emit_signal(
                kObjectPath, kBusName, "NotificationClosed", "",
                Glib::VariantContainerBase::create_tuple(
                    {Glib::Variant<guint32>::create(id),
                     Glib::Variant<guint32>::create(3)})); // 3 = closed by call
    } else if (method_name == "GetCapabilities") {
        const std::vector<Glib::ustring> caps = {"actions", "body", "body-markup",
                                                 "icon-static", "persistence"};
        invocation->return_value(Glib::VariantContainerBase::create_tuple(
            Glib::Variant<std::vector<Glib::ustring>>::create(caps)));
    } else if (method_name == "GetServerInformation") {
        invocation->return_value(Glib::VariantContainerBase::create_tuple(
            {Glib::Variant<Glib::ustring>::create("hypr-shell"),
             Glib::Variant<Glib::ustring>::create("hypr-shell"),
             Glib::Variant<Glib::ustring>::create("0.1.0"),
             Glib::Variant<Glib::ustring>::create("1.2")}));
    } else {
        invocation->return_error(Gio::DBus::Error(
            Gio::DBus::Error::UNKNOWN_METHOD, "unknown method " + method_name));
    }
}

void NotificationService::handle_notify(
    const Glib::VariantContainerBase& parameters,
    const Glib::RefPtr<Gio::DBus::MethodInvocation>& invocation) {
    Glib::ustring app_name, app_icon, summary, body;
    guint32 replaces_id = 0;
    std::vector<Glib::ustring> action_list;
    std::map<Glib::ustring, Glib::VariantBase> hints;
    gint32 expire_timeout = -1;
    try {
        app_name = variant_child<Glib::ustring>(parameters, 0);
        replaces_id = variant_child<guint32>(parameters, 1);
        app_icon = variant_child<Glib::ustring>(parameters, 2);
        summary = variant_child<Glib::ustring>(parameters, 3);
        body = variant_child<Glib::ustring>(parameters, 4);
        action_list = variant_child<std::vector<Glib::ustring>>(parameters, 5);
        hints = variant_child<std::map<Glib::ustring, Glib::VariantBase>>(parameters, 6);
        expire_timeout = variant_child<gint32>(parameters, 7);
    } catch (const std::exception&) {
        invocation->return_error(Gio::DBus::Error(Gio::DBus::Error::INVALID_ARGS,
                                                  "malformed Notify call"));
        return;
    }

    const guint32 id = replaces_id != 0 ? replaces_id : next_id_++;
    if (replaces_id >= next_id_)
        next_id_ = replaces_id + 1;
    invocation->return_value(Glib::VariantContainerBase::create_tuple(
        Glib::Variant<guint32>::create(id)));

    auto hint = [&hints](const char* a, const char* b = nullptr) {
        auto it = hints.find(a);
        if (it == hints.end() && b)
            it = hints.find(b);
        return it == hints.end() ? Glib::VariantBase() : it->second;
    };

    Notification n;
    n.original_id = id;
    n.timestamp_ms = g_get_real_time() / 1000;
    n.app_name = prettify_app_name(app_name.raw());
    n.summary = sanitize_notification_text(summary.raw());
    n.body = sanitize_notification_text(body.raw());
    n.id = content_id(n.summary, n.body, n.app_name, n.timestamp_ms);

    if (auto v = hint("urgency"); v && v.get_type_string() == "y")
        n.urgency = std::clamp<int>(
            Glib::VariantBase::cast_dynamic<Glib::Variant<guchar>>(v).get(), 0, 2);
    if (auto v = hint("desktop-entry"); v && v.get_type_string() == "s")
        n.desktop_entry =
            Glib::VariantBase::cast_dynamic<Glib::Variant<Glib::ustring>>(v).get();
    bool transient = false;
    if (auto v = hint("transient"); v) {
        if (v.get_type_string() == "b")
            transient = Glib::VariantBase::cast_dynamic<Glib::Variant<bool>>(v).get();
        else if (v.get_type_string() == "i")
            transient =
                Glib::VariantBase::cast_dynamic<Glib::Variant<gint32>>(v).get() != 0;
        else if (v.get_type_string() == "u")
            transient =
                Glib::VariantBase::cast_dynamic<Glib::Variant<guint32>>(v).get() != 0;
    }

    // actions arrive as a flat [key, text, key, text, ...] list
    for (std::size_t i = 0; i + 1 < action_list.size(); i += 2) {
        Action a;
        a.key = action_list[i].raw();
        a.text = action_list[i + 1].raw();
        if (a.text.empty())
            a.text = "Action";
        n.actions.push_back(std::move(a));
    }

    // image precedence per spec: image-data > image-path > app_icon
    Glib::VariantBase image_data = hint("image-data", "image_data");
    if (!image_data)
        image_data = hint("icon_data");
    if (image_data) {
        n.image = decode_image_data(image_data, n.id);
        n.image_from_cache = !n.image.empty();
    }
    if (n.image.empty()) {
        std::string path;
        if (auto v = hint("image-path", "image_path"); v && v.get_type_string() == "s")
            path = Glib::VariantBase::cast_dynamic<Glib::Variant<Glib::ustring>>(v).get();
        if (path.empty())
            path = app_icon.raw();
        if (path.rfind("file://", 0) == 0)
            path = path.substr(7);
        n.image = path; // absolute file path or themed icon name; "" = none
    }

    // Noctalia's handleNotification flow: rules → history → DND → popup.
    const auto& nc = Config::get().notifications();
    const std::string rule_action = evaluate_rules(n);
    if (rule_action == "block")
        return;

    const bool save = !transient && (n.urgency == 0   ? nc.save_low
                                     : n.urgency == 2 ? nc.save_critical
                                                      : nc.save_normal);
    if (save)
        add_to_history(n);
    if (rule_action == "hide")
        return;
    if (do_not_disturb_)
        return; // history recorded above, like Noctalia

    show_popup(n, expire_timeout, /*muted=*/rule_action == "mute");
}

// -- popups --------------------------------------------------------------------

gint64 NotificationService::popup_duration_ms(int urgency,
                                              gint32 expire_timeout_ms) const {
    const auto& nc = Config::get().notifications();
    if (nc.respect_expire_timeout) {
        if (expire_timeout_ms == 0)
            return -1; // sender says: never expire
        if (expire_timeout_ms > 0)
            return expire_timeout_ms;
    }
    const int seconds = urgency == 0   ? nc.low_duration_s
                        : urgency == 2 ? nc.critical_duration_s
                                       : nc.normal_duration_s;
    return static_cast<gint64>(seconds) * 1000;
}

void NotificationService::show_popup(const Notification& n, gint32 expire_timeout_ms,
                                     bool muted) {
    const gint64 now = g_get_monotonic_time() / 1000;

    // Replacement (replaces_id): update the popup in place, countdown kept.
    for (auto& popup : popups_) {
        if (popup.n.original_id == n.original_id) {
            popup.n = n;
            popup.duration_ms = popup_duration_ms(n.urgency, expire_timeout_ms);
            popups_changed_.emit();
            return; // no sound on replacement, like Noctalia
        }
    }

    // Duplicate content replaces the older popup (Noctalia's dedup).
    const auto duplicate = std::find_if(
        popups_.begin(), popups_.end(), [&](const Popup& popup) {
            return popup.n.summary == n.summary && popup.n.body == n.body &&
                   popup.n.app_name == n.app_name;
        });
    if (duplicate != popups_.end())
        popups_.erase(duplicate);

    Popup popup;
    popup.n = n;
    popup.start_ms = now;
    popup.duration_ms = popup_duration_ms(n.urgency, expire_timeout_ms);
    popups_.insert(popups_.begin(), std::move(popup));
    while (popups_.size() > kMaxPopups)
        popups_.pop_back(); // overflow leaves history alone
    ensure_popup_timer();
    popups_changed_.emit();

    if (!muted)
        play_sound(n);
}

void NotificationService::ensure_popup_timer() {
    if (popup_timer_.connected())
        return;
    popup_timer_ = Glib::signal_timeout().connect(
        sigc::mem_fun(*this, &NotificationService::on_popup_tick), kPopupTickMs);
}

bool NotificationService::on_popup_tick() {
    if (popups_.empty())
        return false; // timer stops; ensure_popup_timer restarts it
    const gint64 now = g_get_monotonic_time() / 1000;
    std::string expired;
    bool moved = false;
    for (auto& popup : popups_) {
        if (popup.duration_ms < 0 || popup.paused)
            continue;
        const gint64 elapsed = now - popup.start_ms;
        const double progress =
            std::max(1.0 - static_cast<double>(elapsed) / popup.duration_ms, 0.0);
        if (progress <= 0.0 && expired.empty())
            expired = popup.n.id; // one per tick, like Noctalia
        if (std::abs(popup.progress - progress) > 0.005) {
            popup.progress = progress;
            moved = true;
        }
    }
    if (moved)
        popup_progress_.emit();
    if (!expired.empty())
        dismiss_popup(expired);
    return !popups_.empty();
}

void NotificationService::dismiss_popup(const std::string& id) {
    const auto it = std::find_if(popups_.begin(), popups_.end(),
                                 [&](const Popup& popup) { return popup.n.id == id; });
    if (it == popups_.end())
        return;
    popups_.erase(it);
    popups_changed_.emit();
}

void NotificationService::pause_popup(const std::string& id) {
    for (auto& popup : popups_)
        if (popup.n.id == id && !popup.paused) {
            popup.paused = true;
            popup.pause_start_ms = g_get_monotonic_time() / 1000;
        }
}

void NotificationService::resume_popup(const std::string& id) {
    for (auto& popup : popups_)
        if (popup.n.id == id && popup.paused) {
            popup.start_ms += g_get_monotonic_time() / 1000 - popup.pause_start_ms;
            popup.paused = false;
        }
}

// Noctalia's playNotificationSound, via paplay (pipewire-pulse) instead of
// Qt multimedia: rate-limited, skips excluded apps and a muted output.
void NotificationService::play_sound(const Notification& n) {
    const auto& sounds = Config::get().notifications().sounds;
    if (!sounds.enabled)
        return;
    if (Pulse::get().available() && Pulse::get().muted())
        return;

    std::string app = ascii_lower(n.app_name);
    for (std::size_t start = 0; start < sounds.excluded_apps.size();) {
        auto end = sounds.excluded_apps.find(',', start);
        if (end == std::string::npos)
            end = sounds.excluded_apps.size();
        std::string entry = sounds.excluded_apps.substr(start, end - start);
        const auto from = entry.find_first_not_of(" \t");
        if (from != std::string::npos) {
            entry = ascii_lower(
                entry.substr(from, entry.find_last_not_of(" \t") - from + 1));
            if (entry == app)
                return;
        }
        start = end + 1;
    }

    std::string file = sounds.normal_file;
    if (sounds.separate)
        file = n.urgency == 0 ? sounds.low_file
               : n.urgency == 2 ? sounds.critical_file
                                : sounds.normal_file;
    if (file.empty())
        file = default_sound_;
    if (file.empty() || !Glib::file_test(file, Glib::FileTest::EXISTS))
        return;

    const gint64 now = g_get_monotonic_time() / 1000;
    if (now - last_sound_ms_ < kMinSoundIntervalMs)
        return;
    last_sound_ms_ = now;

    const auto volume =
        static_cast<int>(std::clamp(sounds.volume, 0.0, 1.0) * 65536.0);
    const std::vector<std::string> argv = {"paplay",
                                           "--volume=" + std::to_string(volume), file};
    try {
        Glib::spawn_async("", argv,
                          Glib::SpawnFlags::SEARCH_PATH |
                              Glib::SpawnFlags::STDOUT_TO_DEV_NULL |
                              Glib::SpawnFlags::STDERR_TO_DEV_NULL);
    } catch (const Glib::Error& e) {
        static bool warned = false;
        if (!warned) {
            g_warning("notifications: paplay failed (%s) — sounds disabled", e.what());
            warned = true;
        }
    }
}

// Noctalia's NotificationRulesService.evaluate: first matching rule wins;
// pattern is /regex/, a *glob* (case-insensitive) or a plain substring
// (case-insensitive), matched against "app summary body".
std::string NotificationService::evaluate_rules(const Notification& n) const {
    const auto& rules = Config::get().notifications().rules;
    if (rules.empty())
        return "";
    const std::string haystack = n.app_name + " " + n.summary + " " + n.body;
    const std::string lower_haystack = ascii_lower(haystack);
    for (const auto& rule : rules) {
        if (rule.pattern.empty())
            continue;
        bool matched = false;
        if (rule.pattern.size() >= 3 && rule.pattern.front() == '/' &&
            rule.pattern.back() == '/') {
            try {
                matched = std::regex_search(
                    haystack,
                    std::regex(rule.pattern.substr(1, rule.pattern.size() - 2)));
            } catch (const std::regex_error&) {
                g_warning("notifications: invalid rule regex %s", rule.pattern.c_str());
            }
        } else if (rule.pattern.find('*') != std::string::npos) {
            std::string expr;
            for (const char c : rule.pattern) {
                if (c == '*')
                    expr += ".*";
                else if (strchr(".+?^${}()|[]\\", c) != nullptr)
                    expr += std::string("\\") + c;
                else
                    expr += c;
            }
            try {
                matched = std::regex_search(haystack,
                                            std::regex(expr, std::regex::icase));
            } catch (const std::regex_error&) {
                matched = lower_haystack.find(ascii_lower(rule.pattern)) !=
                          std::string::npos;
            }
        } else {
            matched =
                lower_haystack.find(ascii_lower(rule.pattern)) != std::string::npos;
        }
        if (matched) {
            const std::string action = ascii_lower(rule.action);
            if (action == "mute")
                return "mute";
            if (action == "hide" || action == "silence")
                return "hide";
            return "block";
        }
    }
    return "";
}

std::string NotificationService::decode_image_data(const Glib::VariantBase& value,
                                                   const std::string& id) {
    if (value.get_type_string() != "(iiibiiay)")
        return "";
    try {
        auto tuple = Glib::VariantBase::cast_dynamic<Glib::VariantContainerBase>(value);
        const gint32 width = variant_child<gint32>(tuple, 0);
        const gint32 height = variant_child<gint32>(tuple, 1);
        const gint32 rowstride = variant_child<gint32>(tuple, 2);
        const bool has_alpha = variant_child<bool>(tuple, 3);
        const gint32 bits = variant_child<gint32>(tuple, 4);
        const gint32 channels = variant_child<gint32>(tuple, 5);
        const auto data = variant_child<std::vector<guint8>>(tuple, 6);
        if (bits != 8 || width <= 0 || height <= 0 || rowstride <= 0 ||
            channels != (has_alpha ? 4 : 3) ||
            data.size() < static_cast<std::size_t>(rowstride) * (height - 1) +
                              static_cast<std::size_t>(width) * channels)
            return "";

        GBytes* bytes = g_bytes_new(data.data(), data.size());
        GdkPixbuf* pixbuf =
            gdk_pixbuf_new_from_bytes(bytes, GDK_COLORSPACE_RGB, has_alpha, 8,
                                      width, height, rowstride);
        g_bytes_unref(bytes);
        if (!pixbuf)
            return "";
        if (width > kMaxImagePx || height > kMaxImagePx) {
            const double scale = std::min(1.0 * kMaxImagePx / width,
                                          1.0 * kMaxImagePx / height);
            GdkPixbuf* scaled = gdk_pixbuf_scale_simple(
                pixbuf, std::max(1, static_cast<int>(width * scale)),
                std::max(1, static_cast<int>(height * scale)), GDK_INTERP_BILINEAR);
            g_object_unref(pixbuf);
            if (!scaled)
                return "";
            pixbuf = scaled;
        }

        gchar* buffer = nullptr;
        gsize length = 0;
        GError* error = nullptr;
        const bool saved = gdk_pixbuf_save_to_buffer(pixbuf, &buffer, &length,
                                                     "png", &error, nullptr);
        g_object_unref(pixbuf);
        if (!saved) {
            g_warning("notifications: png encode failed: %s",
                      error ? error->message : "?");
            g_clear_error(&error);
            return "";
        }
        g_mkdir_with_parents(image_dir_.c_str(), 0755);
        const std::string path = Glib::build_filename(image_dir_, id + ".png");
        write_file_async(path, std::string(buffer, length));
        g_free(buffer);
        return path;
    } catch (const std::exception& e) {
        g_warning("notifications: bad image-data hint: %s", e.what());
        return "";
    }
}

void NotificationService::add_to_history(Notification n) {
    history_.insert(history_.begin(), std::move(n));
    while (history_.size() > kMaxHistory) {
        delete_cached_image(history_.back());
        history_.pop_back();
    }
    save_history();
    changed_.emit();
}

void NotificationService::delete_cached_image(const Notification& n) {
    // only files we wrote ourselves, never sender-provided paths
    if (!n.image_from_cache || n.image.rfind(image_dir_, 0) != 0)
        return;
    auto file = Gio::File::create_for_path(n.image);
    file->remove_async([file](const Glib::RefPtr<Gio::AsyncResult>& result) {
        try {
            file->remove_finish(result);
        } catch (const Glib::Error&) {
            // already gone — fine
        }
    }); // slot-only overload

}

void NotificationService::set_do_not_disturb(bool dnd) {
    if (do_not_disturb_ == dnd)
        return;
    do_not_disturb_ = dnd;
    changed_.emit();
}

void NotificationService::update_last_seen() {
    last_seen_ms_ = g_get_real_time() / 1000;
    save_history();
    changed_.emit();
}

int NotificationService::unread_count() const {
    int count = 0;
    for (const auto& n : history_)
        if (n.timestamp_ms > last_seen_ms_)
            ++count;
    return count;
}

void NotificationService::remove_from_history(const std::string& id) {
    const auto it = std::find_if(history_.begin(), history_.end(),
                                 [&](const Notification& n) { return n.id == id; });
    if (it == history_.end())
        return;
    delete_cached_image(*it);
    history_.erase(it);
    save_history();
    changed_.emit();
}

void NotificationService::clear_history() {
    for (const auto& n : history_)
        delete_cached_image(n);
    history_.clear();
    save_history();
    changed_.emit();
}

bool NotificationService::invoke_action(const std::string& id, const std::string& key) {
    guint32 original_id = 0;
    for (const auto& popup : popups_) // transient popups never reach history
        if (popup.n.id == id)
            original_id = popup.n.original_id;
    const auto it = std::find_if(history_.begin(), history_.end(),
                                 [&](const Notification& n) { return n.id == id; });
    if (original_id == 0 && it != history_.end())
        original_id = it->original_id;
    if (!connection_ || original_id == 0)
        return false;
    connection_->emit_signal(kObjectPath, kBusName, "ActionInvoked", "",
                             Glib::VariantContainerBase::create_tuple(
                                 {Glib::Variant<guint32>::create(original_id),
                                  Glib::Variant<Glib::ustring>::create(key)}));
    // actions are one shot, like Noctalia
    for (auto& popup : popups_)
        if (popup.n.id == id)
            popup.n.actions.clear();
    if (it != history_.end()) {
        it->actions.clear();
        save_history();
    }
    changed_.emit();
    return true;
}

void NotificationService::focus_sender_window(const std::string& app_name) {
    if (app_name.empty() || app_name == "Unknown")
        return;
    std::string normalized = ascii_lower(app_name);
    normalized.erase(std::remove(normalized.begin(), normalized.end(), ' '),
                     normalized.end());
    auto& hypr = Hyprland::get();
    if (!hypr.available())
        return;
    hypr.request("j/clients", [normalized](const std::string& reply) {
        json clients = json::parse(reply, nullptr, /*allow_exceptions=*/false);
        if (!clients.is_array())
            return;
        for (const auto& client : clients) {
            const std::string cls = ascii_lower(client.value("class", ""));
            if (cls.empty())
                continue;
            const auto dot = cls.rfind('.');
            const std::string last = dot == std::string::npos ? cls : cls.substr(dot + 1);
            if (cls == normalized || last == normalized ||
                cls.find(normalized) != std::string::npos ||
                (!last.empty() && normalized.find(last) != std::string::npos)) {
                Hyprland::get().focus_window(client.value("address", ""));
                return;
            }
        }
    });
}

// -- persistence --------------------------------------------------------------

void NotificationService::save_history() {
    save_timer_.disconnect();
    save_timer_ = Glib::signal_timeout().connect(
        [this] {
            perform_save_history();
            return false;
        },
        kSaveDelayMs);
}

void NotificationService::perform_save_history() {
    json root;
    root["last_seen_ts"] = last_seen_ms_;
    json items = json::array();
    for (const auto& n : history_) {
        json item;
        item["id"] = n.id;
        item["original_id"] = n.original_id;
        item["app_name"] = n.app_name;
        item["summary"] = n.summary;
        item["body"] = n.body;
        item["urgency"] = n.urgency;
        item["timestamp"] = n.timestamp_ms;
        item["image"] = n.image;
        item["image_from_cache"] = n.image_from_cache;
        item["desktop_entry"] = n.desktop_entry;
        json actions = json::array();
        for (const auto& a : n.actions)
            actions.push_back({{"key", a.key}, {"text", a.text}});
        item["actions"] = std::move(actions);
        items.push_back(std::move(item));
    }
    root["notifications"] = std::move(items);

    gchar* dir = g_path_get_dirname(state_path_.c_str());
    g_mkdir_with_parents(dir, 0755);
    g_free(dir);
    write_file_async(state_path_, root.dump());
}

void NotificationService::load_history() {
    std::string data;
    try {
        data = Glib::file_get_contents(state_path_);
    } catch (const Glib::Error&) {
        return; // no history yet
    }
    const json root = json::parse(data, nullptr, /*allow_exceptions=*/false);
    if (!root.is_object())
        return;
    try {
        last_seen_ms_ = root.value("last_seen_ts", static_cast<gint64>(0));
        const json items = root.value("notifications", json::array());
        for (const auto& item : items) {
            if (!item.is_object())
                continue;
            Notification n;
            n.id = item.value("id", "");
            n.original_id = item.value("original_id", 0u);
            n.app_name = item.value("app_name", "");
            n.summary = item.value("summary", "");
            n.body = item.value("body", "");
            n.urgency = std::clamp(item.value("urgency", 1), 0, 2);
            n.timestamp_ms = item.value("timestamp", static_cast<gint64>(0));
            n.image = item.value("image", "");
            n.image_from_cache = item.value("image_from_cache", false);
            n.desktop_entry = item.value("desktop_entry", "");
            for (const auto& a : item.value("actions", json::array()))
                if (a.is_object())
                    n.actions.push_back({a.value("key", ""), a.value("text", "")});
            if (n.original_id >= next_id_) // keep ActionInvoked ids unique
                next_id_ = n.original_id + 1;
            if (!n.id.empty())
                history_.push_back(std::move(n));
            if (history_.size() >= kMaxHistory)
                break;
        }
    } catch (const json::exception& e) {
        g_warning("notifications: %s: %s — history dropped", state_path_.c_str(),
                  e.what());
        history_.clear();
    }
}

} // namespace hyprshell
