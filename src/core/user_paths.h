#ifndef SYNCVIEW_CORE_USER_PATHS_H
#define SYNCVIEW_CORE_USER_PATHS_H

#include "core/settings.h"

#include <glib.h>

/*
 * Porting di config/user_paths.py (UserPathManager): ultime path usate
 * dall'utente (un percorso video per ognuno dei SYNCVIEW_MAX_VIDEOS slot +
 * ultima directory di export), persistite in JSON. A differenza
 * dell'originale non c'è un'istanza globale né un path hardcoded: il file
 * è iniettato alla creazione (testabilità); il path di default
 * (~/.syncview/user_paths.json) è fornito da user_paths_default_file().
 *
 * Formato file (invariato, compatibile con la versione Python):
 *   {"video_paths": [<str|null> x4], "last_export_dir": <str|null>}
 */

#define USER_PATHS_ERROR (user_paths_error_quark())
GQuark user_paths_error_quark(void);

typedef enum {
    USER_PATHS_ERROR_INDEX,  /* slot video fuori da 0..SYNCVIEW_MAX_VIDEOS-1 */
    USER_PATHS_ERROR_IO,     /* salvataggio su file fallito */
} UserPathsError;

typedef struct UserPaths UserPaths;

/* ~/.syncview/user_paths.json. Il chiamante libera con g_free(). */
char *user_paths_default_file(void);

/*
 * Crea il gestore sul file indicato: crea la directory padre se manca e
 * carica i valori salvati. File assente, illeggibile o non valido → valori
 * vuoti (come l'originale, nessun errore). Non ritorna mai NULL.
 */
UserPaths *user_paths_new(const char *file_path);
void user_paths_free(UserPaths *paths);

/*
 * Imposta il percorso dello slot e salva subito il file. FALSE con error
 * se l'indice non è valido (nulla cambia) o se il salvataggio fallisce (il
 * valore in memoria resta aggiornato, come nell'originale).
 */
gboolean user_paths_set_video_path(UserPaths *paths, int index, const char *path, GError **error);

/* Percorso dello slot, o NULL se vuoto o indice non valido. Valido fino alla prossima modifica. */
const char *user_paths_get_video_path(const UserPaths *paths, int index);

/* Svuota lo slot e salva. Stessa semantica di errore di set_video_path. */
gboolean user_paths_clear_video_path(UserPaths *paths, int index, GError **error);

/* Imposta l'ultima directory di export (NULL = nessuna) e salva. FALSE con error se il salvataggio fallisce. */
gboolean user_paths_set_export_dir(UserPaths *paths, const char *path, GError **error);

const char *user_paths_get_export_dir(const UserPaths *paths);

/*
 * Come get_valid_video_paths: riempie out[0..SYNCVIEW_MAX_VIDEOS-1] con i
 * percorsi che esistono ancora come file regolari (NULL per gli altri),
 * rimuove dagli slot quelli non più validi e, se ne ha rimossi, salva il
 * file. Ritorna il numero di percorsi validi. Un eventuale errore di
 * salvataggio è riportato in save_error (opzionale); out resta comunque
 * valido. Le stringhe di out valgono fino alla prossima modifica.
 */
int user_paths_get_valid_video_paths(UserPaths *paths, const char *out[SYNCVIEW_MAX_VIDEOS],
                                     GError **save_error);

#endif /* SYNCVIEW_CORE_USER_PATHS_H */
