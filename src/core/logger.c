#include "core/logger.h"

#include <errno.h>
#include <glib/gstdio.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef LoggerLevel Level;

#define LEVEL_DEBUG LOGGER_LEVEL_DEBUG
#define LEVEL_INFO LOGGER_LEVEL_INFO
#define LEVEL_ERROR LOGGER_LEVEL_ERROR

static const char *const LEVEL_NAMES[] = { "DEBUG", "INFO", "WARNING", "ERROR" };

static const char *const MODULE_NAMES[LOGGER_MODULE_COUNT] = {
    "APP", "USER", "VIDEO", "EXPORT", "SYNC", "MARKER", "GST", "UI",
};

typedef struct {
    guint id;
    LoggerSinkFunc func;
    gpointer user_data;
} Sink;

/* Stato globale del modulo. L'UNICO punto in cui la modalità debug è valutata. */
static GMutex state_lock;
static gboolean initialized = FALSE;
static gboolean debug_mode = FALSE;
static FILE *log_file = NULL;
static gboolean module_enabled[LOGGER_MODULE_COUNT] = { TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, TRUE };
static GArray *sinks = NULL;  /* Sink, protetto da state_lock */
static guint next_sink_id = 1;

/* Impostato mentre il thread corrente esegue un sink: blocca la ricorsione sui sink. */
static GPrivate in_sink_key = G_PRIVATE_INIT(NULL);

const char *
logger_module_name(LoggerModule module)
{
    return (module >= 0 && module < LOGGER_MODULE_COUNT) ? MODULE_NAMES[module] : "?";
}

void
logger_set_module_enabled(LoggerModule module, gboolean enabled)
{
    if (module < 0 || module >= LOGGER_MODULE_COUNT) {
        return;
    }

    g_mutex_lock(&state_lock);
    module_enabled[module] = enabled ? TRUE : FALSE;
    g_mutex_unlock(&state_lock);
}

gboolean
logger_is_module_enabled(LoggerModule module)
{
    if (module < 0 || module >= LOGGER_MODULE_COUNT) {
        return FALSE;
    }

    g_mutex_lock(&state_lock);
    gboolean enabled = module_enabled[module];
    g_mutex_unlock(&state_lock);
    return enabled;
}

gboolean
logger_is_debug_mode(void)
{
    g_mutex_lock(&state_lock);
    gboolean debug = initialized && debug_mode;
    g_mutex_unlock(&state_lock);
    return debug;
}

guint
logger_add_sink(LoggerSinkFunc func, gpointer user_data)
{
    g_return_val_if_fail(func != NULL, 0);

    g_mutex_lock(&state_lock);
    if (!sinks) {
        sinks = g_array_new(FALSE, FALSE, sizeof(Sink));
    }
    Sink sink = { next_sink_id++, func, user_data };
    g_array_append_val(sinks, sink);
    g_mutex_unlock(&state_lock);

    return sink.id;
}

void
logger_remove_sink(guint sink_id)
{
    g_mutex_lock(&state_lock);
    for (guint i = 0; sinks && i < sinks->len; i++) {
        if (g_array_index(sinks, Sink, i).id == sink_id) {
            g_array_remove_index(sinks, i);
            break;
        }
    }
    g_mutex_unlock(&state_lock);
}

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

/*
 * Scrive una riga già formattata (senza newline) secondo modalità, filtro
 * per modulo e livello, e la recapita ai sink (solo in debug).
 */
static void
emit(Level level, LoggerModule module, const char *message)
{
    Sink *sinks_copy = NULL;
    guint n_sinks = 0;
    char *ts = NULL;

    g_mutex_lock(&state_lock);

    /* I dettagli di livello DEBUG esistono solo in modalità debug; gli ERROR non sono mai filtrati per modulo. */
    gboolean wanted = initialized && (level != LEVEL_DEBUG || debug_mode)
                      && (level == LEVEL_ERROR || module_enabled[module]);

    if (wanted) {
        ts = timestamp_now();
        char *line = g_strdup_printf("%s - SyncView - %s - %s\n", ts, LEVEL_NAMES[level], message);

        if (log_file) {
            fputs(line, log_file);
            fflush(log_file);
        }
        if (debug_mode) {
            fputs(line, stderr);
            fflush(stderr);

            if (sinks && sinks->len > 0 && !g_private_get(&in_sink_key)) {
                n_sinks = sinks->len;
                sinks_copy = g_memdup2(sinks->data, n_sinks * sizeof(Sink));
            }
        }

        g_free(line);
    }

    g_mutex_unlock(&state_lock);

    /* Sink fuori dal lock: possono registrare/rimuovere sink, e un log dal sink non causa deadlock. */
    if (sinks_copy) {
        g_private_set(&in_sink_key, GINT_TO_POINTER(1));
        for (guint i = 0; i < n_sinks; i++) {
            sinks_copy[i].func(level, module, ts, message, sinks_copy[i].user_data);
        }
        g_private_set(&in_sink_key, NULL);
        g_free(sinks_copy);
    }

    g_free(ts);
}

