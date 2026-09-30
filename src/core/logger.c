#include "core/logger.h"

#include <errno.h>
#include <glib/gstdio.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef enum {
    LEVEL_DEBUG,
    LEVEL_INFO,
    LEVEL_WARNING,
    LEVEL_ERROR,
} Level;

static const char *const LEVEL_NAMES[] = { "DEBUG", "INFO", "WARNING", "ERROR" };

/* Stato globale del modulo. L'UNICO punto in cui la modalità debug è valutata. */
static GMutex state_lock;
static gboolean initialized = FALSE;
static gboolean debug_mode = FALSE;
static FILE *log_file = NULL;

char *
logger_default_file(void)
{
    return g_build_filename(g_get_home_dir(), ".syncview", "syncview_log.txt", NULL);
}

static gboolean
env_debug_requested(void)
{
    const char *value = g_getenv("SYNCVIEW_DEBUG");

    return value && *value && g_ascii_strcasecmp(value, "0") != 0
           && g_ascii_strcasecmp(value, "false") != 0;
}

static char *
timestamp_now(void)
{
    GDateTime *now = g_date_time_new_now_local();
    char *s = g_strdup_printf("%04d-%02d-%02d %02d:%02d:%02d.%03d", g_date_time_get_year(now),
                              g_date_time_get_month(now), g_date_time_get_day_of_month(now),
                              g_date_time_get_hour(now), g_date_time_get_minute(now),
                              g_date_time_get_second(now), g_date_time_get_microsecond(now) / 1000);
    g_date_time_unref(now);
    return s;
}

/* Scrive una riga già formattata (senza newline) secondo modalità e livello. */
static void
emit(Level level, const char *message)
{
    g_mutex_lock(&state_lock);

    /* I dettagli di livello DEBUG esistono solo in modalità debug. */
    if (initialized && (level != LEVEL_DEBUG || debug_mode)) {
        char *ts = timestamp_now();
        char *line = g_strdup_printf("%s - SyncView - %s - %s\n", ts, LEVEL_NAMES[level], message);

        if (log_file) {
            fputs(line, log_file);
            fflush(log_file);
        }
        if (debug_mode) {
            fputs(line, stderr);
            fflush(stderr);
        }

        g_free(line);
        g_free(ts);
    }

    g_mutex_unlock(&state_lock);
}

static void
emit_with_details(Level level, const char *prefix, const char *text, const char *details)
{
    char *msg = (details && *details) ? g_strdup_printf("%s%s - %s", prefix, text, details)
                                      : g_strdup_printf("%s%s", prefix, text);
    emit(level, msg);
    g_free(msg);
}

static void
close_locked(void)
{
    if (log_file) {
        fclose(log_file);
        log_file = NULL;
    }
    initialized = FALSE;
    debug_mode = FALSE;
}

gboolean
logger_init(const char *file_path, gboolean cli_debug, GError **error)
{
    gboolean ok = TRUE;

    g_mutex_lock(&state_lock);
    close_locked();

    debug_mode = cli_debug || env_debug_requested();
    initialized = TRUE;

    if (debug_mode) {
        /* Log interni di GStreamer senza che l'utente debba conoscerne la sintassi; non sovrascrive una scelta esplicita. */
        g_setenv("GST_DEBUG", "3", FALSE);
    }

    if (file_path) {
        char *dir = g_path_get_dirname(file_path);
        g_mkdir_with_parents(dir, 0755);
        g_free(dir);

        /* "wb": troncato ad ogni avvio; binario per avere sempre "\n" (su Windows "w" scriverebbe "\r\n"). */
        log_file = fopen(file_path, "wb");
        if (!log_file) {
            g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                        "Impossibile aprire il file di log %s: %s", file_path, g_strerror(errno));
            ok = FALSE;
        }
    }
    g_mutex_unlock(&state_lock);

    if (log_file) {
        /* Intestazione di avvio, come _write_startup_header (scritta solo su file). */
        char *ts = timestamp_now();
        g_mutex_lock(&state_lock);
        fprintf(log_file,
                "\n======================================================================\n"
                "  SYNCVIEW - AVVIO APPLICAZIONE\n"
                "  Timestamp: %s\n"
                "======================================================================\n",
                ts);
        g_mutex_unlock(&state_lock);
        g_free(ts);
    }

    emit(LEVEL_INFO, "Applicazione SyncView avviata");
    return ok;
}

void
logger_shutdown(void)
{
    g_mutex_lock(&state_lock);
    close_locked();
    g_mutex_unlock(&state_lock);
}

void
log_user_action(const char *action, const char *details)
{
    emit_with_details(LEVEL_INFO, "[AZIONE UTENTE] ", action, details);
}

void
log_video_action(int video_index, const char *action, const char *details)
{
    char *prefix = g_strdup_printf("[VIDEO %d] ", video_index + 1);
    emit_with_details(LEVEL_INFO, prefix, action, details);
    g_free(prefix);
}

void
log_playback(int video_index, const char *state)
{
    char *msg = g_strdup_printf("[VIDEO %d] Stato riproduzione: %s", video_index + 1, state);
    emit(LEVEL_INFO, msg);
    g_free(msg);
}

void
log_timeline_seek(int video_index, int64_t position_ms)
{
    int64_t seconds = position_ms / 1000;
    char *msg = g_strdup_printf("[VIDEO %d] Timeline seek: %02d:%02d (%lldms)", video_index + 1,
                                (int)(seconds / 60), (int)(seconds % 60), (long long)position_ms);
    emit(LEVEL_INFO, msg);
    g_free(msg);
}

void
log_error(const char *message, const GError *error)
{
    emit(LEVEL_ERROR, message);
    if (error && error->message) {
        emit(LEVEL_ERROR, error->message);
    }
}

void
log_export(const char *video_name, gboolean success, const char *error_message)
{
    char *msg;

    if (success) {
        msg = g_strdup_printf("\xe2\x9c\x93 Esportazione completata: %s", video_name);
        emit(LEVEL_INFO, msg);
    } else {
        msg = g_strdup_printf("\xe2\x9c\x97 Esportazione fallita: %s - %s", video_name,
                              error_message ? error_message : "");
        emit(LEVEL_ERROR, msg);
    }
    g_free(msg);
}

void
log_export_action(const char *action, const char *details)
{
    emit_with_details(LEVEL_INFO, "[EXPORT] ", action, details);
}

static void
emit_debug_category(const char *tag, const char *fmt, va_list args)
{
    char *text = g_strdup_vprintf(fmt, args);
    char *msg = g_strdup_printf("[%s] %s", tag, text);

    emit(LEVEL_DEBUG, msg);

    g_free(msg);
    g_free(text);
}

#define DEFINE_DEBUG_LOG(name, tag)           \
    void name(const char *fmt, ...)           \
    {                                         \
        va_list args;                         \
        va_start(args, fmt);                  \
        emit_debug_category(tag, fmt, args);  \
        va_end(args);                         \
    }

DEFINE_DEBUG_LOG(log_sync, "SYNC")
DEFINE_DEBUG_LOG(log_marker, "MARKER")
DEFINE_DEBUG_LOG(log_gst, "GST")
DEFINE_DEBUG_LOG(log_ui, "UI")
