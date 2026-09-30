#ifndef SYNCVIEW_CORE_MARKER_DB_H
#define SYNCVIEW_CORE_MARKER_DB_H

#include "core/markers.h"

#include <glib.h>

/*
 * Porting di core/marker_db.py (MarkerDatabase): persistenza dei marker su
 * SQLite. Come nell'originale non c'è una connessione persistente: ogni
 * operazione apre e chiude la propria connessione (thread-safety). Unica
 * deviazione: sqlite3_busy_timeout() su ogni connessione, per ridurre gli
 * errori spuri "database is locked" sotto contesa.
 */

#define SYNCVIEW_MARKER_DB_VERSION 1

#define MARKER_DB_ERROR (marker_db_error_quark())
GQuark marker_db_error_quark(void);

typedef enum {
    MARKER_DB_ERROR_OPEN,  /* impossibile aprire il file/creare la directory */
    MARKER_DB_ERROR_SQL,   /* errore SQLite generico */
    MARKER_DB_ERROR_JSON,  /* JSON legacy non valido / backup non creabile */
} MarkerDbError;

typedef struct MarkerDb MarkerDb;

/*
 * Apre (creando file e directory se mancano) il database in path e si
 * assicura che lo schema sia alla versione corrente: su DB nuovo crea le
 * tabelle/indici e scrive db_version + created_at in `metadata`; su DB
 * esistente con versione inferiore esegue la migrazione e aggiorna
 * db_version. Ritorna NULL e imposta error in caso di fallimento.
 */
MarkerDb *marker_db_open(const char *path, GError **error);

void marker_db_free(MarkerDb *db);

const char *marker_db_get_path(const MarkerDb *db);

/*
 * Salva tutti i marker dello store in un'unica transazione (upsert per id:
 * `ON CONFLICT(id) DO UPDATE`, come save_markers_batch). Aggiorna
 * timestamp/color/description/category/video_index/updated_at; created_at
 * e is_deleted di una riga esistente non vengono toccati. Un marker con
 * video_index SYNCVIEW_MARKER_VIDEO_INDEX_ALL viene salvato come NULL.
 * Se un qualunque inserimento fallisce, l'intero batch viene annullato
 * (rollback). Uno store vuoto è un successo.
 */
gboolean marker_db_save_batch(MarkerDb *db, const MarkerStore *store, GError **error);

/*
 * Soft delete: imposta is_deleted=1 e aggiorna updated_at per il marker
 * con l'id dato (come delete_marker). La riga resta nel DB ed è esclusa da
 * marker_db_load_all. Come nell'originale, un id inesistente non è un
 * errore (nessuna riga modificata, ritorna TRUE); FALSE solo per errori SQLite.
 */
gboolean marker_db_delete(MarkerDb *db, const char *id, GError **error);

/*
 * Migra il JSON legacy (`{"markers": [...]}`, formato di MarkerManager.save)
 * in SQLite, come _migrate_from_json:
 *  - file assente: successo, *migrated_count = 0;
 *  - `label` scartato; qualunque altra chiave sconosciuta, tipo errato, o
 *    timestamp/color mancanti fanno fallire l'intera migrazione (nulla
 *    viene salvato, il JSON resta intatto); id/created_at mancanti vengono
 *    generati; video_index null/assente = marker globale;
 *  - lista vuota: niente salvataggio e niente backup;
 *  - altrimenti salva in batch e solo dopo rinomina il JSON in
 *    `<nome>.json.backup` (sostituisce l'estensione, come with_suffix),
 *    sovrascrivendo un eventuale backup precedente.
 * migrated_count (opzionale) riceve il numero di marker migrati.
 */
gboolean marker_db_migrate_from_json(MarkerDb *db, const char *json_path, int *migrated_count,
                                     GError **error);

/*
 * Apre il DB e, se il file DB non esisteva ancora e legacy_json_path non è
 * NULL, tenta la migrazione del JSON (come il costruttore di MarkerManager).
 * Un errore di migrazione non impedisce l'apertura: viene riportato in
 * migration_error (opzionale) e il JSON resta al suo posto. `error` copre
 * solo il fallimento dell'apertura (ritorna NULL).
 */
MarkerDb *marker_db_open_migrating(const char *db_path, const char *legacy_json_path,
                                   GError **migration_error, GError **error);

/*
 * Carica i marker in un nuovo MarkerStore (ordinato per timestamp; owned
 * dal chiamante, da liberare con marker_store_free). I marker con
 * is_deleted=1 sono esclusi, a meno di include_deleted. Ritorna NULL e
 * imposta error in caso di fallimento.
 */
MarkerStore *marker_db_load_all(MarkerDb *db, gboolean include_deleted, GError **error);

#endif /* SYNCVIEW_CORE_MARKER_DB_H */
