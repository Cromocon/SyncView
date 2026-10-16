/*
 * M1.15: filtro per modulo e sink del logger (base delle finestre di debug).
 */
#include "core/logger.h"

#include <fcntl.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define HAS(text, needle) (strstr((text), (needle)) != NULL)

/* --- cattura di stderr (come test_logger.c) --- */
static int saved_stderr = -1;

static void
capture_begin(const char *path)
{
    fflush(stderr);
    saved_stderr = dup(2);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    g_assert_true(fd >= 0 && saved_stderr >= 0);
    dup2(fd, 2);
    close(fd);
}

static void
capture_end(void)
{
    fflush(stderr);
    dup2(saved_stderr, 2);
    close(saved_stderr);
}

static char *
read_file(const char *path)
{
    char *content = NULL;
    g_assert_true(g_file_get_contents(path, &content, NULL, NULL));
    return content;
}

/* --- sink di test: registra ciò che riceve --- */
typedef struct {
    GMutex lock;
    GPtrArray *messages;   /* char*: "LIVELLO|MODULO|messaggio" */
    int ts_ok;             /* timestamp con formato atteso */
} Recorder;

static void
recorder_init(Recorder *r)
{
    g_mutex_init(&r->lock);
    r->messages = g_ptr_array_new_with_free_func(g_free);
    r->ts_ok = 0;
}

static void
recorder_clear(Recorder *r)
{
    g_mutex_lock(&r->lock);
    g_ptr_array_set_size(r->messages, 0);
    g_mutex_unlock(&r->lock);
}

static void
recorder_sink(LoggerLevel level, LoggerModule module, const char *timestamp, const char *message,
              gpointer user_data)
{
    Recorder *r = user_data;
    static const char *const levels[] = { "DEBUG", "INFO", "WARNING", "ERROR" };

    g_mutex_lock(&r->lock);
    g_ptr_array_add(r->messages, g_strdup_printf("%s|%s|%s", levels[level], logger_module_name(module), message));
    if (strlen(timestamp) == 23 && timestamp[4] == '-' && timestamp[10] == ' ' && timestamp[19] == '.') {
        r->ts_ok++;
    }
    g_mutex_unlock(&r->lock);
}

static guint
recorder_count(Recorder *r)
{
    g_mutex_lock(&r->lock);
    guint n = r->messages->len;
    g_mutex_unlock(&r->lock);
    return n;
}

static gboolean
recorder_has(Recorder *r, const char *exact)
{
    gboolean found = FALSE;

    g_mutex_lock(&r->lock);
    for (guint i = 0; i < r->messages->len && !found; i++) {
        found = strcmp(g_ptr_array_index(r->messages, i), exact) == 0;
    }
    g_mutex_unlock(&r->lock);
    return found;
}

static void
log_one_of_each(void)
{
    GError *error = g_error_new_literal(G_FILE_ERROR, G_FILE_ERROR_FAILED, "dettaglio");

    log_user_action("u", NULL);
    log_video_action(0, "v", NULL);
    log_playback(0, "playing");
    log_timeline_seek(0, 1000);
    log_export("clip", TRUE, NULL);
    log_export_action("e", NULL);
    log_sync("s");
    log_marker("m");
    log_gst("g");
    log_ui("i");
    log_error("err", error);
    g_error_free(error);
}

static void
test_names_and_defaults(void)
{
    const char *expected[] = { "APP", "USER", "VIDEO", "EXPORT", "SYNC", "MARKER", "GST", "UI" };

    g_assert_true(LOGGER_MODULE_COUNT == G_N_ELEMENTS(expected));
    for (int m = 0; m < LOGGER_MODULE_COUNT; m++) {
        g_assert_cmpstr(logger_module_name(m), ==, expected[m]);
        g_assert_true(logger_is_module_enabled(m));
    }
    g_assert_cmpstr(logger_module_name(LOGGER_MODULE_COUNT), ==, "?");
    g_assert_cmpstr(logger_module_name(-1), ==, "?");
    g_assert_false(logger_is_module_enabled(LOGGER_MODULE_COUNT));
    logger_set_module_enabled(LOGGER_MODULE_COUNT, FALSE);  /* indice non valido: ignorato */
}

