// "Users" page — see users_page.hpp.
//
// Backend: AccountsService over GDBus (async calls, weak "alive" guard like
// the hotspot page). Polkit decides who may do what — our own name/picture
// need no prompt, passwords and other accounts prompt for the administrator
// password through the session's polkit agent (the prompt doubles as the
// "current password" check GNOME's own-password dialog performs itself).
// Refreshes come from the daemon's UserAdded / UserDeleted / User.Changed
// signals, so a change made elsewhere (useradd, GNOME Settings) shows up too.
#include "settings/users_page.hpp"

#include "settings/command.hpp"
#include "settings/login_page.hpp" // sddm_config_value()

#include <crypt.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib/gstdio.h>
#include <pwd.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace hyprshell::settings {
namespace {

constexpr const char* kAccountsName = "org.freedesktop.Accounts";
constexpr const char* kAccountsPath = "/org/freedesktop/Accounts";
constexpr const char* kAccountsIface = "org.freedesktop.Accounts";
constexpr const char* kUserIface = "org.freedesktop.Accounts.User";
constexpr const char* kDefaultFacesDir = "/usr/share/sddm/faces";
constexpr int kAvatarSavePx = 512; // GNOME's user panel saves 512x512 too
constexpr int kAvatarViewPx = 120;
constexpr int kAvatarRowPx = 32;
// AccountsService refuses icon files above 1 MiB (user.c); a noisy photo
// as PNG can exceed that, then it is saved as JPEG instead
constexpr gsize kMaxIconBytes = 1024 * 1024;
constexpr int kAccountAdmin = 1;

struct User {
    std::string path; // /org/freedesktop/Accounts/User<uid>
    std::string name;
    std::string real_name;
    std::string icon_file;
    std::string home;
    guint64 uid = 0;
    int account_type = 0; // 0 standard, 1 administrator
    bool system = false;
    bool local = true;
};

struct UserView;

struct UsersPage {
    GtkWindow* window = nullptr;
    GtkWidget* nav = nullptr;        // AdwNavigationView (returned)
    GtkWidget* root_stack = nullptr; // "loading" / "unavailable" / "page"
    GtkWidget* unavailable = nullptr;
    GDBusConnection* bus = nullptr;
    guint sub_added = 0, sub_deleted = 0, sub_changed = 0;
    guint refresh_source = 0;
    int pending_loads = 0;
    unsigned load_serial = 0;
    uid_t self_uid = getuid();
    std::string faces_dir; // SDDM's FacesDir, "" when SDDM is not installed
    std::vector<User> users;
    UserView* self_view = nullptr;
    UserView* other_view = nullptr; // the pushed page, if any
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};

// One user's page (root = ourselves, pushed = another account).
struct UserView {
    UsersPage* page = nullptr;
    std::string user_path;
    bool is_self = false;
    bool loading = false;
    GtkWidget* view = nullptr;     // AdwToolbarView (header + page)
    GtkWidget* nav_page = nullptr; // wraps `view` for pushed (other-user) pages
    AdwAvatar* avatar = nullptr;
    GtkWidget* avatar_remove = nullptr;
    GtkWidget* avatar_status = nullptr;
    GtkWidget* name_row = nullptr;
    GtkWidget* password_row = nullptr;
    GtkWidget* admin_row = nullptr;   // other users only
    GtkWidget* admin_switch = nullptr;
    GtkWidget* others_group = nullptr; // self only
    GtkWidget* others_list = nullptr;
    GtkWidget* add_group = nullptr;
    GtkWidget* remove_group = nullptr;
    GtkWidget* status = nullptr;
    std::vector<GtkWidget*> other_rows;
};

using CallDone = std::function<void(GVariant* result, const std::string& error)>;

// -- helpers ----------------------------------------------------------------

// A DBus error as a sentence for a status label.
std::string describe_error(GError* error) {
    if (error == nullptr)
        return "";
    gchar* remote = g_dbus_error_get_remote_error(error);
    std::string name = remote != nullptr ? remote : "";
    g_free(remote);
    g_dbus_error_strip_remote_error(error);
    if (name.find("PermissionDenied") != std::string::npos
        || name.find("NotAuthorized") != std::string::npos
        || name.find("Cancelled") != std::string::npos)
        return "Authorisation was not granted.";
    std::string msg = error->message != nullptr ? error->message : "Unknown error";
    if (!msg.empty() && msg.back() != '.')
        msg += '.';
    return msg;
}

// Fire-and-forget AccountsService call; `done` runs on the main loop unless
// the page is gone. ALLOW_INTERACTIVE_AUTHORIZATION + no timeout: a polkit
// prompt may sit for as long as the user needs.
void call(UsersPage* p, const std::string& path, const char* iface, const char* method,
          GVariant* params, CallDone done) {
    struct Ctx {
        std::weak_ptr<bool> alive;
        CallDone done;
    };
    auto* ctx = new Ctx{p->alive, std::move(done)};
    g_dbus_connection_call(
        p->bus, kAccountsName, path.c_str(), iface, method, params, nullptr,
        G_DBUS_CALL_FLAGS_ALLOW_INTERACTIVE_AUTHORIZATION, G_MAXINT, nullptr,
        [](GObject* source, GAsyncResult* result, gpointer data) {
            std::unique_ptr<Ctx> ctx(static_cast<Ctx*>(data));
            GError* error = nullptr;
            GVariant* reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
            if (!ctx->alive.expired() && ctx->done)
                ctx->done(reply, describe_error(error));
            g_clear_error(&error);
            if (reply != nullptr)
                g_variant_unref(reply);
        },
        ctx);
}

const User* find_user(const UsersPage* p, const std::string& path) {
    for (const auto& u : p->users)
        if (u.path == path)
            return &u;
    return nullptr;
}

std::string display_name(const User& u) { return u.real_name.empty() ? u.name : u.real_name; }

bool has_focus_within(GtkWidget* w) {
    return (gtk_widget_get_state_flags(w) & GTK_STATE_FLAG_FOCUS_WITHIN) != 0;
}

void set_status(GtkWidget* label, const std::string& text, bool error) {
    if (label == nullptr)
        return;
    gtk_label_set_text(GTK_LABEL(label), text.c_str());
    gtk_widget_set_visible(label, !text.empty());
    if (error)
        gtk_widget_add_css_class(label, "error");
    else
        gtk_widget_remove_css_class(label, "error");
}

GtkWidget* make_status_label() {
    GtkWidget* label = gtk_label_new("");
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_add_css_class(label, "dim-label");
    gtk_widget_add_css_class(label, "caption");
    gtk_widget_set_margin_top(label, 6);
    gtk_widget_set_visible(label, FALSE);
    return label;
}

// The picture a view shows for `u`: our own ~/.face first (what the shell
// reads), else what AccountsService has (readable copies live under
// /var/lib/AccountsService/icons).
std::string picture_path(const UsersPage* p, const User& u) {
    if (u.uid == p->self_uid) {
        gchar* face = g_build_filename(g_get_home_dir(), ".face", nullptr);
        std::string path = face;
        g_free(face);
        if (g_file_test(path.c_str(), G_FILE_TEST_IS_REGULAR))
            return path;
    }
    if (!u.icon_file.empty() && g_file_test(u.icon_file.c_str(), G_FILE_TEST_IS_REGULAR)
        && g_access(u.icon_file.c_str(), R_OK) == 0)
        return u.icon_file;
    return "";
}

void set_avatar_image(AdwAvatar* avatar, const std::string& path, const std::string& text) {
    adw_avatar_set_text(avatar, text.c_str());
    adw_avatar_set_show_initials(avatar, TRUE);
    GdkTexture* texture = path.empty() ? nullptr : gdk_texture_new_from_filename(path.c_str(), nullptr);
    adw_avatar_set_custom_image(avatar, texture != nullptr ? GDK_PAINTABLE(texture) : nullptr);
    if (texture != nullptr)
        g_object_unref(texture);
}

// -- username / password rules ----------------------------------------------

// useradd's default NAME_REGEX: [a-z_][a-z0-9_-]*[$]?, 32 chars; the '$'
// suffix (Samba machine accounts) is not offered.
bool username_valid(const std::string& s) {
    if (s.empty() || s.size() > 32)
        return false;
    if (!(g_ascii_islower(s[0]) || s[0] == '_'))
        return false;
    for (char c : s)
        if (!(g_ascii_islower(c) || g_ascii_isdigit(c) || c == '_' || c == '-'))
            return false;
    return true;
}

bool username_taken(const std::string& s) { return getpwnam(s.c_str()) != nullptr; }

// GECOS fields are ':'-separated and the real name is ','-separated inside.
bool real_name_valid(const std::string& s) {
    return !s.empty() && s.find(':') == std::string::npos && s.find(',') == std::string::npos
           && s.find('\n') == std::string::npos;
}

// First free username from a full name, GNOME's first proposals: the first
// word, then initial + last word, then the first word with a number.
std::string propose_username(const std::string& full_name) {
    gchar* lower = g_utf8_strdown(full_name.c_str(), -1);
    gchar* ascii = g_str_to_ascii(lower, nullptr);
    g_free(lower);
    std::vector<std::string> words;
    std::string word;
    for (const char* c = ascii; *c != '\0'; ++c) {
        if (g_ascii_isalnum(*c) || *c == '_' || *c == '-') {
            word += *c;
        } else if (!word.empty()) {
            words.push_back(word);
            word.clear();
        }
    }
    if (!word.empty())
        words.push_back(word);
    g_free(ascii);
    if (words.empty())
        return "";
    auto ok = [](const std::string& s) { return username_valid(s) && !username_taken(s); };
    std::vector<std::string> candidates{words.front()};
    if (words.size() > 1)
        candidates.push_back(words.front().substr(0, 1) + words.back());
    for (const auto& c : candidates)
        if (ok(c))
            return c;
    for (int i = 1; i < 100; ++i) {
        const std::string c = words.front() + std::to_string(i);
        if (ok(c))
            return c;
    }
    return "";
}

struct Strength {
    int level;        // 0..5 for the level bar
    const char* text; // GNOME's wording
};

Strength password_strength(const std::string& pw) {
    if (pw.empty())
        return {0, ""};
    bool lower = false, upper = false, digit = false, other = false;
    for (unsigned char c : pw) {
        if (g_ascii_islower(c))
            lower = true;
        else if (g_ascii_isupper(c))
            upper = true;
        else if (g_ascii_isdigit(c))
            digit = true;
        else
            other = true;
    }
    const int classes = lower + upper + digit + other;
    int score = 0;
    if (pw.size() >= 6)
        ++score;
    if (pw.size() >= 8)
        ++score;
    if (pw.size() >= 12)
        ++score;
    if (classes >= 2)
        ++score;
    if (classes >= 3 && pw.size() >= 8)
        ++score;
    static const char* kTexts[] = {"Strength: Weak", "Strength: Weak", "Strength: Fair",
                                   "Strength: Good", "Strength: Strong", "Strength: Very Strong"};
    return {score, kTexts[std::clamp(score, 0, 5)]};
}

// 12 characters from an unambiguous alphabet, from the kernel's CSPRNG.
std::string generate_password() {
    static const char kAlphabet[] = "abcdefghjkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    constexpr int kLength = 12;
    unsigned char bytes[kLength];
    if (getrandom(bytes, sizeof bytes, 0) != static_cast<ssize_t>(sizeof bytes))
        for (auto& b : bytes)
            b = static_cast<unsigned char>(g_random_int());
    std::string out;
    for (unsigned char b : bytes)
        out += kAlphabet[b % (sizeof(kAlphabet) - 1)];
    return out;
}

// crypt(3) hash for AccountsService's SetPassword: yescrypt ($y$, libxcrypt
// >= 4.3, Arch's default in /etc/login.defs), "" on failure.
std::string crypt_password(const std::string& password) {
    char* salt = crypt_gensalt_ra("$y$", 0, nullptr, 0);
    if (salt == nullptr)
        return "";
    auto data = std::make_unique<struct crypt_data>();
    const char* hash = crypt_r(password.c_str(), salt, data.get());
    free(salt);
    return (hash != nullptr && hash[0] == '$') ? hash : "";
}

// -- picture files ----------------------------------------------------------

// Decode `source` at a size whose shorter side is kAvatarSavePx, centre-crop
// it square and write it to `dest` (PNG, JPEG if that would exceed the
// daemon's 1 MiB limit). Returns an error sentence, "" on success.
std::string save_avatar(const std::string& source, const std::string& dest) {
    int width = 0, height = 0;
    if (gdk_pixbuf_get_file_info(source.c_str(), &width, &height) == nullptr || width <= 0 || height <= 0)
        return "The file is not an image that can be opened.";
    GError* error = nullptr;
    // shorter side → kAvatarSavePx (upscaling a tiny picture is fine, GNOME does too)
    const double scale = static_cast<double>(kAvatarSavePx) / std::min(width, height);
    const int w = std::max(kAvatarSavePx, static_cast<int>(std::lround(width * scale)));
    const int h = std::max(kAvatarSavePx, static_cast<int>(std::lround(height * scale)));
    GdkPixbuf* loaded = gdk_pixbuf_new_from_file_at_scale(source.c_str(), w, h, FALSE, &error);
    if (loaded == nullptr) {
        std::string why = error != nullptr ? error->message : "decode failed";
        g_clear_error(&error);
        return "Could not open the image: " + why + ".";
    }
    GdkPixbuf* oriented = gdk_pixbuf_apply_embedded_orientation(loaded);
    g_object_unref(loaded);
    const int ow = gdk_pixbuf_get_width(oriented);
    const int oh = gdk_pixbuf_get_height(oriented);
    const int side = std::min({ow, oh, kAvatarSavePx});
    GdkPixbuf* cropped = gdk_pixbuf_new_subpixbuf(oriented, (ow - side) / 2, (oh - side) / 2, side, side);
    g_object_unref(oriented);
    GdkPixbuf* square = cropped;
    if (side != kAvatarSavePx) {
        square = gdk_pixbuf_scale_simple(cropped, kAvatarSavePx, kAvatarSavePx, GDK_INTERP_HYPER);
        g_object_unref(cropped);
    }
    gchar* buffer = nullptr;
    gsize length = 0;
    bool ok = gdk_pixbuf_save_to_buffer(square, &buffer, &length, "png", &error, nullptr);
    if (ok && length > kMaxIconBytes) {
        g_free(buffer);
        buffer = nullptr;
        ok = gdk_pixbuf_save_to_buffer(square, &buffer, &length, "jpeg", &error, "quality", "90", nullptr);
    }
    g_object_unref(square);
    if (!ok) {
        std::string why = error != nullptr ? error->message : "encode failed";
        g_clear_error(&error);
        g_free(buffer);
        return "Could not encode the picture: " + why + ".";
    }
    ok = g_file_set_contents_full(dest.c_str(), buffer, static_cast<gssize>(length),
                                  G_FILE_SET_CONTENTS_CONSISTENT, 0644, &error);
    g_free(buffer);
    if (!ok) {
        std::string why = error != nullptr ? error->message : "write failed";
        g_clear_error(&error);
        return "Could not write " + dest + ": " + why;
    }
    return "";
}

std::string cache_file(const std::string& name) {
    gchar* dir = g_build_filename(g_get_user_cache_dir(), "hypr-shell", nullptr);
    g_mkdir_with_parents(dir, 0755);
    gchar* path = g_build_filename(dir, name.c_str(), nullptr);
    std::string out = path;
    g_free(path);
    g_free(dir);
    return out;
}

std::string sddm_face_path(const UsersPage* p, const User& u) {
    return p->faces_dir.empty() ? "" : p->faces_dir + "/" + u.name + ".face.icon";
}

// -- data -------------------------------------------------------------------

void rebuild_views(UsersPage* p);
void show_unavailable(UsersPage* p, const std::string& why);

void user_from_properties(User& u, GVariant* dict) {
    const char* s = nullptr;
    if (g_variant_lookup(dict, "UserName", "&s", &s))
        u.name = s;
    if (g_variant_lookup(dict, "RealName", "&s", &s))
        u.real_name = s;
    if (g_variant_lookup(dict, "IconFile", "&s", &s))
        u.icon_file = s;
    if (g_variant_lookup(dict, "HomeDirectory", "&s", &s))
        u.home = s;
    g_variant_lookup(dict, "Uid", "t", &u.uid);
    g_variant_lookup(dict, "AccountType", "i", &u.account_type);
    gboolean b = FALSE;
    if (g_variant_lookup(dict, "SystemAccount", "b", &b))
        u.system = b;
    if (g_variant_lookup(dict, "LocalAccount", "b", &b))
        u.local = b;
}

void load_users(UsersPage* p) {
    const unsigned serial = ++p->load_serial;
    // ListCachedUsers only knows accounts that logged in once; ours is
    // fetched by uid as well so the root page never comes up empty.
    call(p, kAccountsPath, kAccountsIface, "ListCachedUsers", nullptr,
         [p, serial](GVariant* result, const std::string& error) {
             if (serial != p->load_serial)
                 return;
             if (result == nullptr) {
                 show_unavailable(p, error);
                 return;
             }
             std::vector<std::string> paths;
             GVariant* array = g_variant_get_child_value(result, 0);
             GVariantIter it;
             g_variant_iter_init(&it, array);
             const char* path = nullptr;
             while (g_variant_iter_next(&it, "&o", &path))
                 paths.emplace_back(path);
             g_variant_unref(array);
             call(p, kAccountsPath, kAccountsIface, "FindUserById",
                  g_variant_new("(x)", static_cast<gint64>(p->self_uid)),
                  [p, serial, paths](GVariant* result, const std::string&) mutable {
                      if (serial != p->load_serial)
                          return;
                      if (result != nullptr) {
                          const char* self = nullptr;
                          g_variant_get(result, "(&o)", &self);
                          if (self != nullptr
                              && std::find(paths.begin(), paths.end(), self) == paths.end())
                              paths.emplace_back(self);
                      }
                      auto collected = std::make_shared<std::vector<User>>();
                      auto remaining = std::make_shared<int>(static_cast<int>(paths.size()));
                      if (paths.empty()) {
                          p->users.clear();
                          rebuild_views(p);
                          return;
                      }
                      for (const auto& user_path : paths) {
                          call(p, user_path, "org.freedesktop.DBus.Properties", "GetAll",
                               g_variant_new("(s)", kUserIface),
                               [p, serial, user_path, collected, remaining](GVariant* result,
                                                                           const std::string&) {
                                   if (serial != p->load_serial)
                                       return;
                                   if (result != nullptr) {
                                       GVariant* dict = g_variant_get_child_value(result, 0);
                                       User u;
                                       u.path = user_path;
                                       user_from_properties(u, dict);
                                       g_variant_unref(dict);
                                       if (!u.name.empty())
                                           collected->push_back(std::move(u));
                                   }
                                   if (--*remaining > 0)
                                       return;
                                   std::sort(collected->begin(), collected->end(),
                                             [](const User& a, const User& b) {
                                                 return g_utf8_collate(display_name(a).c_str(),
                                                                       display_name(b).c_str()) < 0;
                                             });
                                   p->users = std::move(*collected);
                                   rebuild_views(p);
                               });
                      }
                  });
         });
}

gboolean refresh_now(gpointer data) {
    auto* p = static_cast<UsersPage*>(data);
    p->refresh_source = 0;
    load_users(p);
    return G_SOURCE_REMOVE;
}

// Signals arrive in bursts (a new user emits UserAdded + several Changed);
// one reload 150 ms later covers them all.
void schedule_refresh(UsersPage* p) {
    if (p->refresh_source == 0)
        p->refresh_source = g_timeout_add(150, refresh_now, p);
}

void on_accounts_signal(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*,
                        GVariant*, gpointer data) {
    schedule_refresh(static_cast<UsersPage*>(data));
}

void show_unavailable(UsersPage* p, const std::string& why) {
    adw_status_page_set_description(
        ADW_STATUS_PAGE(p->unavailable),
        ("User accounts are managed through AccountsService, which is not available: " + why
         + " Install the accountsservice package and enable accounts-daemon.service.")
            .c_str());
    gtk_stack_set_visible_child_name(GTK_STACK(p->root_stack), "unavailable");
}

// -- per-user page ----------------------------------------------------------

void update_view(UserView* v);
void present_add_user_dialog(UsersPage* p);
void present_password_dialog(UserView* v);
void push_user_page(UsersPage* p, const std::string& user_path);

void on_name_apply(AdwEntryRow* row, gpointer data) {
    auto* v = static_cast<UserView*>(data);
    const std::string name = trim(gtk_editable_get_text(GTK_EDITABLE(row)));
    const User* u = find_user(v->page, v->user_path);
    if (u == nullptr)
        return;
    if (!real_name_valid(name)) {
        set_status(v->status, "A name cannot be empty or contain ':' or ','.", true);
        gtk_editable_set_text(GTK_EDITABLE(row), u->real_name.c_str());
        return;
    }
    if (name == u->real_name)
        return;
    set_status(v->status, "", false);
    call(v->page, v->user_path, kUserIface, "SetRealName", g_variant_new("(s)", name.c_str()),
         [v](GVariant* result, const std::string& error) {
             if (result == nullptr) {
                 set_status(v->status, error, true);
                 update_view(v); // back to the stored name
             }
         });
}

void on_admin_toggled(GObject* sw, GParamSpec*, gpointer data) {
    auto* v = static_cast<UserView*>(data);
    if (v->loading)
        return;
    const bool admin = gtk_switch_get_active(GTK_SWITCH(sw));
    const User* u = find_user(v->page, v->user_path);
    if (u == nullptr || (u->account_type == kAccountAdmin) == admin)
        return;
    set_status(v->status, "", false);
    call(v->page, v->user_path, kUserIface, "SetAccountType",
         g_variant_new("(i)", admin ? kAccountAdmin : 0), [v](GVariant* result, const std::string& error) {
             if (result == nullptr) {
                 set_status(v->status, error, true);
                 update_view(v); // flips the switch back
             }
         });
}

// The privileged half of a picture change: SDDM's face file and, for another
// account, that user's own ~/.face — one pkexec for both. `install` with
// positional parameters keeps the paths out of the shell text.
void install_faces(UserView* v, const std::string& source, const User& u) {
    const std::string sddm_face = sddm_face_path(v->page, u);
    const std::string other_face = v->is_self || u.home.empty() ? "" : u.home + "/.face";
    if ((sddm_face.empty() && other_face.empty()) || g_getenv("HS_USERS_NO_PKEXEC") != nullptr)
        return;
    set_status(v->avatar_status, "Waiting for authorisation…", false);
    const std::string script =
        "set -e; "
        "[ -z \"$2\" ] || install -m 644 -o root -g root \"$1\" \"$2\"; "
        "[ -z \"$3\" ] || install -m 644 -o \"$4\" -g \"$(id -gn \"$4\")\" \"$1\" \"$3\"";
    std::weak_ptr<bool> alive = v->page->alive;
    const bool cleanup = !v->is_self;
    run_command({"pkexec", "/bin/sh", "-c", script, "sh", source, sddm_face, other_face, u.name},
                [v, alive, source, cleanup, sddm_face](bool ok, int status, const std::string&,
                                                       const std::string& err) {
                    if (cleanup)
                        g_unlink(source.c_str());
                    if (alive.expired())
                        return;
                    if (ok) {
                        set_status(v->avatar_status,
                                   sddm_face.empty() ? "" : "Picture updated, including the login screen.",
                                   false);
                        return;
                    }
                    const std::string why = (status == 126 || status == 127)
                                                ? "Authorisation was not granted; the login screen keeps "
                                                  "its old picture."
                                                : "Login screen picture failed: " + first_line(err);
                    set_status(v->avatar_status, why, true);
                });
}

void apply_picture(UserView* v, const std::string& chosen) {
    const User* found = find_user(v->page, v->user_path);
    if (found == nullptr)
        return;
    const User u = *found;
    // ours goes straight to ~/.face (the shell reads it); another user's
    // home is not ours to write, so the file is staged in our cache and
    // pkexec installs it (install_faces)
    std::string dest;
    if (v->is_self) {
        gchar* face = g_build_filename(g_get_home_dir(), ".face", nullptr);
        dest = face;
        g_free(face);
    } else {
        dest = cache_file("avatar-" + u.name);
    }
    const std::string error = save_avatar(chosen, dest);
    if (!error.empty()) {
        set_status(v->avatar_status, error, true);
        return;
    }
    set_status(v->avatar_status, "", false);
    // AccountsService copies it under /var/lib/AccountsService/icons and
    // announces the change — other consumers (GDM, greeters) agree with us
    call(v->page, v->user_path, kUserIface, "SetIconFile", g_variant_new("(s)", dest.c_str()),
         [v, dest, u](GVariant* result, const std::string& error) {
             if (result == nullptr)
                 set_status(v->avatar_status, error, true);
             update_view(v);
             install_faces(v, dest, u);
         });
}

void on_avatar_change_clicked(GtkButton*, gpointer data) {
    auto* v = static_cast<UserView*>(data);
    GtkFileDialog* dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Select a Picture");
    GtkFileFilter* images = gtk_file_filter_new();
    gtk_file_filter_set_name(images, "Images");
    gtk_file_filter_add_mime_type(images, "image/*");
    GListStore* filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filters, images);
    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
    gtk_file_dialog_set_default_filter(dialog, images);
    g_object_unref(filters);
    g_object_unref(images);
    gchar* pictures = g_build_filename(g_get_home_dir(), "Pictures", nullptr);
    GFile* folder = g_file_new_for_path(pictures);
    gtk_file_dialog_set_initial_folder(dialog, folder);
    g_object_unref(folder);
    g_free(pictures);
    struct Ctx {
        UserView* v;
        std::weak_ptr<bool> alive;
    };
    gtk_file_dialog_open(
        dialog, v->page->window, nullptr,
        [](GObject* source, GAsyncResult* result, gpointer data) {
            std::unique_ptr<Ctx> ctx(static_cast<Ctx*>(data));
            GFile* file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), result, nullptr);
            if (file == nullptr)
                return;
            gchar* path = g_file_get_path(file);
            if (path != nullptr && !ctx->alive.expired())
                apply_picture(ctx->v, path);
            g_free(path);
            g_object_unref(file);
        },
        new Ctx{v, v->page->alive});
    g_object_unref(dialog);
}

