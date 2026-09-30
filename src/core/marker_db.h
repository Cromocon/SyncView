#ifndef SYNCVIEW_CORE_MARKER_DB_H
#define SYNCVIEW_CORE_MARKER_DB_H

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

#endif /* SYNCVIEW_CORE_MARKER_DB_H */
