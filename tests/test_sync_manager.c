#include "core/sync_manager.h"

#include <assert.h>
#include <stddef.h>

/* --- Mock player per sync_manager_sync_all_to_master --- */

typedef struct {
    bool loaded;
    bool seek_called;
    int64_t seek_position_ms;
    bool pause_called;
    int seek_order;  /* -1 se mai chiamato */
    int pause_order; /* -1 se mai chiamato */
} MockPlayer;

static int g_call_counter;

static void
mock_player_reset(MockPlayer *p, bool loaded)
{
    p->loaded = loaded;
    p->seek_called = false;
    p->seek_position_ms = -1;
    p->pause_called = false;
    p->seek_order = -1;
    p->pause_order = -1;
}

static bool
mock_is_loaded(void *user_data)
{
    MockPlayer *p = (MockPlayer *)user_data;
    return p->loaded;
}

static void
mock_seek(void *user_data, int64_t position_ms)
{
    MockPlayer *p = (MockPlayer *)user_data;
    p->seek_called = true;
    p->seek_position_ms = position_ms;
    p->seek_order = g_call_counter++;
}

static void
mock_pause(void *user_data)
{
    MockPlayer *p = (MockPlayer *)user_data;
    p->pause_called = true;
    p->pause_order = g_call_counter++;
}

static void
test_sync_all_to_master(void)
{
    SyncManager sm;
    sync_manager_init(&sm, 4);
    sync_manager_set_master(&sm, 0);
    /* master (0) offset 0, target 1 offset +1000, target 2 offset -500,
     * video 3 non caricato (slot vuoto) */
    sync_manager_set_offset(&sm, 1, 1000);
    sync_manager_set_offset(&sm, 2, -500);

    MockPlayer players[4];
    mock_player_reset(&players[0], true);  /* master */
    mock_player_reset(&players[1], true);
    mock_player_reset(&players[2], true);
    mock_player_reset(&players[3], false); /* non caricato */

    SyncPlayerOps ops[4] = {
        {.user_data = &players[0], .is_loaded = mock_is_loaded, .seek = mock_seek, .pause = mock_pause},
        {.user_data = &players[1], .is_loaded = mock_is_loaded, .seek = mock_seek, .pause = mock_pause},
        {.user_data = &players[2], .is_loaded = mock_is_loaded, .seek = mock_seek, .pause = mock_pause},
        {.user_data = &players[3], .is_loaded = mock_is_loaded, .seek = mock_seek, .pause = mock_pause},
    };

    g_call_counter = 0;
    sync_manager_sync_all_to_master(&sm, 10000, ops, 4);

    /* master: solo pausa, nessun seek */
    assert(players[0].seek_called == false);
    assert(players[0].pause_called == true);

    /* video 1: sync_pos = 10000 - offset[0](0) + offset[1](1000) = 11000 */
    assert(players[1].seek_called == true);
    assert(players[1].seek_position_ms == 11000);
    assert(players[1].pause_called == true);
    assert(players[1].seek_order < players[1].pause_order); /* seek prima di pause */

    /* video 2: sync_pos = 10000 - 0 + (-500) = 9500 */
    assert(players[2].seek_called == true);
    assert(players[2].seek_position_ms == 9500);
    assert(players[2].pause_called == true);
    assert(players[2].seek_order < players[2].pause_order);

    /* video 3: non caricato, nessuna chiamata */
    assert(players[3].seek_called == false);
    assert(players[3].pause_called == false);
}

static void
test_sync_all_to_master_null_slot(void)
{
    /* is_loaded == NULL equivale a "player is None": slot ignorato del
     * tutto, nessuna chiamata anche se seek/pause fossero valorizzati. */
    SyncManager sm;
    sync_manager_init(&sm, 1);

    MockPlayer p;
    mock_player_reset(&p, true);

    SyncPlayerOps ops[1] = {
        {.user_data = &p, .is_loaded = NULL, .seek = mock_seek, .pause = mock_pause},
    };

    sync_manager_sync_all_to_master(&sm, 5000, ops, 1);

    assert(p.seek_called == false);
    assert(p.pause_called == false);
}

int
main(void)
{
    SyncManager sm;
    sync_manager_init(&sm, 4);

    /* Stato iniziale */
    assert(sync_manager_is_enabled(&sm) == true);
    assert(sync_manager_get_master(&sm) == 0);
    for (int i = 0; i < SYNCVIEW_MAX_VIDEOS; i++) {
        assert(sync_manager_get_offset(&sm, i) == 0);
    }

    /* enabled getter/setter */
    sync_manager_set_enabled(&sm, false);
    assert(sync_manager_is_enabled(&sm) == false);
    sync_manager_set_enabled(&sm, true);
    assert(sync_manager_is_enabled(&sm) == true);

    /* offset getter/setter */
    sync_manager_set_offset(&sm, 0, 1500);
    sync_manager_set_offset(&sm, 2, -250);
    assert(sync_manager_get_offset(&sm, 0) == 1500);
    assert(sync_manager_get_offset(&sm, 2) == -250);
    assert(sync_manager_get_offset(&sm, 1) == 0);

    /* offset fuori range: no-op / ritorna 0 */
    sync_manager_set_offset(&sm, 99, 12345);
    assert(sync_manager_get_offset(&sm, 99) == 0);
    assert(sync_manager_get_offset(&sm, -1) == 0);

    /* reset */
    sync_manager_reset_offsets(&sm);
    for (int i = 0; i < SYNCVIEW_MAX_VIDEOS; i++) {
        assert(sync_manager_get_offset(&sm, i) == 0);
    }

    /* master getter/setter */
    sync_manager_set_master(&sm, 2);
    assert(sync_manager_get_master(&sm) == 2);

    /* master fuori range: no-op */
    sync_manager_set_master(&sm, -1);
    assert(sync_manager_get_master(&sm) == 2);
    sync_manager_set_master(&sm, SYNCVIEW_MAX_VIDEOS);
    assert(sync_manager_get_master(&sm) == 2);

    /* --- calculate_sync_position --- */
    sync_manager_reset_offsets(&sm);

    /* offset uguali (entrambi 0): nessuna traslazione */
    assert(sync_manager_calculate_sync_position(&sm, 10000, 0, 1) == 10000);

    /* offset diversi: source=0 offset=500, target=1 offset=2000
     * sync_position = 10000 - 500 + 2000 = 11500 */
    sync_manager_set_offset(&sm, 0, 500);
    sync_manager_set_offset(&sm, 1, 2000);
    assert(sync_manager_calculate_sync_position(&sm, 10000, 0, 1) == 11500);

    /* stesso offset su source e target: nessuna traslazione anche se
     * entrambi non-zero */
    sync_manager_set_offset(&sm, 0, 700);
    sync_manager_set_offset(&sm, 1, 700);
    assert(sync_manager_calculate_sync_position(&sm, 5000, 0, 1) == 5000);

    /* clamp a 0: source_position - source_offset + target_offset < 0 */
    sync_manager_set_offset(&sm, 0, 5000);
    sync_manager_set_offset(&sm, 1, 0);
    assert(sync_manager_calculate_sync_position(&sm, 1000, 0, 1) == 0);

    /* source == target: risultato = source_position invariato */
    sync_manager_reset_offsets(&sm);
    sync_manager_set_offset(&sm, 2, 999);
    assert(sync_manager_calculate_sync_position(&sm, 4321, 2, 2) == 4321);

    test_sync_all_to_master();
    test_sync_all_to_master_null_slot();

    return 0;
}
