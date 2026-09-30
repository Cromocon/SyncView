#include "core/logger.h"

#include <gtk/gtk.h>
#include <string.h>

static void
on_activate(GtkApplication *app, gpointer user_data)
{
    (void)user_data;

    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "SyncView");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);
    gtk_window_present(GTK_WINDOW(window));

    log_ui("Finestra principale creata (%dx%d)", 800, 600);
}

/*
 * Estrae --debug/-v da argv (GApplication rifiuterebbe opzioni che non
 * conosce) e ritorna TRUE se presenti. La modalità debug vera e propria è
 * decisa da core/logger.c (flag CLI oppure SYNCVIEW_DEBUG).
 */
static gboolean
extract_debug_flag(int *argc, char **argv)
{
    gboolean debug = FALSE;
    int out = 1;

    for (int i = 1; i < *argc; i++) {
        if (strcmp(argv[i], "--debug") == 0 || strcmp(argv[i], "-v") == 0) {
            debug = TRUE;
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
    gboolean cli_debug = extract_debug_flag(&argc, argv);

    /* Prima di qualunque altra inizializzazione (GTK, futuro gst_init): in debug imposta GST_DEBUG. */
    char *log_file = logger_default_file();
    GError *log_error_details = NULL;

    if (!logger_init(log_file, cli_debug, &log_error_details)) {
        /* L'app parte comunque: il logger resta attivo (stderr in debug). */
        log_error("Impossibile aprire il file di log", log_error_details);
        g_clear_error(&log_error_details);
    }
    g_free(log_file);

    g_autoptr(GtkApplication) app =
        gtk_application_new("com.syncview.SyncView", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);

    int status = g_application_run(G_APPLICATION(app), argc, argv);

    log_user_action("Applicazione chiusa", NULL);
    logger_shutdown();
    return status;
}
