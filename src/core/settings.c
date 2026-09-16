#include "core/settings.h"

const char *const syncview_supported_video_extensions[] = {
    "mp4", "avi", "mov", "mkv", "wmv", "flv",
};
const size_t syncview_supported_video_extensions_count =
    sizeof(syncview_supported_video_extensions) / sizeof(syncview_supported_video_extensions[0]);

const double syncview_fps_presets[] = {
    24.0, 25.0, 29.97, 30.0, 50.0, 59.94, 60.0,
};
const size_t syncview_fps_presets_count =
    sizeof(syncview_fps_presets) / sizeof(syncview_fps_presets[0]);

const int syncview_frame_step_options_ms[] = {
    40, 100, 200,
};
const size_t syncview_frame_step_options_count =
    sizeof(syncview_frame_step_options_ms) / sizeof(syncview_frame_step_options_ms[0]);
