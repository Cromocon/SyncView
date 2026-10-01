#include "ui/debug_windows.h"

#include "core/logger.h"
#include "ui/debug_log_window.h"
#include "ui/debug_modules_window.h"

/* Chiudendo la finestra principale si distruggono anche quelle di debug (se esistono ancora). */
static void
on_main_destroyed(GtkWidget *main_window, gpointer user_data)
{
    GWeakRef *ref = user_data;
    GtkWidget *debug_window = g_weak_ref_get(ref);

    (void)main_window;
    if (debug_window) {
        gtk_window_destroy(GTK_WINDOW(debug_window));
        g_object_unref(debug_window);
    }
}

static void
free_weak_ref(gpointer data, GClosure *closure)
{
    (void)closure;
    g_weak_ref_clear(data);
    g_free(data);
}

static void
close_with(GtkWindow *main_window, GtkWidget *debug_window)
{
    GWeakRef *ref = g_new0(GWeakRef, 1);

    g_weak_ref_init(ref, debug_window);
    g_signal_connect_data(main_window, "destroy", G_CALLBACK(on_main_destroyed), ref, free_weak_ref, 0);
}

guint
syncview_debug_windows_open(GtkApplication *app, GtkWindow *main_window, GtkWidget **log_out, GtkWidget **modules_out)
{
    if (log_out) {
        *log_out = NULL;
    }
    if (modules_out) {
        *modules_out = NULL;
    }
    if (!logger_is_debug_mode()) {
        return 0;  /* senza --debug compare solo la finestra principale */
    }

    GtkWidget *log_window = syncview_debug_log_window_new(app, main_window);
    GtkWidget *modules_window = syncview_debug_modules_window_new(app, main_window);

    if (main_window) {
        close_with(main_window, log_window);
        close_with(main_window, modules_window);
    }
    gtk_window_present(GTK_WINDOW(log_window));
    gtk_window_present(GTK_WINDOW(modules_window));
    log_ui("Modalità debug: aperte le finestre Log e Moduli");

    if (log_out) {
        *log_out = log_window;
    }
    if (modules_out) {
        *modules_out = modules_window;
    }
    return 2;
}
