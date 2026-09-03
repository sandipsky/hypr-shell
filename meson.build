project(
  'hypr-shell',
  ['c', 'cpp'],
  version: '0.1.0',
  meson_version: '>= 0.62.0',
  default_options: [
    'warning_level=2',
    'cpp_std=c++20',
    'buildtype=debugoptimized',
  ],
)

gnome = import('gnome')

# C is only used for the generated GResource file; keep its warnings quiet
add_project_arguments('-w', language: 'c')

dependencies = [
  dependency('gtkmm-4.0'),
  dependency('gtk4-layer-shell-0'),
  dependency('nlohmann_json'),
  dependency('libpulse'),
  dependency('libpulse-mainloop-glib'),
  dependency('wayland-client'),
]

# ext-idle-notify-v1 client glue for the idle service, generated at build time
# from the wayland-protocols XML (same approach as gtk4-layer-shell / Waybar).
wayland_protocols = dependency('wayland-protocols')
wayland_scanner = find_program('wayland-scanner')
idle_notify_xml = (wayland_protocols.get_variable('pkgdatadir') / 'staging' / 'ext-idle-notify'
                   / 'ext-idle-notify-v1.xml')
idle_notify_glue = [
  custom_target(
    'ext-idle-notify-v1-protocol.c',
    input: idle_notify_xml,
    output: 'ext-idle-notify-v1-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
  ),
  custom_target(
    'ext-idle-notify-v1-client-protocol.h',
    input: idle_notify_xml,
    output: 'ext-idle-notify-v1-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
  ),
]

sources = files(
  'src/main.cpp',
  'src/bar/bar.cpp',
  'src/bar/app_menu_panel.cpp',
  'src/bar/audio_panel.cpp',
  'src/bar/battery_panel.cpp',
  'src/bar/bluetooth_panel.cpp',
  'src/bar/network_panel.cpp',
  'src/bar/notification_panel.cpp',
  'src/bar/notification_popup.cpp',
  'src/bar/notification_ui.cpp',
  'src/bar/session_menu.cpp',
  'src/bar/session_window.cpp',
  'src/bar/calendar.cpp',
  'src/bar/idle_fade.cpp',
  'src/bar/launcher_window.cpp',
  'src/services/apps.cpp',
  'src/services/bluez.cpp',
  'src/services/brightness.cpp',
  'src/services/config.cpp',
  'src/bar/modules/active_window.cpp',
  'src/bar/modules/app_menu.cpp',
  'src/bar/modules/battery.cpp',
  'src/bar/modules/bluetooth.cpp',
  'src/bar/modules/clock.cpp',
  'src/bar/modules/launcher.cpp',
  'src/bar/modules/network.cpp',
  'src/bar/modules/notifications.cpp',
  'src/bar/modules/session.cpp',
  'src/bar/modules/volume.cpp',
  'src/bar/modules/workspaces.cpp',
  'src/services/hyprland.cpp',
  'src/services/idle.cpp',
  'src/services/math_eval.cpp',
  'src/services/network_manager.cpp',
  'src/services/notifications.cpp',
  'src/services/power_profiles.cpp',
  'src/services/pulse.cpp',
  'src/services/session.cpp',
  'src/services/upower.cpp',
)

# Icon fonts. Tabler (wifi/volume glyphs) is MIT; SegoeIcons.ttf (battery
# glyphs) is Microsoft-proprietary — personal use only, see SegoeIcons-NOTICE.txt.
# Default notification sound (from Noctalia, MIT).
install_data(
  'data/sounds/notification-generic.wav',
  install_dir: get_option('datadir') / 'hypr-shell' / 'sounds',
)

install_data(
  'data/fonts/noctalia-tabler-icons.ttf',
  'data/fonts/tabler-icons-license.txt',
  'data/fonts/SegoeIcons.ttf',
  'data/fonts/SegoeIcons-NOTICE.txt',
  install_dir: get_option('datadir') / 'fonts' / 'hypr-shell',
)

resources = gnome.compile_resources(
  'hypr-shell-resources',
  'data/hypr-shell.gresource.xml',
  source_dir: 'data',
)

executable(
  'hypr-shell',
  sources,
  resources,
  idle_notify_glue,
  dependencies: dependencies,
  include_directories: include_directories('src'),
  install: true,
)

# Settings app: plain C++ over the libadwaita C API (no gtkmm — see CLAUDE.md).
# Talks to the shell only through config.json, which the shell hot-reloads.
executable(
  'hypr-shell-settings',
  files('src/settings/main.cpp'),
  dependencies: [
    dependency('libadwaita-1', version: '>= 1.4'),
    dependency('nlohmann_json'),
  ],
  include_directories: include_directories('src'),
  install: true,
)

# Launchers resolve Exec against the session PATH, which does not include
# ~/.local/bin — bake the absolute bindir into the desktop entry.
desktop_conf = configuration_data()
desktop_conf.set('bindir', get_option('prefix') / get_option('bindir'))
install_data(
  configure_file(
    input: 'data/hypr-shell-settings.desktop.in',
    output: 'hypr-shell-settings.desktop',
    configuration: desktop_conf,
  ),
  install_dir: get_option('datadir') / 'applications',
)
