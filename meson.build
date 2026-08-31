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
]

sources = files(
  'src/main.cpp',
  'src/bar/bar.cpp',
  'src/bar/audio_panel.cpp',
  'src/bar/battery_panel.cpp',
  'src/bar/calendar.cpp',
  'src/services/brightness.cpp',
  'src/services/config.cpp',
  'src/bar/modules/active_window.cpp',
  'src/bar/modules/battery.cpp',
  'src/bar/modules/clock.cpp',
  'src/bar/modules/network.cpp',
  'src/bar/modules/volume.cpp',
  'src/bar/modules/workspaces.cpp',
  'src/services/hyprland.cpp',
  'src/services/network_manager.cpp',
  'src/services/power_profiles.cpp',
  'src/services/pulse.cpp',
  'src/services/upower.cpp',
)

# Icon fonts. Tabler (wifi/volume glyphs) is MIT; SegoeIcons.ttf (battery
# glyphs) is Microsoft-proprietary — personal use only, see SegoeIcons-NOTICE.txt.
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