/* Filtro su tutte le destinazioni (file, stderr, sink). */
static void
test_filter_all_destinations(const char *dir)
{
    g_unsetenv("SYNCVIEW_DEBUG");
    char *log_path = g_build_filename(dir, "filter.log", NULL);
    char *err_path = g_build_filename(dir, "filter.err", NULL);
    Recorder rec;
    recorder_init(&rec);

    capture_begin(err_path);
    g_assert_true(logger_init(log_path, TRUE, NULL));
    guint sink = logger_add_sink(recorder_sink, &rec);
    g_assert_true(sink > 0);

    /* 1) Tutto abilitato: ogni messaggio arriva ovunque. */
    log_sync("sync-uno");
    log_marker("marker-uno");

    /* 2) SYNC disabilitato: sparisce da file, stderr e sink; MARKER resta. */
    logger_set_module_enabled(LOGGER_MODULE_SYNC, FALSE);
    g_assert_true(!logger_is_module_enabled(LOGGER_MODULE_SYNC) && logger_is_module_enabled(LOGGER_MODULE_MARKER));
    log_sync("sync-due-NASCOSTO");
    log_marker("marker-due");

    /* 3) Riabilitato: torna. */
    logger_set_module_enabled(LOGGER_MODULE_SYNC, TRUE);
    log_sync("sync-tre");

    logger_remove_sink(sink);
    logger_shutdown();
    capture_end();

    char *file = read_file(log_path);
    char *err = read_file(err_path);
    const char *dests[] = { file, err };
    for (int i = 0; i < 2; i++) {
        g_assert_true(HAS(dests[i], "sync-uno") && HAS(dests[i], "marker-uno"));
        g_assert_false(HAS(dests[i], "sync-due-NASCOSTO"));
        g_assert_true(HAS(dests[i], "marker-due") && HAS(dests[i], "sync-tre"));
    }
    g_assert_true(recorder_has(&rec, "DEBUG|SYNC|[SYNC] sync-uno"));
    g_assert_true(recorder_has(&rec, "DEBUG|MARKER|[MARKER] marker-due"));
    g_assert_true(recorder_has(&rec, "DEBUG|SYNC|[SYNC] sync-tre"));
    g_assert_false(recorder_has(&rec, "DEBUG|SYNC|[SYNC] sync-due-NASCOSTO"));
    g_assert_cmpint(recorder_count(&rec), ==, 4);  /* solo i 4 messaggi passati dal filtro ("avviata" precede il sink) */

    g_free(file);
    g_free(err);
    g_free(log_path);
    g_free(err_path);
    g_ptr_array_free(rec.messages, TRUE);
}