void on_avatar_remove_clicked(GtkButton*, gpointer data) {
    auto* v = static_cast<UserView*>(data);
    const User* found = find_user(v->page, v->user_path);
    if (found == nullptr)
        return;
    const User u = *found;
    if (v->is_self) {
        gchar* face = g_build_filename(g_get_home_dir(), ".face", nullptr);
        g_unlink(face);
        g_free(face);
    }
    set_status(v->avatar_status, "", false);
    call(v->page, v->user_path, kUserIface, "SetIconFile", g_variant_new("(s)", ""),
         [v, u](GVariant* result, const std::string& error) {
             if (result == nullptr)
                 set_status(v->avatar_status, error, true);
             update_view(v);
             const std::string sddm_face = sddm_face_path(v->page, u);
             const std::string other_face = v->is_self || u.home.empty() ? "" : u.home + "/.face";
             const bool have_sddm = !sddm_face.empty() && g_file_test(sddm_face.c_str(), G_FILE_TEST_EXISTS);
             if (!have_sddm && other_face.empty())
                 return;
             set_status(v->avatar_status, "Waiting for authorisation…", false);
             std::weak_ptr<bool> alive = v->page->alive;
             std::vector<std::string> argv{"pkexec", "rm", "-f", "--"};
             if (have_sddm)
                 argv.push_back(sddm_face);
             if (!other_face.empty())
                 argv.push_back(other_face);
             run_command(argv,
                         [v, alive](bool ok, int status, const std::string&, const std::string& err) {
                             if (alive.expired())
                                 return;
                             if (ok) {
                                 set_status(v->avatar_status, "", false);
                                 return;
                             }
                             set_status(v->avatar_status,
                                        (status == 126 || status == 127)
                                            ? "Authorisation was not granted; the login screen keeps "
                                              "its old picture."
                                            : "Login screen picture failed: " + first_line(err),
                                        true);
                         });
         });
}

