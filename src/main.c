#include <gtk/gtk.h>

static void
on_activate(GtkApplication *app, gpointer user_data)
{
    (void)user_data;

    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "SyncView");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);
    gtk_window_present(GTK_WINDOW(window));
}

int
main(int argc, char *argv[])
{
    g_autoptr(GtkApplication) app =
        gtk_application_new("com.syncview.SyncView", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);

    return g_application_run(G_APPLICATION(app), argc, argv);
}
