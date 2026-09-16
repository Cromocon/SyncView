#include "core/settings.h"

#include <assert.h>
#include <string.h>

int
main(void)
{
    assert(SYNCVIEW_MAX_VIDEOS == 4);

    assert(syncview_supported_video_extensions_count == 6);
    assert(strcmp(syncview_supported_video_extensions[0], "mp4") == 0);
    assert(strcmp(syncview_supported_video_extensions[5], "flv") == 0);

    assert(syncview_fps_presets_count == 7);
    assert(syncview_fps_presets[0] == 24.0);
    assert(syncview_fps_presets[6] == 60.0);

    assert(syncview_frame_step_options_count == 3);
    assert(syncview_frame_step_options_ms[0] == 40);
    assert(syncview_frame_step_options_ms[2] == 200);

    assert(SYNCVIEW_FRAME_STEP_DEFAULT_MS == 40);
    assert(SYNCVIEW_DEFAULT_EXPORT_WINDOW_SEC == 5);
    assert(SYNCVIEW_ZOOM_MIN == 1.0);
    assert(SYNCVIEW_ZOOM_MAX == 5.0);

    return 0;
}
