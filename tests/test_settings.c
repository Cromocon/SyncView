#include "core/settings.h"

#include <glib.h>
#include <string.h>

int
main(void)
{
    g_assert_cmpint(SYNCVIEW_MAX_VIDEOS, ==, 4);

    g_assert_cmpint(syncview_supported_video_extensions_count, ==, 6);
    g_assert_cmpstr(syncview_supported_video_extensions[0], ==, "mp4");
    g_assert_cmpstr(syncview_supported_video_extensions[5], ==, "flv");

    g_assert_cmpint(syncview_fps_presets_count, ==, 7);
    g_assert_true(syncview_fps_presets[0] == 24.0);
    g_assert_true(syncview_fps_presets[6] == 60.0);

    g_assert_cmpint(syncview_frame_step_options_count, ==, 3);
    g_assert_cmpint(syncview_frame_step_options_ms[0], ==, 40);
    g_assert_cmpint(syncview_frame_step_options_ms[2], ==, 200);

    g_assert_cmpint(SYNCVIEW_FRAME_STEP_DEFAULT_MS, ==, 40);
    g_assert_cmpint(SYNCVIEW_DEFAULT_EXPORT_WINDOW_SEC, ==, 5);
    g_assert_true(SYNCVIEW_ZOOM_MIN == 1.0);
    g_assert_true(SYNCVIEW_ZOOM_MAX == 5.0);

    return 0;
}
