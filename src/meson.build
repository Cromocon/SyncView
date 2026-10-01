syncview_core_inc = include_directories('.')

# 4.12: gtk_css_provider_load_from_string (il tema è CSS incorporato). GtkFileDialog (4.10) è coperto.
gtk_dep = dependency('gtk4', version: '>=4.12')
gstreamer_dep = dependency('gstreamer-1.0')
gstreamer_pbutils_dep = dependency('gstreamer-pbutils-1.0')
gstreamer_video_dep = dependency('gstreamer-video-1.0')
sqlite_dep = dependency('sqlite3')
glib_dep = dependency('glib-2.0')
gio_dep = dependency('gio-2.0')
json_glib_dep = dependency('json-glib-1.0')

syncview_core_sources = [
  'util/time_format.c',
  'core/settings.c',
  'core/sync_manager.c',
  'core/markers.c',
  'core/marker_db.c',
  'core/user_paths.c',
  'core/logger.c',
  'core/discoverer.c',
  'core/deps_check.c',
]

syncview_core_lib = static_library(
  'syncview_core',
  syncview_core_sources,
  include_directories: syncview_core_inc,
  dependencies: [glib_dep, gio_dep, sqlite_dep, json_glib_dep, gstreamer_dep, gstreamer_pbutils_dep],
)

# Parte video: dipende da GTK/GDK, quindi separata da libsyncview_core (che resta senza GTK).
syncview_video_lib = static_library(
  'syncview_video',
  ['video/video_player.c'],
  include_directories: syncview_core_inc,
  link_with: syncview_core_lib,
  dependencies: [glib_dep, gtk_dep, gstreamer_dep, gstreamer_video_dep],
)

# Interfaccia: tema (CSS generato da design/tokens_tool.py, incorporato come risorsa) e finestra principale.
gnome = import('gnome')
syncview_resources = gnome.compile_resources(
  'syncview-resources',
  'ui/syncview.gresource.xml',
  source_dir: [meson.project_source_root() / 'design' / 'generated', meson.current_source_dir() / 'ui'],
  c_name: 'syncview',
)

syncview_ui_lib = static_library(
  'syncview_ui',
  ['ui/theme.c', 'ui/main_window.c', 'ui/debug_log_window.c', 'ui/debug_modules_window.c', 'ui/debug_windows.c', syncview_resources],
  include_directories: syncview_core_inc,
  link_with: [syncview_video_lib, syncview_core_lib],
  dependencies: [glib_dep, gio_dep, gtk_dep, gstreamer_dep],
)

syncview_sources = [
  'main.c',
]

syncview_deps = [
  gtk_dep,
  gstreamer_dep,
  gstreamer_pbutils_dep,
  gstreamer_video_dep,
  sqlite_dep,
  glib_dep,
  gio_dep,
  json_glib_dep,
]

executable(
  'syncview',
  syncview_sources,
  include_directories: syncview_core_inc,
  link_with: [syncview_ui_lib, syncview_video_lib, syncview_core_lib],
  dependencies: syncview_deps,
)