void on_password_row_activated(AdwActionRow*, gpointer data) {
    present_password_dialog(static_cast<UserView*>(data));
}

void on_add_user_activated(AdwButtonRow*, gpointer data) {
    present_add_user_dialog(static_cast<UsersPage*>(data));
}

void on_other_row_activated(AdwActionRow* row, gpointer data) {
    auto* p = static_cast<UsersPage*>(data);
    const char* path = static_cast<const char*>(g_object_get_data(G_OBJECT(row), "user-path"));
    if (path != nullptr)
        push_user_page(p, path);
}

void on_remove_user_activated(AdwButtonRow*, gpointer data) {
    auto* v = static_cast<UserView*>(data);
    const User* found = find_user(v->page, v->user_path);
    if (found == nullptr)
        return;
    const User u = *found;
    AdwDialog* dialog = adw_alert_dialog_new(nullptr, nullptr);
    adw_alert_dialog_format_heading(ADW_ALERT_DIALOG(dialog), "Remove %s?", display_name(u).c_str());
    adw_alert_dialog_format_body(ADW_ALERT_DIALOG(dialog),
                                 "It is possible to keep the home directory, mail spool and temporary "
                                 "files around when deleting a user account.");
    adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(dialog), "cancel", "_Cancel", "keep", "_Keep Files",
                                   "delete", "_Delete Files", nullptr);
    adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dialog), "delete", ADW_RESPONSE_DESTRUCTIVE);
    adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dialog), "cancel");
    adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(dialog), "cancel");
    struct Ctx {
        UserView* v;
        std::weak_ptr<bool> alive;
        User u;
    };
    g_signal_connect_data(
        dialog, "response",
        G_CALLBACK(+[](AdwAlertDialog*, const char* response, gpointer data) {
            auto* ctx = static_cast<Ctx*>(data);
            if (ctx->alive.expired() || g_strcmp0(response, "cancel") == 0)
                return;
            const bool remove_files = g_strcmp0(response, "delete") == 0;
            UserView* v = ctx->v;
            set_status(v->status, "", false);
            call(v->page, kAccountsPath, kAccountsIface, "DeleteUser",
                 g_variant_new("(xb)", static_cast<gint64>(ctx->u.uid), remove_files ? TRUE : FALSE),
                 [v](GVariant* result, const std::string& error) {
                     if (result == nullptr) {
                         set_status(v->status, error, true);
                         return;
                     }
                     // the UserDeleted signal reloads the list; the page is
                     // popped from there once the user is gone (rebuild_views)
                 });
        }),
        new Ctx{v, v->page->alive, u}, [](gpointer data, GClosure*) { delete static_cast<Ctx*>(data); },
        static_cast<GConnectFlags>(0));
    adw_dialog_present(dialog, GTK_WIDGET(v->page->window));
}

