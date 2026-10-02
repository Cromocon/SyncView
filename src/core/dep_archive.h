#ifndef SYNCVIEW_CORE_DEP_ARCHIVE_H
#define SYNCVIEW_CORE_DEP_ARCHIVE_H

#include <glib.h>

/*
 * Estrazione sicura di archivi ZIP (M2.11), senza dipendenze oltre a GLib/GIO (il deflate è il decompressore raw di
 * GIO). Pensata per archivi scaricati e già verificati con SHA-256, ma NON si fida del contenuto: ogni voce è
 * controllata prima di scrivere qualcosa.
 *
 * Rifiutati (l'estrazione si interrompe con errore e nulla di parziale resta in `dest_dir`, che il chiamante rimuove):
 *  - percorsi assoluti, con lettera di unità, con componenti «..», con backslash, vuoti o con byte nulli (zip-slip);
 *  - voci duplicate, collegamenti simbolici e altri file speciali;
 *  - voci cifrate, metodi di compressione diversi da «stored» e «deflate», ZIP64, archivi su più dischi;
 *  - dimensioni dichiarate oltre i tetti (per voce e totale) o dati che superano la dimensione dichiarata (zip bomb);
 *  - CRC-32 che non corrisponde, dati fuori dal file, struttura incoerente.
 */
#define DEP_ARCHIVE_ERROR (dep_archive_error_quark())
GQuark dep_archive_error_quark(void);

typedef enum {
    DEP_ARCHIVE_ERROR_FORMAT,     /* non è uno ZIP valido o usa funzioni non supportate */
    DEP_ARCHIVE_ERROR_UNSAFE,     /* percorso o tipo di voce pericoloso */
    DEP_ARCHIVE_ERROR_TOO_LARGE,  /* oltre i tetti di dimensione */
    DEP_ARCHIVE_ERROR_CORRUPT,    /* CRC o dimensioni non coerenti */
    DEP_ARCHIVE_ERROR_IO,         /* errore di scrittura */
} DepArchiveError;

#define DEP_ARCHIVE_MAX_ENTRY_BYTES ((gint64)1 << 30)        /* 1 GiB per voce */
#define DEP_ARCHIVE_MAX_TOTAL_BYTES ((gint64)2 << 30)        /* 2 GiB in tutto */
#define DEP_ARCHIVE_MAX_ENTRIES 20000

/*
 * Estrae `zip_path` in `dest_dir` (già esistente, di solito una directory di appoggio nuova). `strip_components`
 * scarta i primi N componenti di ogni percorso (come tar): le voci che restano senza percorso si ignorano.
 * I file con il bit di esecuzione nel ZIP (attributi unix) ricevono 0755, gli altri 0644.
 * `max_total_bytes` <= 0 usa DEP_ARCHIVE_MAX_TOTAL_BYTES. Ritorna FALSE con error.
 */
gboolean dep_archive_extract_zip(const char *zip_path, const char *dest_dir, int strip_components, gint64 max_total_bytes,
                                 GError **error);

/* CRC-32 (come nei file ZIP) — esposto per i test che costruiscono archivi. */
guint32 dep_crc32(guint32 crc, const guchar *data, gsize length);

#endif /* SYNCVIEW_CORE_DEP_ARCHIVE_H */
