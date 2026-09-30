#ifndef SYNCVIEW_CORE_MARKERS_H
#define SYNCVIEW_CORE_MARKERS_H

#include <glib.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Porting 1:1 della dataclass Marker di core/markers.py (letta per
 * intero da main). Campi: timestamp (ms), color (hex), description
 * (default ""), category (default "default"), video_index
 * (Optional[int] in Python, None = marker globale su tutti i video —
 * qui rappresentato da SYNCVIEW_MARKER_VIDEO_INDEX_ALL), created_at
 * (ISO8601, auto-generato), id (auto-generato se non fornito). Nota:
 * l'originale non ha un campo updated_at nella dataclass — viene
 * aggiunto solo a livello di schema SQLite al momento del salvataggio
 * (vedi core/marker_db.c, M1.9+), non qui.
 */
#define SYNCVIEW_MARKER_VIDEO_INDEX_ALL (-1)

typedef struct {
    char *id;          /* owned, mai NULL dopo marker_new() */
    int64_t timestamp_ms;
    char *color;        /* owned, mai NULL */
    char *description;  /* owned, mai NULL ("" se non specificata) */
    char *category;     /* owned, mai NULL ("default" se non specificata) */
    int video_index;    /* SYNCVIEW_MARKER_VIDEO_INDEX_ALL = marker globale */
    char *created_at;   /* owned, ISO8601 locale naive, mai NULL */
} Marker;

/*
 * Crea un nuovo marker. id e created_at vengono generati
 * automaticamente (equivalente a Marker.__post_init__). description e
 * category possono essere NULL (default "" e "default" rispettivamente,
 * come i default della dataclass originale). color non può essere NULL
 * (l'originale non ha un default per questo campo).
 *
 * Ownership: il chiamante possiede il Marker* ritornato e deve
 * liberarlo con marker_free(). Ritorna NULL solo in caso di
 * esaurimento memoria.
 */
Marker *marker_new(int64_t timestamp_ms, const char *color, const char *description,
                    const char *category, int video_index);

void marker_free(Marker *marker);

/*
 * MarkerStore: collezione di Marker ordinata per timestamp_ms (porting di
 * MarkerManager.markers di core/markers.py, senza persistenza: quella vive
 * in core/marker_db.c). L'ordine è mantenuto con inserimento ordinato
 * (binary search) invece di append + sort come nell'originale; a parità di
 * timestamp vale l'ordine di inserimento (come il sort stabile di Python).
 * Lo store possiede i Marker contenuti.
 */
typedef struct MarkerStore MarkerStore;

/* Campi modificabili da marker_store_update (equivalente ai **kwargs). */
typedef enum {
    MARKER_FIELD_TIMESTAMP   = 1 << 0,
    MARKER_FIELD_COLOR       = 1 << 1,
    MARKER_FIELD_DESCRIPTION = 1 << 2,
    MARKER_FIELD_CATEGORY    = 1 << 3,
    MARKER_FIELD_VIDEO_INDEX = 1 << 4,
} MarkerField;

typedef struct {
    unsigned fields;  /* bitmask di MarkerField: solo i campi indicati vengono applicati */
    int64_t timestamp_ms;
    const char *color;        /* non NULL se MARKER_FIELD_COLOR */
    const char *description;  /* NULL trattato come "" */
    const char *category;     /* NULL trattato come "default" */
    int video_index;
} MarkerUpdate;

MarkerStore *marker_store_new(void);
void marker_store_free(MarkerStore *store);

size_t marker_store_count(const MarkerStore *store);

/* Marker in posizione index (ordine per timestamp); NULL se fuori range. Non owned. */
const Marker *marker_store_get(const MarkerStore *store, size_t index);

/* Marker con l'id dato; NULL se non trovato. Non owned. */
const Marker *marker_store_find_by_id(const MarkerStore *store, const char *id);

/*
 * Crea un marker e lo inserisce in ordine (equivalente a add_marker).
 * Ritorna il marker (owned dallo store, valido fino a remove/free) o NULL
 * per esaurimento memoria.
 */
const Marker *marker_store_add(MarkerStore *store, int64_t timestamp_ms, const char *color,
                               const char *description, const char *category, int video_index);

/*
 * Inserisce in ordine un marker già costruito (es. caricato dal DB),
 * trasferendo l'ownership allo store.
 */
void marker_store_add_marker(MarkerStore *store, Marker *marker);

/* Rimuove e libera il marker con l'id dato. TRUE se rimosso, FALSE se non trovato. */
gboolean marker_store_remove(MarkerStore *store, const char *id);

/*
 * Applica i campi indicati in update al marker con l'id dato; se il
 * timestamp cambia, il marker viene riposizionato. Ritorna il marker
 * aggiornato o NULL se l'id non esiste.
 */
const Marker *marker_store_update(MarkerStore *store, const char *id, const MarkerUpdate *update);

/*
 * Query su timestamp, O(log n) con binary search sull'array ordinato.
 * Semantica identica alle scansioni lineari di MarkerManager (Python).
 * I Marker ritornati non sono owned e restano validi fino a
 * add/remove/update successivi.
 */

/*
 * Marker più vicino a timestamp_ms entro tolerance_ms (distanza <=
 * tolerance), o NULL. A parità di distanza vince quello successivo
 * nell'ordine dello store (come get_marker_at, che usa `<=`).
 */
const Marker *marker_store_get_at(const MarkerStore *store, int64_t timestamp_ms,
                                  int64_t tolerance_ms);

/* Primo marker con timestamp > timestamp_ms (get_next_marker), o NULL. */
const Marker *marker_store_get_next(const MarkerStore *store, int64_t timestamp_ms);

/* Ultimo marker con timestamp < timestamp_ms (get_previous_marker), o NULL. */
const Marker *marker_store_get_previous(const MarkerStore *store, int64_t timestamp_ms);

/*
 * Marker con start_ms <= timestamp <= end_ms (get_markers_in_range). Sono
 * contigui nello store: ritorna il numero di marker e scrive in
 * first_index (se non NULL) l'indice del primo, da usare con
 * marker_store_get(). Ritorna 0 se il range è vuoto o start_ms > end_ms.
 */
size_t marker_store_get_range(const MarkerStore *store, int64_t start_ms, int64_t end_ms,
                              size_t *first_index);

#endif /* SYNCVIEW_CORE_MARKERS_H */
