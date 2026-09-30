#ifndef SYNCVIEW_CORE_LOGGER_H
#define SYNCVIEW_CORE_LOGGER_H

#include <glib.h>
#include <stdint.h>

/*
 * Porting di core/logger.py (SyncViewLogger) + modalità debug (vedi
 * PLAN.md, "Modalità debug").
 *
 * Il logger è uno stato globale del modulo (come l'istanza `logger` di
 * Python): logger_init() lo apre una volta in main(); tutti gli altri
 * moduli chiamano direttamente le funzioni log_*() SENZA mai controllare
 * la modalità debug — è questo file a decidere cosa scrivere e dove.
 * Prima di logger_init() (o dopo logger_shutdown()) ogni chiamata è un
 * no-op silenzioso: nessun byte su file né su stderr.
 *
 * Modalità normale:  INFO/WARNING/ERROR solo su file.
 * Modalità debug:    tutto, compresi i dettagli di livello DEBUG
 *                    (log_sync/log_marker/log_gst/log_ui), sul file E su
 *                    stderr in tempo reale.
 *
 * Riga di log: "YYYY-MM-DD HH:MM:SS.mmm - SyncView - LIVELLO - messaggio".
 * Thread-safe (GStreamer logga da thread propri).
 */

/* --- Livelli, moduli, filtro e sink (finestre di debug) --- */

typedef enum {
    LOGGER_LEVEL_DEBUG,
    LOGGER_LEVEL_INFO,
    LOGGER_LEVEL_WARNING,
    LOGGER_LEVEL_ERROR,
} LoggerLevel;

/*
 * Modulo di provenienza di un messaggio. Le categorie storiche si
 * mappano così: log_user_action -> USER; log_video_action/log_playback/
 * log_timeline_seek -> VIDEO; log_export/log_export_action -> EXPORT;
 * log_sync -> SYNC; log_marker -> MARKER; log_gst -> GST; log_ui -> UI;
 * log_error e i messaggi del logger stesso (avvio) -> APP.
 */
typedef enum {
    LOGGER_MODULE_APP,
    LOGGER_MODULE_USER,
    LOGGER_MODULE_VIDEO,
    LOGGER_MODULE_EXPORT,
    LOGGER_MODULE_SYNC,
    LOGGER_MODULE_MARKER,
    LOGGER_MODULE_GST,
    LOGGER_MODULE_UI,
    LOGGER_MODULE_COUNT
} LoggerModule;

/* Nome breve ("SYNC", "MARKER", ...) per le UI; "?" se module non è valido. */
const char *logger_module_name(LoggerModule module);

/*
 * Abilita/disabilita i messaggi di un modulo su TUTTE le destinazioni (file,
 * stderr, sink). Default: tutti abilitati; logger_init() li riabilita.
 * I messaggi di livello ERROR non vengono mai filtrati. Thread-safe.
 * Questa è l'unica API che legge/scrive il filtro: i moduli che loggano
 * non la usano (la usa la finestra "Moduli" di debug).
 */
void logger_set_module_enabled(LoggerModule module, gboolean enabled);
gboolean logger_is_module_enabled(LoggerModule module);

/*
 * Sink: callback che riceve ogni messaggio effettivamente emesso (dopo il
 * filtro), oltre a file e stderr. Invocati SOLO in modalità debug.
 *  - Il sink gira nel thread che ha chiamato log_*() (spesso un thread
 *    GStreamer o worker): non deve toccare widget GTK direttamente, ma
 *    accodare e rimandare al main thread (g_idle_add).
 *  - `timestamp` e `message` valgono solo durante la chiamata (copiarli).
 *  - I log emessi da dentro un sink non vengono recapitati ai sink (nessuna
 *    ricorsione) ma vanno comunque su file/stderr.
 *  - Il sink non è invocato con il lock del logger: può chiamare
 *    logger_add_sink/logger_remove_sink. Dopo logger_remove_sink() una
 *    chiamata già in corso in un altro thread può ancora completarsi.
 */
typedef void (*LoggerSinkFunc)(LoggerLevel level, LoggerModule module, const char *timestamp,
                               const char *message, gpointer user_data);

/* Registra un sink; ritorna un id (> 0) da passare a logger_remove_sink(). */
guint logger_add_sink(LoggerSinkFunc func, gpointer user_data);
void logger_remove_sink(guint sink_id);

/* ~/.syncview/syncview_log.txt. Il chiamante libera con g_free(). */
char *logger_default_file(void);

/*
 * Apre il logger. Il file viene troncato ad ogni apertura (come
 * FileHandler mode='w') e riceve l'intestazione di avvio. debug è TRUE se
 * `cli_debug` è TRUE (flag --debug/-v) oppure la variabile d'ambiente
 * SYNCVIEW_DEBUG è impostata a un valore diverso da "", "0", "false". In
 * debug, se GST_DEBUG non è già impostata viene impostata a "3", quindi
 * logger_init() va chiamato prima di gst_init().
 *
 * file_path NULL = nessun file (solo stderr in debug). Se il file non si
 * può aprire ritorna FALSE con error, ma il logger resta attivo (stderr
 * in debug) — l'app non deve fallire per un log non scrivibile. Una
 * seconda chiamata senza logger_shutdown() chiude prima la precedente.
 */
gboolean logger_init(const char *file_path, gboolean cli_debug, GError **error);

/* Chiude il file e torna allo stato "non inizializzato" (no-op). */
void logger_shutdown(void);

/* --- Categorie dell'originale (livello INFO/ERROR, sempre su file) --- */

void log_user_action(const char *action, const char *details);
/* video_index 0-based; nel log compare come "VIDEO n+1", come l'originale. */
void log_video_action(int video_index, const char *action, const char *details);
void log_playback(int video_index, const char *state);
void log_timeline_seek(int video_index, int64_t position_ms);
/* error opzionale: il suo messaggio è registrato su una seconda riga ERROR (equivalente a logger.exception). */
void log_error(const char *message, const GError *error);
void log_export(const char *video_name, gboolean success, const char *error_message);
void log_export_action(const char *action, const char *details);

/* --- Categorie nuove per la modalità debug (livello DEBUG: scritte solo in debug) --- */

void log_sync(const char *fmt, ...) G_GNUC_PRINTF(1, 2);
void log_marker(const char *fmt, ...) G_GNUC_PRINTF(1, 2);
void log_gst(const char *fmt, ...) G_GNUC_PRINTF(1, 2);
void log_ui(const char *fmt, ...) G_GNUC_PRINTF(1, 2);

#endif /* SYNCVIEW_CORE_LOGGER_H */
