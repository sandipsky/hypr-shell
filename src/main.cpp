#include "bar/bar.hpp"
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
    App() : Gtk::Application("dev.hyprshell.Shell") {}

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
        bar_ = std::make_unique<Bar>();
        add_window(*bar_);
        // bar.visibility = "hidden" starts the shell with no bar mapped;
        // the Bar itself re-shows when the config changes.
        if (Config::get().bar_visibility() != Config::BarVisibility::Hidden)
            bar_->present();
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
    Glib::RefPtr<Gtk::CssProvider> user_provider_;
    Glib::RefPtr<Gio::FileMonitor> css_monitor_;
    std::string user_css_path_;
};

} // namespace hyprshell

int main(int argc, char* argv[]) {
    return hyprshell::App::create()->run(argc, argv);
}