// "Administrator" row: an AdwSwitchRow puts its switch first, GNOME's row
// has the info icon before the switch — so it is an action row with both
// as suffixes in that order.
GtkWidget* make_admin_row(GtkWidget** switch_out) {
    GtkWidget* row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), "Administrator");
    GtkWidget* info = gtk_image_new_from_icon_name("help-about-symbolic");
    gtk_widget_set_tooltip_text(info, "Administrators have extra abilities, including adding and "
                                      "removing users, changing login settings, and removing software. "
                                      "Parental controls cannot be applied to administrators.");
    gtk_widget_set_valign(info, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(ADW_ACTION_ROW(row), info);
    GtkWidget* sw = gtk_switch_new();
    gtk_widget_set_valign(sw, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(ADW_ACTION_ROW(row), sw);
    adw_action_row_set_activatable_widget(ADW_ACTION_ROW(row), sw);
    *switch_out = sw;
    return row;
}

GtkWidget* make_chevron() {
    GtkWidget* icon = gtk_image_new_from_icon_name("go-next-symbolic");
    gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
    return icon;
}

// GNOME's cc-user-page layout: the picture with its two round buttons, then
// the name / password [/ administrator] group, then (root) Other Users and
// Add User, or (pushed) Remove User.
UserView* build_user_view(UsersPage* p, const User& u, bool is_self) {
    auto* v = new UserView();
    v->page = p;
    v->user_path = u.path;
    v->is_self = is_self;

    GtkWidget* page = adw_preferences_page_new();

    // -- picture --------------------------------------------------------------
    GtkWidget* avatar_group = adw_preferences_group_new();
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(avatar_group));
    GtkWidget* avatar_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_halign(avatar_box, GTK_ALIGN_CENTER);
    GtkWidget* overlay = gtk_overlay_new();
    gtk_widget_set_halign(overlay, GTK_ALIGN_CENTER);
    v->avatar = ADW_AVATAR(adw_avatar_new(kAvatarViewPx, display_name(u).c_str(), TRUE));
    gtk_overlay_set_child(GTK_OVERLAY(overlay), GTK_WIDGET(v->avatar));
    GtkWidget* change = gtk_button_new_from_icon_name("document-edit-symbolic");
    gtk_widget_add_css_class(change, "circular");
    gtk_widget_add_css_class(change, "avatar-button");
    gtk_widget_set_tooltip_text(change, "Change picture");
    gtk_widget_set_halign(change, GTK_ALIGN_END);
    gtk_widget_set_valign(change, GTK_ALIGN_END);
    g_signal_connect(change, "clicked", G_CALLBACK(on_avatar_change_clicked), v);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), change);
    v->avatar_remove = gtk_button_new_from_icon_name("user-trash-symbolic");
    gtk_widget_add_css_class(v->avatar_remove, "circular");
    gtk_widget_add_css_class(v->avatar_remove, "avatar-button");
    gtk_widget_add_css_class(v->avatar_remove, "avatar-remove");
    gtk_widget_set_tooltip_text(v->avatar_remove, "Remove picture");
    gtk_widget_set_halign(v->avatar_remove, GTK_ALIGN_END);
    gtk_widget_set_valign(v->avatar_remove, GTK_ALIGN_START);
    g_signal_connect(v->avatar_remove, "clicked", G_CALLBACK(on_avatar_remove_clicked), v);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), v->avatar_remove);
    gtk_box_append(GTK_BOX(avatar_box), overlay);
    v->avatar_status = make_status_label();
    gtk_label_set_xalign(GTK_LABEL(v->avatar_status), 0.5f);
    gtk_label_set_justify(GTK_LABEL(v->avatar_status), GTK_JUSTIFY_CENTER);
    gtk_box_append(GTK_BOX(avatar_box), v->avatar_status);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(avatar_group), avatar_box);

    // -- name / password / administrator --------------------------------------
    GtkWidget* account_group = adw_preferences_group_new();
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(account_group));
    v->name_row = adw_entry_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(v->name_row), "Name");
    adw_entry_row_set_show_apply_button(ADW_ENTRY_ROW(v->name_row), TRUE);
    g_signal_connect(v->name_row, "apply", G_CALLBACK(on_name_apply), v);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(account_group), v->name_row);

    v->password_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(v->password_row), "Password");
    GtkWidget* dots = gtk_label_new("•••••");
    gtk_widget_add_css_class(dots, "dim-label");
    gtk_widget_set_valign(dots, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(ADW_ACTION_ROW(v->password_row), dots);
    adw_action_row_add_suffix(ADW_ACTION_ROW(v->password_row), make_chevron());
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(v->password_row), TRUE);
    g_signal_connect(v->password_row, "activated", G_CALLBACK(on_password_row_activated), v);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(account_group), v->password_row);

    if (!is_self) {
        v->admin_row = make_admin_row(&v->admin_switch);
        g_signal_connect(v->admin_switch, "notify::active", G_CALLBACK(on_admin_toggled), v);
        adw_preferences_group_add(ADW_PREFERENCES_GROUP(account_group), v->admin_row);
    }
    v->status = make_status_label();
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(account_group), v->status);

    if (is_self) {
        // -- Other Users -------------------------------------------------------
        v->others_group = adw_preferences_group_new();
        adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(v->others_group), "Other Users");
        adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(v->others_group));
        v->others_list = v->others_group; // rows are added to the group itself
        gtk_widget_set_visible(v->others_group, FALSE);

        // -- Add User ---------------------------------------------------------
        v->add_group = adw_preferences_group_new();
        adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(v->add_group));
        GtkWidget* add_row = adw_button_row_new();
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(add_row), "Add User");
        adw_button_row_set_end_icon_name(ADW_BUTTON_ROW(add_row), "go-next-symbolic");
        g_signal_connect(add_row, "activated", G_CALLBACK(on_add_user_activated), p);
        adw_preferences_group_add(ADW_PREFERENCES_GROUP(v->add_group), add_row);
    } else {
        // -- Remove User ------------------------------------------------------
        v->remove_group = adw_preferences_group_new();
        adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(v->remove_group));
        GtkWidget* remove_row = adw_button_row_new();
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(remove_row), "Remove User…");
        gtk_widget_add_css_class(remove_row, "destructive-action");
        g_signal_connect(remove_row, "activated", G_CALLBACK(on_remove_user_activated), v);
        adw_preferences_group_add(ADW_PREFERENCES_GROUP(v->remove_group), remove_row);
    }

    v->view = adw_toolbar_view_new();
    GtkWidget* header = adw_header_bar_new();
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(header),
                                    adw_window_title_new(is_self ? "Users" : display_name(u).c_str(), nullptr));
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(v->view), header);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(v->view), page);
    // the root lives in the page's own stack (loading / unavailable / page);
    // only another user's page is pushed onto the navigation view
    GtkWidget* owner = v->view;
    if (!is_self) {
        v->nav_page = GTK_WIDGET(adw_navigation_page_new(v->view, display_name(u).c_str()));
        owner = v->nav_page;
    }
    g_object_set_data_full(G_OBJECT(owner), "user-view", v, [](gpointer data) {
        auto* v = static_cast<UserView*>(data);
        if (v->page->other_view == v)
            v->page->other_view = nullptr;
        if (v->page->self_view == v)
            v->page->self_view = nullptr;
        delete v;
    });
    update_view(v);
    return v;
}