/* Mappatura categorie storiche -> moduli, ed ERROR mai filtrato. */
static void
test_legacy_mapping_and_errors(const char *dir)
{
    g_unsetenv("SYNCVIEW_DEBUG");
    char *log_path = g_build_filename(dir, "map.log", NULL);
    Recorder rec;
    recorder_init(&rec);

    g_assert_true(logger_init(log_path, TRUE, NULL));
    guint sink = logger_add_sink(recorder_sink, &rec);

    /* Tutto abilitato: modulo e livello corretti per ogni funzione. */
    recorder_clear(&rec);
    log_one_of_each();
    g_assert_true(recorder_has(&rec, "INFO|USER|[AZIONE UTENTE] u"));
    g_assert_true(recorder_has(&rec, "INFO|VIDEO|[VIDEO 1] v"));
    g_assert_true(recorder_has(&rec, "INFO|VIDEO|[VIDEO 1] Stato riproduzione: playing"));
    g_assert_true(recorder_has(&rec, "INFO|VIDEO|[VIDEO 1] Timeline seek: 00:01 (1000ms)"));
    g_assert_true(recorder_has(&rec, "INFO|EXPORT|\xe2\x9c\x93 Esportazione completata: clip"));
    g_assert_true(recorder_has(&rec, "INFO|EXPORT|[EXPORT] e"));
    g_assert_true(recorder_has(&rec, "DEBUG|SYNC|[SYNC] s"));
    g_assert_true(recorder_has(&rec, "DEBUG|MARKER|[MARKER] m"));
    g_assert_true(recorder_has(&rec, "DEBUG|GST|[GST] g"));
    g_assert_true(recorder_has(&rec, "DEBUG|UI|[UI] i"));
    g_assert_true(recorder_has(&rec, "ERROR|APP|err"));
    g_assert_true(recorder_has(&rec, "ERROR|APP|dettaglio"));
    g_assert_cmpint(recorder_count(&rec), ==, 12);
    g_assert_cmpint(rec.ts_ok, ==, 12);  /* timestamp nel formato del file */

    /* Tutti i moduli disabilitati: restano solo gli ERROR (anche ERROR di un modulo disabilitato). */
    for (int m = 0; m < LOGGER_MODULE_COUNT; m++) {
        logger_set_module_enabled(m, FALSE);
    }
    recorder_clear(&rec);
    log_one_of_each();
    log_export("clip-ko", FALSE, "motivo");  /* ERROR del modulo EXPORT, disabilitato */
    g_assert_cmpint(recorder_count(&rec), ==, 3);
    g_assert_true(recorder_has(&rec, "ERROR|APP|err") && recorder_has(&rec, "ERROR|APP|dettaglio"));
    g_assert_true(recorder_has(&rec, "ERROR|EXPORT|\xe2\x9c\x97 Esportazione fallita: clip-ko - motivo"));

    /* Disabilitando un solo modulo storico spariscono solo le sue funzioni. */
    for (int m = 0; m < LOGGER_MODULE_COUNT; m++) {
        logger_set_module_enabled(m, TRUE);
    }
    logger_set_module_enabled(LOGGER_MODULE_VIDEO, FALSE);
    recorder_clear(&rec);
    log_one_of_each();
    g_assert_true(recorder_count(&rec) == 12 - 3);  /* video_action, playback, timeline_seek */
    g_assert_false(recorder_has(&rec, "INFO|VIDEO|[VIDEO 1] v"));
    logger_set_module_enabled(LOGGER_MODULE_USER, FALSE);
    recorder_clear(&rec);
    log_one_of_each();
    g_assert_true(recorder_count(&rec) == 12 - 3 - 1);
    g_assert_false(recorder_has(&rec, "INFO|USER|[AZIONE UTENTE] u"));

    /* logger_init() riporta il filtro al default. */
    logger_remove_sink(sink);
    logger_shutdown();
    g_assert_true(logger_init(NULL, TRUE, NULL));
    g_assert_true(logger_is_module_enabled(LOGGER_MODULE_VIDEO) && logger_is_module_enabled(LOGGER_MODULE_USER));
    logger_shutdown();

    g_free(log_path);
    g_ptr_array_free(rec.messages, TRUE);
}

/* Il filtro vale anche in modalità normale (sul file), dove non ci sono sink. */
static void
test_filter_in_normal_mode_and_no_sink_without_debug(const char *dir)
{
    g_unsetenv("SYNCVIEW_DEBUG");
    char *log_path = g_build_filename(dir, "normal.log", NULL);
    char *err_path = g_build_filename(dir, "normal.err", NULL);
    Recorder rec;
    recorder_init(&rec);

    capture_begin(err_path);
    g_assert_true(logger_init(log_path, FALSE, NULL));
    guint sink = logger_add_sink(recorder_sink, &rec);

    log_user_action("visibile-utente", NULL);
    log_export_action("nascosto-export", NULL);  /* prima del filtro: presente */
    logger_set_module_enabled(LOGGER_MODULE_EXPORT, FALSE);
    log_export_action("nascosto-export-2", NULL);
    log_error("errore-sempre", NULL);

    logger_remove_sink(sink);
    logger_shutdown();
    capture_end();

    char *file = read_file(log_path);
    char *err = read_file(err_path);
    g_assert_cmpint(strlen(err), ==, 0);                       /* niente stderr senza debug */
    g_assert_cmpint(recorder_count(&rec), ==, 0);              /* nessun sink invocato senza debug */
    g_assert_true(HAS(file, "visibile-utente") && HAS(file, "nascosto-export\n"));
    g_assert_false(HAS(file, "nascosto-export-2"));
    g_assert_true(HAS(file, "errore-sempre"));

    g_free(file);
    g_free(err);
    g_free(log_path);
    g_free(err_path);
    g_ptr_array_free(rec.messages, TRUE);
}

/* Più sink, rimozione, id distinti. */
static void
test_multiple_sinks_and_removal(void)
{
    g_unsetenv("SYNCVIEW_DEBUG");
    Recorder a, b;
    recorder_init(&a);
    recorder_init(&b);

    g_assert_true(logger_init(NULL, TRUE, NULL));
    guint ida = logger_add_sink(recorder_sink, &a);
    guint idb = logger_add_sink(recorder_sink, &b);
    g_assert_true(ida > 0 && idb > 0 && ida != idb);

    log_sync("uno");
    g_assert_true(recorder_has(&a, "DEBUG|SYNC|[SYNC] uno") && recorder_has(&b, "DEBUG|SYNC|[SYNC] uno"));

    logger_remove_sink(ida);
    logger_remove_sink(ida);   /* id già rimosso: no-op */
    logger_remove_sink(9999);  /* id sconosciuto: no-op */
    log_sync("due");
    g_assert_true(!recorder_has(&a, "DEBUG|SYNC|[SYNC] due") && recorder_has(&b, "DEBUG|SYNC|[SYNC] due"));

    logger_remove_sink(idb);
    logger_shutdown();
    g_ptr_array_free(a.messages, TRUE);
    g_ptr_array_free(b.messages, TRUE);
}