static void
emit_with_details(Level level, LoggerModule module, const char *prefix, const char *text,
                  const char *details)
{
    char *msg = (details && *details) ? g_strdup_printf("%s%s - %s", prefix, text, details)
                                      : g_strdup_printf("%s%s", prefix, text);
    emit(level, module, msg);
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
    for (int m = 0; m < LOGGER_MODULE_COUNT; m++) {
        module_enabled[m] = TRUE;
    }

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

    emit(LEVEL_INFO, LOGGER_MODULE_APP, "Applicazione SyncView avviata");
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
    emit_with_details(LEVEL_INFO, LOGGER_MODULE_USER, "[AZIONE UTENTE] ", action, details);
}

void
log_video_action(int video_index, const char *action, const char *details)
{
    char *prefix = g_strdup_printf("[VIDEO %d] ", video_index + 1);
    emit_with_details(LEVEL_INFO, LOGGER_MODULE_VIDEO, prefix, action, details);
    g_free(prefix);
}

void
log_playback(int video_index, const char *state)
{
    char *msg = g_strdup_printf("[VIDEO %d] Stato riproduzione: %s", video_index + 1, state);
    emit(LEVEL_INFO, LOGGER_MODULE_VIDEO, msg);
    g_free(msg);
}

void
log_timeline_seek(int video_index, int64_t position_ms)
{
    int64_t seconds = position_ms / 1000;
    char *msg = g_strdup_printf("[VIDEO %d] Timeline seek: %02d:%02d (%lldms)", video_index + 1,
                                (int)(seconds / 60), (int)(seconds % 60), (long long)position_ms);
    emit(LEVEL_INFO, LOGGER_MODULE_VIDEO, msg);
    g_free(msg);
}

void
log_error(const char *message, const GError *error)
{
    emit(LEVEL_ERROR, LOGGER_MODULE_APP, message);
    if (error && error->message) {
        emit(LEVEL_ERROR, LOGGER_MODULE_APP, error->message);
    }
}

void
log_export(const char *video_name, gboolean success, const char *error_message)
{
    char *msg;

    if (success) {
        msg = g_strdup_printf("\xe2\x9c\x93 Esportazione completata: %s", video_name);
        emit(LEVEL_INFO, LOGGER_MODULE_EXPORT, msg);
    } else {
        msg = g_strdup_printf("\xe2\x9c\x97 Esportazione fallita: %s - %s", video_name,
                              error_message ? error_message : "");
        emit(LEVEL_ERROR, LOGGER_MODULE_EXPORT, msg);
    }
    g_free(msg);
}

void
log_export_action(const char *action, const char *details)
{
    emit_with_details(LEVEL_INFO, LOGGER_MODULE_EXPORT, "[EXPORT] ", action, details);
}

static void
emit_debug_category(LoggerModule module, const char *fmt, va_list args)
{
    char *text = g_strdup_vprintf(fmt, args);
    char *msg = g_strdup_printf("[%s] %s", MODULE_NAMES[module], text);

    emit(LEVEL_DEBUG, module, msg);

    g_free(msg);
    g_free(text);
}

#define DEFINE_DEBUG_LOG(name, module)           \
    void name(const char *fmt, ...)              \
    {                                            \
        va_list args;                            \
        va_start(args, fmt);                     \
        emit_debug_category(module, fmt, args);  \
        va_end(args);                            \
    }

DEFINE_DEBUG_LOG(log_sync, LOGGER_MODULE_SYNC)
DEFINE_DEBUG_LOG(log_marker, LOGGER_MODULE_MARKER)
DEFINE_DEBUG_LOG(log_gst, LOGGER_MODULE_GST)
DEFINE_DEBUG_LOG(log_ui, LOGGER_MODULE_UI)
