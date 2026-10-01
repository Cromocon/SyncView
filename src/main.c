#include "core/deps_check.h"
#include "core/logger.h"
#include "ui/debug_windows.h"
#include "ui/main_window.h"
#include "video/video_player.h"

#include <gst/gst.h>
#include <gtk/gtk.h>
#include <string.h>

#ifdef G_OS_UNIX
#include <glib-unix.h>
#include <signal.h>
#endif

#ifdef G_OS_UNIX
/* SIGINT/SIGTERM (Ctrl+C, kill, chiusura della sessione): uscita ordinata, con smontaggio di pipeline e gst_deinit. */
static gboolean
on_terminate_signal(gpointer user_data)
{
    log_user_action("Chiusura richiesta dal sistema (segnale)", NULL);
    g_application_quit(G_APPLICATION(user_data));
    return G_SOURCE_REMOVE;
}
#endif

static void
on_activate(GtkApplication *app, gpointer user_data)
{
    (void)user_data;

    GtkWidget *window = syncview_main_window_new(app, NULL);

    gtk_window_present(GTK_WINDOW(window));
    syncview_debug_windows_open(app, GTK_WINDOW(window), NULL, NULL);  /* solo con --debug */
#ifdef G_OS_UNIX
    g_unix_signal_add(SIGINT, on_terminate_signal, app);
    g_unix_signal_add(SIGTERM, on_terminate_signal, app);
#endif
}

/*
 * `syncview --check-deps`: verifica le dipendenze runtime, stampa il report su stdout e termina
 * (exit 0 = riproduzione ed export possibili, 1 = manca qualcosa di richiesto). Non apre finestre.
 */
static int
run_check_deps(void)
{
    DepsReport *report = deps_check_run(NULL, NULL);
    char *text = deps_report_to_text(report);

    deps_report_log(report);
    g_print("%s", text);

    int status = (deps_report_can_play(report) && deps_report_can_export(report)) ? 0 : 1;

    g_free(text);
    deps_report_free(report);
    return status;
}

/*
 * Estrae --debug/-v e --check-deps da argv (GApplication rifiuterebbe opzioni che non
 * conosce); ritorna TRUE se c'è il flag di debug, *check_deps se c'è --check-deps. La modalità debug vera e propria è
 * decisa da core/logger.c (flag CLI oppure SYNCVIEW_DEBUG).
 */
static gboolean
extract_debug_flag(int *argc, char **argv, gboolean *check_deps)
{
    gboolean debug = FALSE;
    int out = 1;

    *check_deps = FALSE;
    for (int i = 1; i < *argc; i++) {
        if (strcmp(argv[i], "--debug") == 0 || strcmp(argv[i], "-v") == 0) {
            debug = TRUE;
        } else if (strcmp(argv[i], "--check-deps") == 0) {
            *check_deps = TRUE;
        } else {
            argv[out++] = argv[i];
        }
    }

    argv[out] = NULL;
    *argc = out;
    return debug;
}

int
main(int argc, char *argv[])
{
    gboolean check_deps = FALSE;
    gboolean cli_debug = extract_debug_flag(&argc, argv, &check_deps);

    /* Prima di qualunque altra inizializzazione (GTK, futuro gst_init): in debug imposta GST_DEBUG. */
    char *log_file = logger_default_file();
    GError *log_error_details = NULL;

    if (!logger_init(log_file, cli_debug, &log_error_details)) {
        /* L'app parte comunque: il logger resta attivo (stderr in debug). */
        log_error("Impossibile aprire il file di log", log_error_details);
        g_clear_error(&log_error_details);
    }
    g_free(log_file);

    if (check_deps) {
        int deps_status = run_check_deps();

        logger_shutdown();
        return deps_status;
    }

    g_autoptr(GtkApplication) app =
        gtk_application_new("com.syncview.SyncView", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);

    int status = g_application_run(G_APPLICATION(app), argc, argv);

    /* Uscita dal main loop (anche da segnale): le finestre vanno distrutte perché rilascino i player. */
    GList *toplevels = g_list_copy(gtk_window_list_toplevels());

    for (GList *l = toplevels; l; l = l->next) {
        g_object_ref(l->data);
    }
    for (GList *l = toplevels; l; l = l->next) {
        gtk_window_destroy(GTK_WINDOW(l->data));
    }
    for (GList *l = toplevels; l; l = l->next) {
        g_object_unref(l->data);
    }
    g_list_free(toplevels);

    /*
     * Chiusa la finestra il player può avere ancora una pipeline che sta scendendo di stato (smontaggio rimandato):
     * si lascia girare il main loop finché non ha finito, poi si rilascia GStreamer (nessun thread orfano all'uscita).
     */
    gint64 deadline = g_get_monotonic_time() + 12 * G_USEC_PER_SEC;

    while (syncview_video_player_pending_teardowns() > 0 && g_get_monotonic_time() < deadline) {
        g_main_context_iteration(NULL, FALSE);
        g_usleep(2000);
    }
    if (gst_is_initialized()) {
        gst_deinit();
    }

    log_user_action("Applicazione chiusa", NULL);
    logger_shutdown();
    return status;
}
