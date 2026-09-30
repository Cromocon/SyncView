#include "core/logger.h"

#include <assert.h>
#include <fcntl.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Cattura di stderr (fd 2) su file, per verificare quanti byte il logger vi scrive. */
static int saved_stderr = -1;

static void
capture_stderr_begin(const char *path)
{
    fflush(stderr);
    saved_stderr = dup(2);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    assert(fd >= 0 && saved_stderr >= 0);
    dup2(fd, 2);
    close(fd);
}

static void
capture_stderr_end(void)
{
    fflush(stderr);
    dup2(saved_stderr, 2);
    close(saved_stderr);
    saved_stderr = -1;
}

static char *
read_file(const char *path)
{
    char *content = NULL;
    assert(g_file_get_contents(path, &content, NULL, NULL));
    return content;
}

/* Invoca TUTTE le funzioni di log (più volte). */
static void
call_every_log_function(void)
{
    GError *error = g_error_new_literal(G_FILE_ERROR, G_FILE_ERROR_FAILED, "dettaglio-errore");

    for (int i = 0; i < 20; i++) {
        log_user_action("azione-utente", "dettagli-azione");
        log_user_action("azione-senza-dettagli", NULL);
        log_video_action(0, "azione-video", "dettagli-video");
        log_playback(1, "playing");
        log_timeline_seek(2, 125999);
        log_error("errore-semplice", NULL);
        log_error("errore-con-gerror", error);
        log_export("clip.mp4", TRUE, NULL);
        log_export("clip-ko.mp4", FALSE, "ffmpeg-fallito");
        log_export_action("job-creato", "id=7");
        log_sync("sync-dettaglio-%d", i);
        log_marker("marker-dettaglio-%s", "x");
        log_gst("gst-dettaglio");
        log_ui("ui-dettaglio");
    }

    g_error_free(error);
}

static int
count_occurrences(const char *haystack, const char *needle)
{
    int n = 0;
    for (const char *p = haystack; (p = strstr(p, needle)); p += strlen(needle)) {
        n++;
    }
    return n;
}

/* Esegue un ciclo completo init/log/shutdown e ritorna contenuto di file e di stderr. */
static void
run_session(const char *dir, const char *tag, gboolean cli_debug, char **file_out, char **stderr_out)
{
    char *log_path = g_build_filename(dir, tag, "log.txt", NULL);  /* directory creata dal logger */
    char *err_path = g_build_filename(dir, tag, "stderr.txt", NULL);
    char *err_dir = g_path_get_dirname(err_path);
    g_mkdir_with_parents(err_dir, 0755);
    g_free(err_dir);

    capture_stderr_begin(err_path);
    assert(logger_init(log_path, cli_debug, NULL));
    call_every_log_function();
    logger_shutdown();
    capture_stderr_end();

    *file_out = read_file(log_path);
    *stderr_out = read_file(err_path);
    g_free(log_path);
    g_free(err_path);
}

