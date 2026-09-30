#include "core/sync_manager.h"

#include <string.h>

void
sync_manager_init(SyncManager *sm, int num_players)
{
    sm->sync_enabled = true;
    sm->num_players = num_players;
    memset(sm->video_offsets_ms, 0, sizeof(sm->video_offsets_ms));
    sm->master_video_index = 0;
}

void
sync_manager_set_enabled(SyncManager *sm, bool enabled)
{
    sm->sync_enabled = enabled;
}

bool
sync_manager_is_enabled(const SyncManager *sm)
{
    return sm->sync_enabled;
}

void
sync_manager_set_offset(SyncManager *sm, int video_index, int64_t offset_ms)
{
    if (video_index < 0 || video_index >= SYNCVIEW_MAX_VIDEOS) {
        return;
    }
    sm->video_offsets_ms[video_index] = offset_ms;
}

int64_t
sync_manager_get_offset(const SyncManager *sm, int video_index)
{
    if (video_index < 0 || video_index >= SYNCVIEW_MAX_VIDEOS) {
        return 0;
    }
    return sm->video_offsets_ms[video_index];
}

void
sync_manager_reset_offsets(SyncManager *sm)
{
    memset(sm->video_offsets_ms, 0, sizeof(sm->video_offsets_ms));
}

void
sync_manager_set_master(SyncManager *sm, int video_index)
{
    if (video_index < 0 || video_index >= SYNCVIEW_MAX_VIDEOS) {
        return;
    }
    sm->master_video_index = video_index;
}

int
sync_manager_get_master(const SyncManager *sm)
{
    return sm->master_video_index;
}

int64_t
sync_manager_calculate_sync_position(const SyncManager *sm,
                                      int64_t source_position_ms,
                                      int source_index,
                                      int target_index)
{
    int64_t source_offset = sync_manager_get_offset(sm, source_index);
    int64_t target_offset = sync_manager_get_offset(sm, target_index);

    int64_t sync_position = source_position_ms - source_offset + target_offset;

    return sync_position < 0 ? 0 : sync_position;
}

void
sync_manager_sync_all_to_master(SyncManager *sm,
                                 int64_t master_position_ms,
                                 SyncPlayerOps players[],
                                 int n_players)
{
    for (int i = 0; i < n_players; i++) {
        SyncPlayerOps *ops = &players[i];

        if (ops->is_loaded == NULL || !ops->is_loaded(ops->user_data)) {
            continue;
        }

        if (i != sm->master_video_index) {
            int64_t sync_pos = sync_manager_calculate_sync_position(
                sm, master_position_ms, sm->master_video_index, i);
            if (ops->seek) {
                ops->seek(ops->user_data, sync_pos);
            }
            if (ops->pause) {
                ops->pause(ops->user_data);
            }
        } else {
            if (ops->pause) {
                ops->pause(ops->user_data);
            }
        }
    }
}
