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