void rebuild_other_rows(UserView* v) {
    UsersPage* p = v->page;
    for (GtkWidget* row : v->other_rows)
        adw_preferences_group_remove(ADW_PREFERENCES_GROUP(v->others_group), row);
    v->other_rows.clear();
    for (const auto& u : p->users) {
        if (u.uid == p->self_uid || u.system)
            continue;
        GtkWidget* row = adw_action_row_new();
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), display_name(u).c_str());
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row),
                                    u.account_type == kAccountAdmin ? "Administrator" : "Standard");
        GtkWidget* avatar = adw_avatar_new(kAvatarRowPx, display_name(u).c_str(), TRUE);
        set_avatar_image(ADW_AVATAR(avatar), picture_path(p, u), display_name(u));
        gtk_widget_set_valign(avatar, GTK_ALIGN_CENTER);
        adw_action_row_add_prefix(ADW_ACTION_ROW(row), avatar);
        adw_action_row_add_suffix(ADW_ACTION_ROW(row), make_chevron());
        gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), TRUE);
        g_object_set_data_full(G_OBJECT(row), "user-path", g_strdup(u.path.c_str()), g_free);
        g_signal_connect(row, "activated", G_CALLBACK(on_other_row_activated), p);
        adw_preferences_group_add(ADW_PREFERENCES_GROUP(v->others_group), row);
        v->other_rows.push_back(row);
    }
    gtk_widget_set_visible(v->others_group, !v->other_rows.empty());
}

