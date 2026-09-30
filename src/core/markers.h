#ifndef SYNCVIEW_CORE_MARKERS_H
#define SYNCVIEW_CORE_MARKERS_H

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

#endif /* SYNCVIEW_CORE_MARKERS_H */
