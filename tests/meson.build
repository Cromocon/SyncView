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