void update_view(UserView* v) {
    const User* u = find_user(v->page, v->user_path);
    if (u == nullptr)
        return;
    v->loading = true;
    const std::string name = display_name(*u);
    const std::string picture = picture_path(v->page, *u);
    set_avatar_image(v->avatar, picture, name);
    gtk_widget_set_visible(v->avatar_remove, !picture.empty());
    // a name being typed must not be overwritten by a daemon signal
    if (!has_focus_within(v->name_row))
        gtk_editable_set_text(GTK_EDITABLE(v->name_row), u->real_name.c_str());
    if (v->admin_switch != nullptr)
        gtk_switch_set_active(GTK_SWITCH(v->admin_switch), u->account_type == kAccountAdmin);
    if (v->nav_page != nullptr && !v->is_self)
        adw_navigation_page_set_title(ADW_NAVIGATION_PAGE(v->nav_page), name.c_str());
    if (v->is_self)
        rebuild_other_rows(v);
    v->loading = false;
}

void push_user_page(UsersPage* p, const std::string& user_path) {
    const User* u = find_user(p, user_path);
    if (u == nullptr)
        return;
    if (p->other_view != nullptr)
        adw_navigation_view_pop(ADW_NAVIGATION_VIEW(p->nav));
    p->other_view = build_user_view(p, *u, /*is_self=*/false);
    adw_navigation_view_push(ADW_NAVIGATION_VIEW(p->nav), ADW_NAVIGATION_PAGE(p->other_view->nav_page));
}

// Fresh data → refresh the shown pages (the root is built on the first load).
void rebuild_views(UsersPage* p) {
    const User* self = nullptr;
    for (const auto& u : p->users)
        if (u.uid == p->self_uid)
            self = &u;
    if (self == nullptr) {
        show_unavailable(p, "the current account is unknown to it.");
        return;
    }
    if (p->self_view == nullptr) {
        p->self_view = build_user_view(p, *self, /*is_self=*/true);
        gtk_stack_add_named(GTK_STACK(p->root_stack), p->self_view->view, "page");
    } else {
        p->self_view->user_path = self->path;
        update_view(p->self_view);
    }
    gtk_stack_set_visible_child_name(GTK_STACK(p->root_stack), "page");
    if (p->other_view != nullptr) {
        if (find_user(p, p->other_view->user_path) == nullptr)
            adw_navigation_view_pop(ADW_NAVIGATION_VIEW(p->nav)); // removed elsewhere
        else
            update_view(p->other_view);
    }
}

// -- dialogs ----------------------------------------------------------------

// Header-bar dialog like GNOME's: Cancel at the start, the action at the end.
struct DialogShell {
    AdwDialog* dialog;
    GtkWidget* action; // the suggested button
    GtkWidget* page;   // AdwPreferencesPage for the groups
};

DialogShell make_dialog(const char* title, const char* action_label, int width) {
    DialogShell d{};
    d.dialog = adw_dialog_new();
    adw_dialog_set_title(d.dialog, title);
    adw_dialog_set_content_width(d.dialog, width);
    GtkWidget* view = adw_toolbar_view_new();
    GtkWidget* header = adw_header_bar_new();
    adw_header_bar_set_show_start_title_buttons(ADW_HEADER_BAR(header), FALSE);
    adw_header_bar_set_show_end_title_buttons(ADW_HEADER_BAR(header), FALSE);
    GtkWidget* cancel = gtk_button_new_with_label("Cancel");
    g_signal_connect_swapped(cancel, "clicked", G_CALLBACK(adw_dialog_close), d.dialog);
    adw_header_bar_pack_start(ADW_HEADER_BAR(header), cancel);
    d.action = gtk_button_new_with_label(action_label);
    gtk_widget_add_css_class(d.action, "suggested-action");
    adw_header_bar_pack_end(ADW_HEADER_BAR(header), d.action);
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(view), header);
    d.page = adw_preferences_page_new();
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(view), d.page);
    adw_dialog_set_child(d.dialog, view);
    adw_dialog_set_default_widget(d.dialog, d.action);
    return d;
}

GtkWidget* make_password_row(const char* title, bool with_generate) {
    GtkWidget* row = adw_password_entry_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    adw_entry_row_set_activates_default(ADW_ENTRY_ROW(row), TRUE);
    if (with_generate) {
        GtkWidget* generate = gtk_button_new_from_icon_name("view-refresh-symbolic");
        gtk_widget_add_css_class(generate, "flat");
        gtk_widget_set_valign(generate, GTK_ALIGN_CENTER);
        gtk_widget_set_tooltip_text(generate, "Generate a password");
        g_signal_connect_swapped(generate, "clicked", G_CALLBACK(+[](GtkWidget* row) {
                                     gtk_editable_set_text(GTK_EDITABLE(row), generate_password().c_str());
                                 }),
                                 row);
        adw_entry_row_add_suffix(ADW_ENTRY_ROW(row), generate);
    }
    return row;
}

// Shared by both dialogs: password + confirmation, strength bar and hint.
struct PasswordFields {
    GtkWidget* password = nullptr;
    GtkWidget* confirm = nullptr;
    GtkWidget* strength = nullptr; // GtkLevelBar
    GtkWidget* hint = nullptr;

    std::string value() const { return gtk_editable_get_text(GTK_EDITABLE(password)); }
    // updates the visuals, returns whether the pair is acceptable
    bool validate() const {
        const std::string pw = value();
        const std::string again = gtk_editable_get_text(GTK_EDITABLE(confirm));
        const Strength s = password_strength(pw);
        gtk_level_bar_set_value(GTK_LEVEL_BAR(strength), s.level);
        std::string text = s.text;
        bool ok = !pw.empty();
        gtk_widget_remove_css_class(confirm, "error");
        if (!again.empty() && again != pw) {
            gtk_widget_add_css_class(confirm, "error");
            text = "The passwords do not match.";
            ok = false;
        } else if (!pw.empty() && again.empty()) {
            ok = false;
        }
        gtk_label_set_text(GTK_LABEL(hint), text.c_str());
        return ok;
    }
};

PasswordFields add_password_group(GtkWidget* page, const char* title, const char* password_title) {
    PasswordFields f;
    GtkWidget* group = adw_preferences_group_new();
    if (title != nullptr)
        adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(group), title);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(group));
    f.password = make_password_row(password_title, true);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), f.password);
    f.confirm = make_password_row("Confirm Password", false);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), f.confirm);
    GtkWidget* below = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_top(below, 8);
    f.strength = gtk_level_bar_new_for_interval(0, 5);
    gtk_level_bar_set_mode(GTK_LEVEL_BAR(f.strength), GTK_LEVEL_BAR_MODE_DISCRETE);
    gtk_box_append(GTK_BOX(below), f.strength);
    f.hint = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(f.hint), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(f.hint), TRUE);
    gtk_widget_add_css_class(f.hint, "dim-label");
    gtk_widget_add_css_class(f.hint, "caption");
    gtk_box_append(GTK_BOX(below), f.hint);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), below);
    return f;
}

