test_dummy = executable(
  'test_dummy',
  'test_dummy.c',
)
test('dummy', test_dummy)

test_time_format = executable(
  'test_time_format',
  'test_time_format.c',
  include_directories: syncview_core_inc,
  link_with: syncview_core_lib,
)
test('time_format', test_time_format)

test_settings = executable(
  'test_settings',
  'test_settings.c',
  include_directories: syncview_core_inc,
  link_with: syncview_core_lib,
)
test('settings', test_settings)

test_sync_manager = executable(
  'test_sync_manager',
  'test_sync_manager.c',
  include_directories: syncview_core_inc,
  link_with: syncview_core_lib,
)
test('sync_manager', test_sync_manager)

test_markers = executable(
  'test_markers',
  'test_markers.c',
  include_directories: syncview_core_inc,
  link_with: syncview_core_lib,
  dependencies: [glib_dep],
)
test('markers', test_markers)

test_marker_db = executable(
  'test_marker_db',
  'test_marker_db.c',
  include_directories: syncview_core_inc,
  link_with: syncview_core_lib,
  dependencies: [glib_dep, sqlite_dep, json_glib_dep],
)
test('marker_db', test_marker_db)

test_user_paths = executable(
  'test_user_paths',
  'test_user_paths.c',
  include_directories: syncview_core_inc,
  link_with: syncview_core_lib,
  dependencies: [glib_dep, json_glib_dep],
)
test('user_paths', test_user_paths)

test_logger = executable(
  'test_logger',
  'test_logger.c',
  include_directories: syncview_core_inc,
  link_with: syncview_core_lib,
  dependencies: [glib_dep],
)
test('logger', test_logger)

# Vincolo della modalità debug: nessun output/controllo debug ad-hoc fuori da core/logger.c.
python = import('python').find_installation('python3')
test(
  'no_adhoc_logging',
  python,
  args: [files('check_no_adhoc_logging.py'), meson.project_source_root() / 'src'],
)

test_module_logging = executable(
  'test_module_logging',
  'test_module_logging.c',
  include_directories: syncview_core_inc,
  link_with: syncview_core_lib,
  dependencies: [glib_dep, sqlite_dep, json_glib_dep],
)
test('module_logging', test_module_logging)

test_logger_filter = executable(
  'test_logger_filter',
  'test_logger_filter.c',
  include_directories: syncview_core_inc,
  link_with: syncview_core_lib,
  dependencies: [glib_dep],
)
test('logger_filter', test_logger_filter)

test_discoverer = executable(
  'test_discoverer',
  'test_discoverer.c',
  include_directories: syncview_core_inc,
  link_with: syncview_core_lib,
  dependencies: [glib_dep, gio_dep, gstreamer_dep, gstreamer_pbutils_dep],
)
# I plugin GStreamer caricano driver GPU di terze parti che perdono memoria (vedi lsan.supp):
# la soppressione vale solo quando i test girano con -Db_sanitize=address.
test(
  'discoverer',
  test_discoverer,
  timeout: 120,
  env: {'LSAN_OPTIONS': 'suppressions=' + meson.current_source_dir() / 'lsan.supp'},
)

# Richiede display GTK e gtk4paintablesink: senza, esce con 77 (skip).
test_video_player = executable(
  'test_video_player',
  'test_video_player.c',
  include_directories: syncview_core_inc,
  link_with: [syncview_video_lib, syncview_core_lib],
  dependencies: [glib_dep, gtk_dep, gstreamer_dep, gstreamer_video_dep],
)
test(
  'video_player',
  test_video_player,
  timeout: 60,
  env: {'LSAN_OPTIONS': 'suppressions=' + meson.current_source_dir() / 'lsan.supp'},
)

test_deps_check = executable(
  'test_deps_check',
  'test_deps_check.c',
  include_directories: syncview_core_inc,
  link_with: syncview_core_lib,
  dependencies: [glib_dep, gio_dep, gstreamer_dep],
)
test('deps_check', test_deps_check, timeout: 60)
