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

#endif /* SYNCVIEW_CORE_SYNC_MANAGER_H */