static void
test_normal_mode(const char *dir)
{
    g_unsetenv("SYNCVIEW_DEBUG");
    char *file, *err;
    run_session(dir, "normal", FALSE, &file, &err);

    /* Requisito centrale: modalità debug disattivata => ZERO byte su stderr, per qualsiasi numero di chiamate. */
    assert(strlen(err) == 0);

    /* File: intestazione + categorie dell'originale. */
    assert(strstr(file, "SYNCVIEW - AVVIO APPLICAZIONE") != NULL);
    assert(strstr(file, "Timestamp: ") != NULL);
    assert(strstr(file, " - SyncView - INFO - Applicazione SyncView avviata") != NULL);
    assert(count_occurrences(file, "[AZIONE UTENTE] azione-utente - dettagli-azione") == 20);
    assert(count_occurrences(file, "[AZIONE UTENTE] azione-senza-dettagli\n") == 20);
    assert(count_occurrences(file, "INFO - [VIDEO 1] azione-video - dettagli-video") == 20);
    assert(count_occurrences(file, "[VIDEO 2] Stato riproduzione: playing") == 20);
    assert(count_occurrences(file, "[VIDEO 3] Timeline seek: 02:05 (125999ms)") == 20);
    assert(count_occurrences(file, "ERROR - errore-semplice") == 20);
    assert(count_occurrences(file, "ERROR - errore-con-gerror") == 20);
    assert(count_occurrences(file, "ERROR - dettaglio-errore") == 20);
    assert(count_occurrences(file, "INFO - \xe2\x9c\x93 Esportazione completata: clip.mp4") == 20);
    assert(count_occurrences(file, "ERROR - \xe2\x9c\x97 Esportazione fallita: clip-ko.mp4 - ffmpeg-fallito") == 20);
    assert(count_occurrences(file, "INFO - [EXPORT] job-creato - id=7") == 20);

    /* I dettagli di livello DEBUG non compaiono senza modalità debug. */
    assert(strstr(file, "sync-dettaglio") == NULL && strstr(file, "[MARKER]") == NULL);
    assert(strstr(file, "[GST]") == NULL && strstr(file, "[UI]") == NULL);

    /* Formato riga: "YYYY-MM-DD HH:MM:SS.mmm - SyncView - LIVELLO - msg". */
    const char *line = strstr(file, "- SyncView - INFO - [AZIONE UTENTE]");
    assert(line != NULL);
    line -= 24;  /* lunghezza "YYYY-MM-DD HH:MM:SS.mmm " */
    assert(line[4] == '-' && line[7] == '-' && line[10] == ' ' && line[13] == ':' && line[16] == ':' && line[19] == '.');

    g_free(file);
    g_free(err);
}

static void
test_debug_mode(const char *dir, gboolean via_env)
{
    if (via_env) {
        g_setenv("SYNCVIEW_DEBUG", "1", TRUE);
    } else {
        g_unsetenv("SYNCVIEW_DEBUG");
    }

    char *file, *err;
    run_session(dir, via_env ? "debug_env" : "debug_cli", !via_env, &file, &err);
    g_unsetenv("SYNCVIEW_DEBUG");

    /* In debug: tutto anche su stderr, con timestamp, livello e categoria. */
    assert(count_occurrences(err, "sync-dettaglio-") == 20);
    assert(strstr(err, " - SyncView - DEBUG - [SYNC] sync-dettaglio-0") != NULL);
    assert(strstr(err, " - SyncView - DEBUG - [MARKER] marker-dettaglio-x") != NULL);
    assert(strstr(err, " - SyncView - DEBUG - [GST] gst-dettaglio") != NULL);
    assert(strstr(err, " - SyncView - DEBUG - [UI] ui-dettaglio") != NULL);
    assert(strstr(err, "INFO - [AZIONE UTENTE] azione-utente - dettagli-azione") != NULL);
    assert(strstr(err, "ERROR - errore-semplice") != NULL);

    /* E anche sul file, che contiene ciò che c'è in modalità normale più i dettagli DEBUG. */
    assert(count_occurrences(file, "sync-dettaglio-") == 20);
    assert(count_occurrences(file, "[MARKER] marker-dettaglio-x") == 20);
    assert(count_occurrences(file, "[AZIONE UTENTE] azione-utente - dettagli-azione") == 20);
    /* stderr e file hanno le stesse righe di log (l'intestazione di avvio è solo nel file e non ha il prefisso). */
    assert(count_occurrences(err, "\n") == count_occurrences(file, " - SyncView - "));

    g_free(file);
    g_free(err);
}

static void
test_env_values(const char *dir)
{
    /* "0", "false" e stringa vuota NON attivano il debug. */
    const char *off[] = { "0", "false", "FALSE", "" };
    for (size_t i = 0; i < G_N_ELEMENTS(off); i++) {
        g_setenv("SYNCVIEW_DEBUG", off[i], TRUE);
        char *file, *err;
        run_session(dir, "env_off", FALSE, &file, &err);
        assert(strlen(err) == 0);
        g_free(file);
        g_free(err);
    }
    g_unsetenv("SYNCVIEW_DEBUG");
}

