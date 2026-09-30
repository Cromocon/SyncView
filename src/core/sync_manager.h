#ifndef SYNCVIEW_CORE_SYNC_MANAGER_H
#define SYNCVIEW_CORE_SYNC_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

#include "core/settings.h"

/*
 * Porting 1:1 di core/sync_manager.py (SyncManager). Modulo di logica
 * pura, senza I/O: a differenza dell'originale (che chiamava
 * logger.log_user_action direttamente nei setter), qui il logging
 * dell'azione utente è responsabilità del chiamante (UI/controller,
 * M3+) — questo modulo resta testabile in isolamento senza dipendere
 * da core/logger.c.
 *
 * Nota di fedeltà: come nell'originale, i controlli di range su
 * video_index usano sempre [0, SYNCVIEW_MAX_VIDEOS) (hardcoded a 4 nel
 * Python), non [0, num_players) — è il comportamento verificato nel
 * sorgente originale, non un refactor.
 */
typedef struct {
    bool sync_enabled;
    int num_players;
    int64_t video_offsets_ms[SYNCVIEW_MAX_VIDEOS];
    int master_video_index;
} SyncManager;

/*
 * Inizializza sm con num_players player. Stato iniziale (come
 * l'originale __init__): sync_enabled = true, tutti gli offset a 0,
 * master_video_index = 0.
 */
void sync_manager_init(SyncManager *sm, int num_players);

void sync_manager_set_enabled(SyncManager *sm, bool enabled);
bool sync_manager_is_enabled(const SyncManager *sm);

/* No-op se video_index fuori da [0, SYNCVIEW_MAX_VIDEOS). */
void sync_manager_set_offset(SyncManager *sm, int video_index, int64_t offset_ms);

/* Ritorna 0 se video_index fuori da [0, SYNCVIEW_MAX_VIDEOS) (come
 * l'originale get_video_offset, che ritorna il default del dict). */
int64_t sync_manager_get_offset(const SyncManager *sm, int video_index);

void sync_manager_reset_offsets(SyncManager *sm);

/* No-op se video_index fuori da [0, SYNCVIEW_MAX_VIDEOS). */
void sync_manager_set_master(SyncManager *sm, int video_index);
int sync_manager_get_master(const SyncManager *sm);

/*
 * Porting 1:1 di SyncManager.calculate_sync_position: traduzione
 * lineare tra gli offset di due video, nessuna correzione di drift.
 *
 *   sync_position = source_position - offset[source_index] + offset[target_index]
 *
 * Clampata a >= 0. Nessun evento periodico/continuo: va richiamata
 * solo su eventi discreti (seek, resync, click marker), come
 * nell'originale.
 */
int64_t sync_manager_calculate_sync_position(const SyncManager *sm,
                                              int64_t source_position_ms,
                                              int source_index,
                                              int target_index);

/*
 * Callback opachi verso un video player reale, per tenere
 * sync_manager_sync_all_to_master testabile senza dipendere dal vero
 * SyncviewVideoPlayer (M2+). is_loaded == NULL indica uno slot vuoto
 * (nessun player caricato), equivalente a `player is None`
 * nell'originale Python — in quel caso lo slot viene ignorato del
 * tutto, senza chiamare seek/pause.
 */
typedef struct {
    void *user_data;
    bool (*is_loaded)(void *user_data);
    void (*seek)(void *user_data, int64_t position_ms);
    void (*pause)(void *user_data);
} SyncPlayerOps;

/*
 * Porting 1:1 di SyncManager.sync_all_to_master: per ogni player
 * caricato diverso dal master, calcola la posizione sincronizzata
 * rispetto al master e chiama seek() poi pause(); il player master
 * viene solo messo in pausa (nessun seek). Funziona indipendentemente
 * da sm->sync_enabled, esattamente come l'originale (chiamata anche
 * dal resync manuale a sync disattivata).
 */
void sync_manager_sync_all_to_master(SyncManager *sm,
                                      int64_t master_position_ms,
                                      SyncPlayerOps players[],
                                      int n_players);

#endif /* SYNCVIEW_CORE_SYNC_MANAGER_H */
