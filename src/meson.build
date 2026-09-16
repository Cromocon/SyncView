syncview_core_inc = include_directories('.')

syncview_core_sources = [
  'util/time_format.c',
  'core/settings.c',
]

syncview_core_lib = static_library(
  'syncview_core',
  syncview_core_sources,
  include_directories: syncview_core_inc,
)

syncview_sources = [
  'main.c',
]

gtk_dep = dependency('gtk4', version: '>=4.10')
gstreamer_dep = dependency('gstreamer-1.0')
gstreamer_pbutils_dep = dependency('gstreamer-pbutils-1.0')
gstreamer_video_dep = dependency('gstreamer-video-1.0')
sqlite_dep = dependency('sqlite3')
glib_dep = dependency('glib-2.0')
gio_dep = dependency('gio-2.0')
json_glib_dep = dependency('json-glib-1.0')

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
  link_with: syncview_core_lib,
  dependencies: syncview_deps,
)