static void
test_uninitialized_is_silent(const char *dir)
{
    g_setenv("SYNCVIEW_DEBUG", "1", TRUE);  /* anche con la variabile impostata, senza init niente output */
    char *err_path = g_build_filename(dir, "uninit_stderr.txt", NULL);

    capture_stderr_begin(err_path);
    call_every_log_function();  /* prima di ogni init */
    logger_init(NULL, FALSE, NULL);
    logger_shutdown();
    call_every_log_function();  /* dopo lo shutdown */
    capture_stderr_end();

    char *err = read_file(err_path);
    /* Solo la riga "avviata" emessa da init in debug (env) è ammessa: nessuna delle chiamate fuori sessione. */
    assert(strstr(err, "azione-utente") == NULL && strstr(err, "sync-dettaglio") == NULL);
    g_free(err);
    g_free(err_path);
    g_unsetenv("SYNCVIEW_DEBUG");
}

static void
test_truncates_file_on_open(const char *dir)
{
    char *path = g_build_filename(dir, "truncate.txt", NULL);
    g_unsetenv("SYNCVIEW_DEBUG");

    assert(logger_init(path, FALSE, NULL));
    log_user_action("prima-sessione", NULL);
    logger_shutdown();

    assert(logger_init(path, FALSE, NULL));
    log_user_action("seconda-sessione", NULL);
    logger_shutdown();

    char *content = read_file(path);
    assert(strstr(content, "prima-sessione") == NULL);
    assert(strstr(content, "seconda-sessione") != NULL);
    assert(count_occurrences(content, "AVVIO APPLICAZIONE") == 1);

    g_free(content);
    g_free(path);
}

static void
test_gst_debug_propagation(void)
{
    g_unsetenv("SYNCVIEW_DEBUG");

    /* Debug attivo e GST_DEBUG assente: impostata. */
    g_unsetenv("GST_DEBUG");
    logger_init(NULL, TRUE, NULL);
    logger_shutdown();
    assert(g_strcmp0(g_getenv("GST_DEBUG"), "3") == 0);

    /* Scelta esplicita dell'utente non sovrascritta. */
    g_setenv("GST_DEBUG", "playbin3:5", TRUE);
    logger_init(NULL, TRUE, NULL);
    logger_shutdown();
    assert(g_strcmp0(g_getenv("GST_DEBUG"), "playbin3:5") == 0);

    /* Senza debug GST_DEBUG non viene toccata. */
    g_unsetenv("GST_DEBUG");
    logger_init(NULL, FALSE, NULL);
    logger_shutdown();
    assert(g_getenv("GST_DEBUG") == NULL);
}

static void
test_unwritable_file(const char *dir)
{
    /* Il path è una directory: init segnala l'errore ma il logger resta utilizzabile (stderr in debug). */
    char *path = g_build_filename(dir, "isdir_log", NULL);
    assert(g_mkdir_with_parents(path, 0755) == 0);
    g_unsetenv("SYNCVIEW_DEBUG");
    char *err_path = g_build_filename(dir, "unwritable_stderr.txt", NULL);

    GError *error = NULL;
    capture_stderr_begin(err_path);
    assert(!logger_init(path, TRUE, &error));
    assert(error != NULL);
    log_user_action("ancora-vivo", NULL);
    logger_shutdown();
    capture_stderr_end();

    char *err = read_file(err_path);
    assert(strstr(err, "ancora-vivo") != NULL);

    g_free(err);
    g_error_free(error);
    g_free(err_path);
    g_free(path);
}

static void
test_default_file(void)
{
    char *f = logger_default_file();
    assert(g_str_has_suffix(f, "syncview_log.txt") && strstr(f, ".syncview") != NULL);
    g_free(f);
}

int
main(void)
{
    char *dir = g_dir_make_tmp("syncview-logger-XXXXXX", NULL);
    assert(dir != NULL);

    test_normal_mode(dir);
    test_debug_mode(dir, FALSE);
    test_debug_mode(dir, TRUE);
    test_env_values(dir);
    test_uninitialized_is_silent(dir);
    test_truncates_file_on_open(dir);
    test_gst_debug_propagation();
    test_unwritable_file(dir);
    test_default_file();

    char *cmd = g_strdup_printf("rm -rf '%s'", dir);
    assert(system(cmd) == 0);
    g_free(cmd);
    g_free(dir);
    return 0;
}