// GNOME's "Change Password" for another user, without the "set at next login"
// choice; for ourselves polkit asks for the administrator password first.
void present_password_dialog(UserView* v) {
    const User* found = find_user(v->page, v->user_path);
    if (found == nullptr)
        return;
    struct State {
        UserView* v;
        std::weak_ptr<bool> alive;
        DialogShell shell;
        PasswordFields fields;
        GtkWidget* status;
        bool busy = false;
        std::shared_ptr<bool> open = std::make_shared<bool>(true); // false once the dialog is gone
    };
    auto* st = new State();
    st->v = v;
    st->alive = v->page->alive;
    st->shell = make_dialog("Change Password", "Change", 440);
    GtkWidget* intro_group = adw_preferences_group_new();
    // polkit's change-own-password / user-administration are auth_admin:
    // an administrator confirms their own password, anyone else needs one
    std::string intro = v->is_self
                            ? (found->account_type == kAccountAdmin
                                   ? "You will be asked to confirm your current password before the change is made."
                                   : "An administrator's password is required to change your password.")
                            : "Set a new password for " + display_name(*found)
                                  + ". An administrator's password is required.";
    adw_preferences_group_set_description(ADW_PREFERENCES_GROUP(intro_group), intro.c_str());
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(st->shell.page), ADW_PREFERENCES_GROUP(intro_group));
    st->fields = add_password_group(st->shell.page, nullptr, "New Password");
    st->status = make_status_label();
    GtkWidget* status_group = adw_preferences_group_new();
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(status_group), st->status);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(st->shell.page), ADW_PREFERENCES_GROUP(status_group));

    auto validate = +[](GtkEditable*, gpointer data) {
        auto* st = static_cast<State*>(data);
        gtk_widget_set_sensitive(st->shell.action, st->fields.validate() && !st->busy);
    };
    g_signal_connect(st->fields.password, "changed", G_CALLBACK(validate), st);
    g_signal_connect(st->fields.confirm, "changed", G_CALLBACK(validate), st);
    gtk_widget_set_sensitive(st->shell.action, FALSE);

    g_signal_connect(st->shell.action, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
                         auto* st = static_cast<State*>(data);
                         if (st->busy || !st->fields.validate())
                             return;
                         const std::string hash = crypt_password(st->fields.value());
                         if (hash.empty()) {
                             set_status(st->status, "The password could not be hashed.", true);
                             return;
                         }
                         st->busy = true;
                         gtk_widget_set_sensitive(st->shell.action, FALSE);
                         set_status(st->status, "Waiting for authorisation…", false);
                         std::weak_ptr<bool> open = st->open;
                         call(st->v->page, st->v->user_path, kUserIface, "SetPassword",
                              g_variant_new("(ss)", hash.c_str(), ""),
                              [st, open](GVariant* result, const std::string& error) {
                                  if (open.expired()) // Cancel while the prompt was up
                                      return;
                                  st->busy = false;
                                  if (result != nullptr) {
                                      adw_dialog_close(st->shell.dialog);
                                      return;
                                  }
                                  set_status(st->status, error, true);
                                  gtk_widget_set_sensitive(st->shell.action, st->fields.validate());
                              });
                     }),
                     st);
    g_object_set_data_full(G_OBJECT(st->shell.dialog), "state", st,
                           [](gpointer data) { delete static_cast<State*>(data); });
    adw_dialog_present(st->shell.dialog, GTK_WIDGET(v->page->window));
    gtk_widget_grab_focus(st->fields.password);
}

// GNOME's Add User dialog minus "User sets password on first login": the
// account is created and its password set in one go (CreateUser, then
// SetPassword on the new object — one polkit authorisation covers both,
// user-administration is auth_admin_keep).
void present_add_user_dialog(UsersPage* p) {
    struct State {
        UsersPage* p;
        std::weak_ptr<bool> alive;
        DialogShell shell;
        GtkWidget* full_name;
        GtkWidget* username;
        GtkWidget* username_hint;
        GtkWidget* admin; // GtkSwitch
        PasswordFields fields;
        GtkWidget* status;
        bool username_edited = false; // typed by hand → stop proposing
        bool proposing = false;
        bool busy = false;
        std::shared_ptr<bool> open = std::make_shared<bool>(true); // false once the dialog is gone
    };
    constexpr const char* kUsernameHint =
        "Usernames can only include lower case letters, numbers, hyphens and underscores.";
    auto* st = new State();
    st->p = p;
    st->alive = p->alive;
    st->shell = make_dialog("Add User", "Add", 480);

    // -- User Details ---------------------------------------------------------
    GtkWidget* details = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(details), "User Details");
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(st->shell.page), ADW_PREFERENCES_GROUP(details));
    st->full_name = adw_entry_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(st->full_name), "Full Name");
    adw_entry_row_set_activates_default(ADW_ENTRY_ROW(st->full_name), TRUE);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(details), st->full_name);
    st->username = adw_entry_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(st->username), "Username");
    adw_entry_row_set_activates_default(ADW_ENTRY_ROW(st->username), TRUE);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(details), st->username);
    GtkWidget* hint_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top(hint_box, 8);
    gtk_widget_set_margin_start(hint_box, 2);
    GtkWidget* hint_icon = gtk_image_new_from_icon_name("help-about-symbolic");
    gtk_widget_set_valign(hint_icon, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(hint_box), hint_icon);
    st->username_hint = gtk_label_new(kUsernameHint);
    gtk_label_set_wrap(GTK_LABEL(st->username_hint), TRUE);
    gtk_label_set_xalign(GTK_LABEL(st->username_hint), 0.0f);
    gtk_widget_add_css_class(st->username_hint, "caption");
    gtk_box_append(GTK_BOX(hint_box), st->username_hint);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(details), hint_box);

    GtkWidget* admin_group = adw_preferences_group_new();
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(st->shell.page), ADW_PREFERENCES_GROUP(admin_group));
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(admin_group), make_admin_row(&st->admin));

    // -- Password -------------------------------------------------------------
    st->fields = add_password_group(st->shell.page, "Password", "Password");
    st->status = make_status_label();
    GtkWidget* status_group = adw_preferences_group_new();
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(status_group), st->status);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(st->shell.page), ADW_PREFERENCES_GROUP(status_group));

    auto validate = +[](GtkEditable*, gpointer data) {
        auto* st = static_cast<State*>(data);
        const std::string full = trim(gtk_editable_get_text(GTK_EDITABLE(st->full_name)));
        const std::string user = gtk_editable_get_text(GTK_EDITABLE(st->username));
        bool ok = real_name_valid(full);
        gtk_widget_remove_css_class(st->username, "error");
        gtk_widget_remove_css_class(st->username_hint, "error");
        std::string hint = kUsernameHint;
        if (!user.empty() && !username_valid(user)) {
            ok = false;
            hint = "Usernames can only include lower case letters, numbers, hyphens and "
                   "underscores, and must start with a letter or underscore.";
            gtk_widget_add_css_class(st->username, "error");
            gtk_widget_add_css_class(st->username_hint, "error");
        } else if (!user.empty() && username_taken(user)) {
            ok = false;
            hint = "That username is already in use.";
            gtk_widget_add_css_class(st->username, "error");
            gtk_widget_add_css_class(st->username_hint, "error");
        } else if (user.empty()) {
            ok = false;
        }
        gtk_label_set_text(GTK_LABEL(st->username_hint), hint.c_str());
        ok = st->fields.validate() && ok;
        gtk_widget_set_sensitive(st->shell.action, ok && !st->busy);
    };
    g_signal_connect(st->full_name, "changed", G_CALLBACK(+[](GtkEditable* editable, gpointer data) {
                         auto* st = static_cast<State*>(data);
                         if (!st->username_edited) {
                             st->proposing = true;
                             gtk_editable_set_text(GTK_EDITABLE(st->username),
                                                   propose_username(gtk_editable_get_text(editable)).c_str());
                             st->proposing = false;
                         }
                     }),
                     st);
    g_signal_connect(st->username, "changed", G_CALLBACK(+[](GtkEditable* editable, gpointer data) {
                         auto* st = static_cast<State*>(data);
                         if (!st->proposing)
                             st->username_edited = *gtk_editable_get_text(editable) != '\0';
                     }),
                     st);
    for (GtkWidget* w : {st->full_name, st->username, st->fields.password, st->fields.confirm})
        g_signal_connect_after(w, "changed", G_CALLBACK(validate), st);
    gtk_widget_set_sensitive(st->shell.action, FALSE);

    g_signal_connect(
        st->shell.action, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
            auto* st = static_cast<State*>(data);
            if (st->busy)
                return;
            const std::string full = trim(gtk_editable_get_text(GTK_EDITABLE(st->full_name)));
            const std::string user = gtk_editable_get_text(GTK_EDITABLE(st->username));
            if (!real_name_valid(full) || !username_valid(user) || username_taken(user)
                || !st->fields.validate())
                return;
            const std::string hash = crypt_password(st->fields.value());
            if (hash.empty()) {
                set_status(st->status, "The password could not be hashed.", true);
                return;
            }
            const bool admin = gtk_switch_get_active(GTK_SWITCH(st->admin));
            st->busy = true;
            gtk_widget_set_sensitive(st->shell.action, FALSE);
            set_status(st->status, "Waiting for authorisation…", false);
            std::weak_ptr<bool> open = st->open;
            UsersPage* p = st->p;
            call(st->p, kAccountsPath, kAccountsIface, "CreateUser",
                 g_variant_new("(ssi)", user.c_str(), full.c_str(), admin ? kAccountAdmin : 0),
                 [st, open, p, hash](GVariant* result, const std::string& error) {
                     if (open.expired()) {
                         // the dialog was cancelled meanwhile; the account
                         // still gets its password so it is not left locked
                         if (result != nullptr) {
                             const char* path = nullptr;
                             g_variant_get(result, "(&o)", &path);
                             call(p, path, kUserIface, "SetPassword", g_variant_new("(ss)", hash.c_str(), ""),
                                  nullptr);
                         }
                         return;
                     }
                     if (result == nullptr) {
                         st->busy = false;
                         set_status(st->status, error, true);
                         gtk_widget_set_sensitive(st->shell.action, TRUE);
                         return;
                     }
                     const char* path = nullptr;
                     g_variant_get(result, "(&o)", &path);
                     set_status(st->status, "Setting the password…", false);
                     call(st->p, path, kUserIface, "SetPassword", g_variant_new("(ss)", hash.c_str(), ""),
                          [st, open](GVariant* result, const std::string& error) {
                              if (open.expired())
                                  return;
                              st->busy = false;
                              if (result != nullptr) {
                                  adw_dialog_close(st->shell.dialog);
                                  return;
                              }
                              // the account exists now (the list shows it);
                              // the password can be set from its page
                              set_status(st->status,
                                         "The user was created, but the password could not be set: "
                                             + error + " Set it from the user's page.",
                                         true);
                          });
                 });
        }),
        st);
    g_object_set_data_full(G_OBJECT(st->shell.dialog), "state", st,
                           [](gpointer data) { delete static_cast<State*>(data); });
    adw_dialog_present(st->shell.dialog, GTK_WIDGET(p->window));
    gtk_widget_grab_focus(st->full_name);
}

