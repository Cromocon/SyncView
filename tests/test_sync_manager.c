#include "core/sync_manager.h"

#include <assert.h>

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

    return 0;
}
