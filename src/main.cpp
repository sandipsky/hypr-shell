#include "bar/bar.hpp"
#include "bar/idle_fade.hpp"
#include "bar/clipboard_window.hpp"
#include "bar/launcher_window.hpp"
#include "bar/lock_screen.hpp"
#include "bar/notification_popup.hpp"
#include "bar/osd_window.hpp"
#include "bar/session_window.hpp"
#include "bar/wallpaper_window.hpp"
#include "services/battery_alerts.hpp"
#include "services/config.hpp"
#include "services/theme.hpp"
#include "services/wallpaper.hpp"
#include "services/idle.hpp"
#include "services/night_light.hpp"
#include "services/osd.hpp"
#include "services/session.hpp"

#include <gtk4-layer-shell.h>
#include <gtkmm.h>

#include <algorithm>

#include <iostream>
#include <memory>
#include <string>

namespace hyprshell {

class App : public Gtk::Application {
public:
    static Glib::RefPtr<App> create() {
        return Glib::make_refptr_for_instance(new App());
    }

protected:
    App() : Gtk::Application("dev.hyprshell.Shell") {
        // `hypr-shell --launcher` toggles the launcher in the running
        // instance — bind it in hyprland.conf:
        //   bind = SUPER, SPACE, exec, hypr-shell --launcher
        add_main_option_entry(Gtk::Application::OptionType::BOOL, "launcher", 'l',
                              "Toggle the app launcher in the running instance");
        add_action("launcher", [this] {
            if (launcher_window_)
                launcher_window_->toggle();
        });
        // `hypr-shell --clipboard` toggles the clipboard history window, e.g.
        //   bind = SUPER, V, exec, hypr-shell --clipboard
        add_main_option_entry(Gtk::Application::OptionType::BOOL, "clipboard", 'c',
                              "Toggle the clipboard history in the running instance");
        add_action("clipboard", [this] {
            if (clipboard_window_)
                clipboard_window_->toggle();
        });
        // `hypr-shell --app-menu` toggles the bar's app menu popover, e.g.
        //   bindr = SUPER, SUPER_L, exec, hypr-shell --app-menu
        add_main_option_entry(Gtk::Application::OptionType::BOOL, "app-menu", 'm',
                              "Toggle the bar's app menu in the running instance");
        add_action("app-menu", [this] {
            if (bar_)
                bar_->toggle_app_menu();
        });
        // `hypr-shell --session` toggles the session menu: the fullscreen
        // window or the bar module's dropdown, per session.mode
        add_main_option_entry(Gtk::Application::OptionType::BOOL, "session", 's',
                              "Toggle the session menu in the running instance");
        add_action("session", [this] {
            if (Config::get().session().mode == Config::Session::Mode::Fullscreen) {
                if (session_window_)
                    session_window_->toggle();
            } else if (bar_) {
                bar_->toggle_session_menu();
            }
        });
        // `hypr-shell --lock` locks the session (ext-session-lock), e.g.
        //   bind = SUPER, L, exec, hypr-shell --lock
        add_main_option_entry(Gtk::Application::OptionType::BOOL, "lock", 'k',
                              "Lock the screen in the running instance");
        add_action("lock", [] { request_lock(); });
        // `hypr-shell --lock-and-suspend` for the lid switch
        add_main_option_entry(Gtk::Application::OptionType::BOOL, "lock-and-suspend", 'S',
                              "Lock the screen, then suspend");
        add_action("lock-and-suspend", [] { lock_and_suspend(); });
        // `hypr-shell --control-center` toggles the bar's control center popover
        add_main_option_entry(Gtk::Application::OptionType::BOOL, "control-center", 'C',
                              "Toggle the control center in the running instance");
        add_action("control-center", [this] {
            if (bar_)
                bar_->toggle_control_center();
        });
        // `hypr-shell --wallpaper-next` picks the next wallpaper (slideshow order)
        add_main_option_entry(Gtk::Application::OptionType::BOOL, "wallpaper-next", 'w',
                              "Switch to the next wallpaper");
        add_action("wallpaper-next", [] { Wallpaper::get().next(); });
    }

    // Runs in the *invoking* process before activate: forward --launcher to
    // the primary instance (GApplication uniqueness) and exit.
    int on_handle_local_options(const Glib::RefPtr<Glib::VariantDict>& options) override {
        bool launcher = false, app_menu = false, session = false, lock = false, clipboard = false;
        bool lock_suspend = false, control_center = false, wallpaper_next = false;
        if (options) {
            options->lookup_value("launcher", launcher);
            options->lookup_value("clipboard", clipboard);
            options->lookup_value("app-menu", app_menu);
            options->lookup_value("session", session);
            options->lookup_value("lock", lock);
            options->lookup_value("lock-and-suspend", lock_suspend);
            options->lookup_value("control-center", control_center);
            options->lookup_value("wallpaper-next", wallpaper_next);
        }
        if (launcher || app_menu || session || lock || clipboard || lock_suspend ||
            control_center || wallpaper_next) {
            try {
                register_application();
            } catch (const Glib::Error& e) {
                g_warning("could not register application: %s", e.what());
                return 1;
            }
            if (is_remote()) {
                if (launcher)
                    activate_action("launcher");
                if (clipboard)
                    activate_action("clipboard");
                if (app_menu)
                    activate_action("app-menu");
                if (session)
                    activate_action("session");
                if (lock)
                    activate_action("lock");
                if (lock_suspend)
                    activate_action("lock-and-suspend");
                if (control_center)
                    activate_action("control-center");
                if (wallpaper_next)
                    activate_action("wallpaper-next");
                return 0; // handled — don't start a second shell
            }
            // no running instance: start up normally, then open what was asked
            open_launcher_on_startup_ = launcher;
            open_clipboard_on_startup_ = clipboard;
            open_app_menu_on_startup_ = app_menu;
            open_session_on_startup_ = session;
            lock_on_startup_ = lock;
        }
        return -1; // continue normal startup
    }