// -- bus ----------------------------------------------------------------------

void on_bus_ready(GObject*, GAsyncResult* result, gpointer data) {
    auto* ctx = static_cast<std::pair<UsersPage*, std::weak_ptr<bool>>*>(data);
    UsersPage* p = ctx->first;
    const bool alive = !ctx->second.expired();
    delete ctx;
    GError* error = nullptr;
    GDBusConnection* bus = g_bus_get_finish(result, &error);
    if (!alive) {
        if (bus != nullptr)
            g_object_unref(bus);
        g_clear_error(&error);
        return;
    }
    if (bus == nullptr) {
        show_unavailable(p, describe_error(error));
        g_clear_error(&error);
        return;
    }
    p->bus = bus;
    p->sub_added = g_dbus_connection_signal_subscribe(bus, kAccountsName, kAccountsIface, "UserAdded",
                                                      kAccountsPath, nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
                                                      on_accounts_signal, p, nullptr);
    p->sub_deleted = g_dbus_connection_signal_subscribe(bus, kAccountsName, kAccountsIface, "UserDeleted",
                                                        kAccountsPath, nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
                                                        on_accounts_signal, p, nullptr);
    p->sub_changed = g_dbus_connection_signal_subscribe(bus, kAccountsName, kUserIface, "Changed", nullptr,
                                                        nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
                                                        on_accounts_signal, p, nullptr);
    load_users(p);
}

// Dev hooks, 1.5 s after the page exists (pointer clicks cannot be
// scripted): HS_USERS_ADD=1 / HS_USERS_PASSWORD=1 open the dialogs,
// HS_USERS_PICTURE=<image> applies that picture to our own account exactly
// like the file dialog would (HS_USERS_NO_PKEXEC=1 skips the privileged
// SDDM copy so a test raises no prompt).
gboolean on_dev_hook(gpointer data) {
    auto* p = static_cast<UsersPage*>(data);
    if (p->self_view == nullptr)
        return G_SOURCE_CONTINUE; // still loading
    if (g_getenv("HS_USERS_ADD") != nullptr)
        present_add_user_dialog(p);
    else if (g_getenv("HS_USERS_PASSWORD") != nullptr)
        present_password_dialog(p->self_view);
    else if (const char* picture = g_getenv("HS_USERS_PICTURE"))
        apply_picture(p->self_view, picture);
    return G_SOURCE_REMOVE;
}

} // namespace

GtkWidget* build_users_page(GtkWindow* window) {
    auto* p = new UsersPage();
    p->window = window;
    gchar* sddm = g_find_program_in_path("sddm");
    if (sddm != nullptr) {
        p->faces_dir = sddm_config_value("Theme", "FacesDir");
        if (p->faces_dir.empty())
            p->faces_dir = kDefaultFacesDir;
        while (p->faces_dir.size() > 1 && p->faces_dir.back() == '/')
            p->faces_dir.pop_back();
    }
    g_free(sddm);

    p->nav = adw_navigation_view_new();
    p->root_stack = gtk_stack_new();
    gtk_stack_set_hhomogeneous(GTK_STACK(p->root_stack), FALSE);
    gtk_stack_set_vhomogeneous(GTK_STACK(p->root_stack), FALSE);

    // loading: header + spinner, so the page has its title from the start
    GtkWidget* loading_view = adw_toolbar_view_new();
    GtkWidget* loading_header = adw_header_bar_new();
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(loading_header), adw_window_title_new("Users", nullptr));
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(loading_view), loading_header);
    GtkWidget* spinner = adw_spinner_new();
    gtk_widget_set_size_request(spinner, 32, 32);
    gtk_widget_set_halign(spinner, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(spinner, GTK_ALIGN_CENTER);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(loading_view), spinner);
    gtk_stack_add_named(GTK_STACK(p->root_stack), loading_view, "loading");

    GtkWidget* unavailable_view = adw_toolbar_view_new();
    GtkWidget* unavailable_header = adw_header_bar_new();
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(unavailable_header),
                                    adw_window_title_new("Users", nullptr));
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(unavailable_view), unavailable_header);
    p->unavailable = adw_status_page_new();
    adw_status_page_set_icon_name(ADW_STATUS_PAGE(p->unavailable), "system-users-symbolic");
    adw_status_page_set_title(ADW_STATUS_PAGE(p->unavailable), "User Accounts Unavailable");
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(unavailable_view), p->unavailable);
    gtk_stack_add_named(GTK_STACK(p->root_stack), unavailable_view, "unavailable");

    GtkWidget* root = GTK_WIDGET(adw_navigation_page_new(p->root_stack, "Users"));
    adw_navigation_view_add(ADW_NAVIGATION_VIEW(p->nav), ADW_NAVIGATION_PAGE(root));
    g_object_set_data_full(G_OBJECT(p->nav), "users-page-state", p, [](gpointer data) {
        auto* p = static_cast<UsersPage*>(data);
        *p->alive = false;
        if (p->refresh_source != 0)
            g_source_remove(p->refresh_source);
        if (p->bus != nullptr) {
            for (guint id : {p->sub_added, p->sub_deleted, p->sub_changed})
                if (id != 0)
                    g_dbus_connection_signal_unsubscribe(p->bus, id);
            g_object_unref(p->bus);
        }
        delete p;
    });

    g_bus_get(G_BUS_TYPE_SYSTEM, nullptr, on_bus_ready,
              new std::pair<UsersPage*, std::weak_ptr<bool>>(p, p->alive));
    if (g_getenv("HS_USERS_ADD") != nullptr || g_getenv("HS_USERS_PASSWORD") != nullptr
        || g_getenv("HS_USERS_PICTURE") != nullptr)
        g_timeout_add(1500, on_dev_hook, p);
    return p->nav;
}

} // namespace hyprshell::settings
