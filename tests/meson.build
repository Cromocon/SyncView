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