    void on_activate() override {
        // GApplication uniqueness: a second launch only pokes this instance.
        if (!get_windows().empty()) {
            g_message("hypr-shell is already running");
            return;
        }

        if (!gtk_layer_is_supported()) {
            std::cerr << "hypr-shell: wlr-layer-shell unavailable — must run inside a Wayland compositor\n";
            quit();
            return;
        }

        // A shell must outlive its windows: bar.visibility = "hidden" unmaps
        // every window, and GTK would otherwise quit the application.
        hold();

        load_css();
        apply_theme();
        Config::get().signal_changed().connect(
            sigc::mem_fun(*this, &App::apply_theme));
        bar_ = std::make_unique<Bar>();
        add_window(*bar_);
        // bar.visibility = "hidden" starts the shell with no bar mapped;
        // the Bar itself re-shows when the config changes.
        if (Config::get().bar_visibility() != Config::BarVisibility::Hidden)
            bar_->present();
        if (open_app_menu_on_startup_)
            Glib::signal_timeout().connect_once([this] { bar_->toggle_app_menu(); }, 800);

        // desktop wallpaper: one background layer window per monitor, fed by
        // the Wallpaper service (directory scan, slideshow, persisted pick)
        wallpaper_ = std::make_unique<WallpaperManager>(*this);

        // notification toasts; presents itself while popups exist
        popups_ = std::make_unique<NotificationPopups>();
        add_window(*popups_);

        // on-screen display for volume / mic / brightness / lock keys;
        // presents itself for 2s on each change
        osd_window_ = std::make_unique<OsdWindow>();
        add_window(*osd_window_);
        // dev hook: HS_OSD_SHOW=volume|input|brightness|lock shows that OSD
        // shortly after startup (bypasses the 2s startup grace)
        if (const char* key = g_getenv("HS_OSD_SHOW")) {
            const auto type = Osd::type_from_key(key);
            Glib::signal_timeout().connect_once([type] { Osd::get().show(type); }, 1200);
        }

        // fullscreen session menu; hidden until toggled (session.mode = fullscreen)
        session_window_ = std::make_unique<SessionWindow>();
        add_window(*session_window_);
        // dev hook: HS_OPEN_SESSION=1 opens the session menu (either mode)
        if (open_session_on_startup_ || g_getenv("HS_OPEN_SESSION") != nullptr)
            Glib::signal_timeout().connect_once([this] { activate_action("session"); }, 800);

        // app launcher overlay; hidden until toggled
        launcher_window_ = std::make_unique<LauncherWindow>();
        add_window(*launcher_window_);

        // clipboard history overlay (cliphist); hidden until toggled
        clipboard_window_ = std::make_unique<ClipboardWindow>();
        add_window(*clipboard_window_);
        // dev hook: HS_OPEN_CLIPBOARD=1 opens it after startup (>1 = delay in ms)
        if (open_clipboard_on_startup_ || g_getenv("HS_OPEN_CLIPBOARD") != nullptr) {
            const char* hook = g_getenv("HS_OPEN_CLIPBOARD");
            Glib::signal_timeout().connect_once([this] { clipboard_window_->open(); },
                                                std::max(400, hook ? atoi(hook) : 0));
        }

        // lock screen (ext-session-lock + PAM); answers request_lock() from the
        // idle daemon, the session menus, --lock and logind's Lock signal
        lock_screen_ = std::make_unique<LockScreen>();
        // dev hook: HS_LOCK_PREVIEW=1 shows the lock UI as a plain overlay
        // window (Escape on the cover closes it) — no real session lock
        if (g_getenv("HS_LOCK_PREVIEW") != nullptr)
            Glib::signal_timeout().connect_once([this] { lock_screen_->open_preview(); }, 600);
        if (lock_on_startup_)
            Glib::signal_timeout().connect_once([this] { lock_screen_->lock(); }, 600);

        // night light: hyprsunset scheduled by sunrise/sunset (or forced)
        NightLight::get();
        // low / critical battery notifications
        BatteryAlerts::get();

        // idle daemon (ext-idle-notify-v1) and its fade-to-black grace overlay
        Idle::get();
        idle_fade_ = std::make_unique<IdleFade>();
        add_window(*idle_fade_);
        // dev hook: HS_IDLE_SIMULATE=screen_off|lock|suspend drives that stage
        // as if the seat went idle (1.5s after startup) and
        // HS_IDLE_SIMULATE_RESUME=<ms> ends it that long after — pair with
        // HS_IDLE_DRY_RUN=1 so nothing really blanks, locks or suspends.
        if (const char* stage_key = g_getenv("HS_IDLE_SIMULATE")) {
            const auto stage = Idle::stage_from_key(stage_key);
            Glib::signal_timeout().connect_once([stage] { Idle::get().simulate_idled(stage); },
                                                1500);
            if (const char* resume = g_getenv("HS_IDLE_SIMULATE_RESUME"))
                Glib::signal_timeout().connect_once([] { Idle::get().simulate_resumed(); },
                                                    1500 + static_cast<unsigned>(atoi(resume)));
        }
        // dev hook: HS_OPEN_LAUNCHER=1 opens it shortly after startup
        if (open_launcher_on_startup_ || g_getenv("HS_OPEN_LAUNCHER") != nullptr)
            Glib::signal_timeout().connect_once([this] { launcher_window_->open(); },
                                                400);
    }

private:
    void load_css() {
        auto display = Gdk::Display::get_default();

        auto provider = Gtk::CssProvider::create();
        provider->load_from_resource("/dev/hyprshell/Shell/style.css");
        Gtk::StyleProvider::add_provider_for_display(
            display, provider, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

        // User override, hot-reloaded so CSS can be tweaked live.
        user_css_path_ = Glib::build_filename(
            Glib::get_user_config_dir(), "hypr-shell", "style.css");
        user_provider_ = Gtk::CssProvider::create();
        Gtk::StyleProvider::add_provider_for_display(
            display, user_provider_, GTK_STYLE_PROVIDER_PRIORITY_USER);
        reload_user_css();

        auto file = Gio::File::create_for_path(user_css_path_);
        css_monitor_ = file->monitor_file();
        css_monitor_->signal_changed().connect(
            [this](const Glib::RefPtr<Gio::File>&, const Glib::RefPtr<Gio::File>&,
                   Gio::FileMonitor::Event event) {
                if (event == Gio::FileMonitor::Event::CHANGES_DONE_HINT ||
                    event == Gio::FileMonitor::Event::CREATED ||
                    event == Gio::FileMonitor::Event::DELETED) {
                    reload_user_css();
                }
            });
    }

    // Theme provider: the palette tokens (@define-color, redefining the
    // defaults declared in data/style.css), the text font and
    // bar.background_opacity, regenerated on every config reload. Sits just
    // above the built-in theme so ~/.config/hypr-shell/style.css (USER
    // priority) still overrides everything.
    void apply_theme() {
        if (!theme_provider_) {
            theme_provider_ = Gtk::CssProvider::create();
            Gtk::StyleProvider::add_provider_for_display(
                Gdk::Display::get_default(), theme_provider_,
                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
        }
        // g_ascii_dtostr: locale-independent "0.88" (never a decimal comma)
        char buf[G_ASCII_DTOSTR_BUF_SIZE];
        g_ascii_dtostr(buf, sizeof buf, Config::get().bar_background_opacity());
        // the font name is quoted; a stray quote inside would break the sheet
        std::string font = Theme::get().font();
        font.erase(std::remove(font.begin(), font.end(), '"'), font.end());
        std::string css = Theme::get().css();
        css += Glib::ustring::compose("window, popover { font-family: \"%1\"; }\n", font);
        css += Glib::ustring::compose(".bar-inner { background-color: alpha(@mSurface, %1); }\n",
                                      buf);
        theme_provider_->load_from_data(css);
    }

    void reload_user_css() {
        try {
            if (Glib::file_test(user_css_path_, Glib::FileTest::EXISTS)) {
                user_provider_->load_from_path(user_css_path_);
            } else {
                user_provider_->load_from_data("");
            }
        } catch (const Glib::Error& e) {
            g_warning("user css reload failed: %s", e.what());
        }
    }

    std::unique_ptr<Bar> bar_;
    std::unique_ptr<WallpaperManager> wallpaper_;
    std::unique_ptr<NotificationPopups> popups_;
    std::unique_ptr<OsdWindow> osd_window_;
    std::unique_ptr<LauncherWindow> launcher_window_;
    std::unique_ptr<ClipboardWindow> clipboard_window_;
    std::unique_ptr<SessionWindow> session_window_;
    std::unique_ptr<IdleFade> idle_fade_;
    std::unique_ptr<LockScreen> lock_screen_;
    bool lock_on_startup_ = false;
    bool open_launcher_on_startup_ = false;
    bool open_clipboard_on_startup_ = false;
    bool open_app_menu_on_startup_ = false;
    bool open_session_on_startup_ = false;
    Glib::RefPtr<Gtk::CssProvider> theme_provider_;
    Glib::RefPtr<Gtk::CssProvider> user_provider_;
    Glib::RefPtr<Gio::FileMonitor> css_monitor_;
    std::string user_css_path_;
};

} // namespace hyprshell

int main(int argc, char* argv[]) {
    return hyprshell::App::create()->run(argc, argv);
}