/* Un sink che logga a sua volta non causa ricorsione né deadlock. */
typedef struct {
    int calls;
    guint self_id;
} ReentrantState;

static void
reentrant_sink(LoggerLevel level, LoggerModule module, const char *timestamp, const char *message,
               gpointer user_data)
{
    (void)level; (void)module; (void)timestamp;
    ReentrantState *st = user_data;

    st->calls++;
    if (strstr(message, "originale")) {
        log_sync("dal-sink");            /* non deve rientrare nei sink */
        /* Registrare/rimuovere sink dal sink non deve andare in deadlock. */
        guint extra = logger_add_sink(reentrant_sink, st);
        logger_remove_sink(extra);
    }
}

static void
test_reentrant_sink(const char *dir)
{
    g_unsetenv("SYNCVIEW_DEBUG");
    char *log_path = g_build_filename(dir, "reent.log", NULL);
    ReentrantState st = { 0, 0 };

    g_assert_true(logger_init(log_path, TRUE, NULL));
    st.self_id = logger_add_sink(reentrant_sink, &st);

    log_sync("originale");
    logger_remove_sink(st.self_id);
    logger_shutdown();

    g_assert_cmpint(st.calls, ==, 1);  /* "dal-sink" non è stato recapitato ai sink */
    char *file = read_file(log_path);
    g_assert_true(HAS(file, "originale") && HAS(file, "dal-sink"));  /* ma è nel file */
    g_free(file);
    g_free(log_path);
}

/* Log concorrenti da più thread: nessuna perdita, nessun crash. */
#define N_THREADS 4
#define N_PER_THREAD 200

static gpointer
logging_thread(gpointer data)
{
    int id = GPOINTER_TO_INT(data);

    for (int i = 0; i < N_PER_THREAD; i++) {
        log_sync("thread-%d-msg-%d", id, i);
        if (i % 50 == 0) {
            logger_set_module_enabled(LOGGER_MODULE_UI, i % 100 == 0);  /* toggle concorrente di un altro modulo */
        }
    }
    return NULL;
}

static void
test_concurrent_logging(void)
{
    g_unsetenv("SYNCVIEW_DEBUG");
    Recorder rec;
    recorder_init(&rec);

    g_assert_true(logger_init(NULL, TRUE, NULL));
    guint sink = logger_add_sink(recorder_sink, &rec);

    GThread *threads[N_THREADS];
    for (int t = 0; t < N_THREADS; t++) {
        threads[t] = g_thread_new("log", logging_thread, GINT_TO_POINTER(t));
    }
    for (int t = 0; t < N_THREADS; t++) {
        g_thread_join(threads[t]);
    }

    logger_remove_sink(sink);
    logger_shutdown();

    g_assert_true(recorder_count(&rec) == N_THREADS * N_PER_THREAD);  /* nessun messaggio SYNC perso */
    for (int t = 0; t < N_THREADS; t++) {
        char *last = g_strdup_printf("DEBUG|SYNC|[SYNC] thread-%d-msg-%d", t, N_PER_THREAD - 1);
        g_assert_true(recorder_has(&rec, last));
        g_free(last);
    }
    g_ptr_array_free(rec.messages, TRUE);
}

int
main(void)
{
    char *dir = g_dir_make_tmp("syncview-logger-filter-XXXXXX", NULL);
    g_assert_nonnull(dir);

    test_names_and_defaults();
    test_filter_all_destinations(dir);
    test_legacy_mapping_and_errors(dir);
    test_filter_in_normal_mode_and_no_sink_without_debug(dir);
    test_multiple_sinks_and_removal();
    test_reentrant_sink(dir);
    test_concurrent_logging();

    char *cmd = g_strdup_printf("rm -rf '%s'", dir);
    g_assert_cmpint(system(cmd), ==, 0);
    g_free(cmd);
    g_free(dir);
    return 0;
}
