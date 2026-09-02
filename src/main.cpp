#include "bar/bar.hpp"
#include "bar/launcher_window.hpp"
#include "bar/notification_popup.hpp"
#include "services/config.hpp"

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
    }

    // Runs in the *invoking* process before activate: forward --launcher to
    // the primary instance (GApplication uniqueness) and exit.
    int on_handle_local_options(const Glib::RefPtr<Glib::VariantDict>& options) override {
        bool launcher = false;
        if (options && options->lookup_value("launcher", launcher) && launcher) {
            try {
                register_application();
            } catch (const Glib::Error& e) {
                g_warning("could not register application: %s", e.what());
                return 1;
            }
            if (is_remote()) {
                activate_action("launcher");
                return 0; // handled — don't start a second shell
            }
            // no running instance: start up normally, then open the launcher
            open_launcher_on_startup_ = true;
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

        // notification toasts; presents itself while popups exist
        popups_ = std::make_unique<NotificationPopups>();
        add_window(*popups_);

        // app launcher overlay; hidden until toggled
        launcher_window_ = std::make_unique<LauncherWindow>();
        add_window(*launcher_window_);
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
    std::unique_ptr<LauncherWindow> launcher_window_;
    bool open_launcher_on_startup_ = false;
    Glib::RefPtr<Gtk::CssProvider> opacity_provider_;
    Glib::RefPtr<Gtk::CssProvider> user_provider_;
    Glib::RefPtr<Gio::FileMonitor> css_monitor_;
    std::string user_css_path_;
};

} // namespace hyprshell

int main(int argc, char* argv[]) {
    return hyprshell::App::create()->run(argc, argv);
}
