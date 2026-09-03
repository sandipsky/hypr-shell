#include "bar/bar.hpp"
#include "bar/idle_fade.hpp"
#include "bar/launcher_window.hpp"
#include "bar/lock_screen.hpp"
#include "bar/notification_popup.hpp"
#include "bar/osd_window.hpp"
#include "bar/session_window.hpp"
#include "services/config.hpp"
#include "services/idle.hpp"
#include "services/osd.hpp"
#include "services/session.hpp"

#include <gtk4-layer-shell.h>
#include <gtkmm.h>

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
    }

    // Runs in the *invoking* process before activate: forward --launcher to
    // the primary instance (GApplication uniqueness) and exit.
    int on_handle_local_options(const Glib::RefPtr<Glib::VariantDict>& options) override {
        bool launcher = false, app_menu = false, session = false, lock = false;
        if (options) {
            options->lookup_value("launcher", launcher);
            options->lookup_value("app-menu", app_menu);
            options->lookup_value("session", session);
            options->lookup_value("lock", lock);
        }
        if (launcher || app_menu || session || lock) {
            try {
                register_application();
            } catch (const Glib::Error& e) {
                g_warning("could not register application: %s", e.what());
                return 1;
            }
            if (is_remote()) {
                if (launcher)
                    activate_action("launcher");
                if (app_menu)
                    activate_action("app-menu");
                if (session)
                    activate_action("session");
                if (lock)
                    activate_action("lock");
                return 0; // handled — don't start a second shell
            }
            // no running instance: start up normally, then open what was asked
            open_launcher_on_startup_ = launcher;
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
        apply_bar_opacity();
        Config::get().signal_changed().connect(
            sigc::mem_fun(*this, &App::apply_bar_opacity));
        bar_ = std::make_unique<Bar>();
        add_window(*bar_);
        // bar.visibility = "hidden" starts the shell with no bar mapped;
        // the Bar itself re-shows when the config changes.
        if (Config::get().bar_visibility() != Config::BarVisibility::Hidden)
            bar_->present();
        if (open_app_menu_on_startup_)
            Glib::signal_timeout().connect_once([this] { bar_->toggle_app_menu(); }, 800);

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

        // lock screen (ext-session-lock + PAM); answers request_lock() from the
        // idle daemon, the session menus, --lock and logind's Lock signal
        lock_screen_ = std::make_unique<LockScreen>();
        // dev hook: HS_LOCK_PREVIEW=1 shows the lock UI as a plain overlay
        // window (Escape on the cover closes it) — no real session lock
        if (g_getenv("HS_LOCK_PREVIEW") != nullptr)
            Glib::signal_timeout().connect_once([this] { lock_screen_->open_preview(); }, 600);
        if (lock_on_startup_)
            Glib::signal_timeout().connect_once([this] { lock_screen_->lock(); }, 600);

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

    // bar.background_opacity, applied as a one-rule CSS provider just above
    // the built-in theme so ~/.config/hypr-shell/style.css (USER priority)
    // still overrides it. #11111b is the default theme's bar color — becomes
    // a theme token when theming lands.
    void apply_bar_opacity() {
        if (!opacity_provider_) {
            opacity_provider_ = Gtk::CssProvider::create();
            Gtk::StyleProvider::add_provider_for_display(
                Gdk::Display::get_default(), opacity_provider_,
                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
        }
        // g_ascii_dtostr: locale-independent "0.88" (never a decimal comma)
        char buf[G_ASCII_DTOSTR_BUF_SIZE];
        g_ascii_dtostr(buf, sizeof buf, Config::get().bar_background_opacity());
        opacity_provider_->load_from_data(Glib::ustring::compose(
            ".bar-inner { background-color: alpha(#11111b, %1); }", buf));
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
    std::unique_ptr<NotificationPopups> popups_;
    std::unique_ptr<OsdWindow> osd_window_;
    std::unique_ptr<LauncherWindow> launcher_window_;
    std::unique_ptr<SessionWindow> session_window_;
    std::unique_ptr<IdleFade> idle_fade_;
    std::unique_ptr<LockScreen> lock_screen_;
    bool lock_on_startup_ = false;
    bool open_launcher_on_startup_ = false;
    bool open_app_menu_on_startup_ = false;
    bool open_session_on_startup_ = false;
    Glib::RefPtr<Gtk::CssProvider> opacity_provider_;
    Glib::RefPtr<Gtk::CssProvider> user_provider_;
    Glib::RefPtr<Gio::FileMonitor> css_monitor_;
    std::string user_css_path_;
};

} // namespace hyprshell

int main(int argc, char* argv[]) {
    return hyprshell::App::create()->run(argc, argv);
}
