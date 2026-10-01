#include "ui/debug_modules_window.h"

#include "core/logger.h"
#include "ui/theme.h"

#include <string.h>

#define MODULES_WINDOW_DATA "syncview-debug-modules-window"

static const char *MODULE_DESCRIPTIONS[LOGGER_MODULE_COUNT] = {
    [LOGGER_MODULE_APP] = "Avvio dell'applicazione e messaggi generali",
    [LOGGER_MODULE_USER] = "Azioni dell'utente (apri video, scorciatoie)",
    [LOGGER_MODULE_VIDEO] = "Caricamento, riproduzione, seek, passi per frame",
    [LOGGER_MODULE_EXPORT] = "Coda di esportazione e clip",
    [LOGGER_MODULE_SYNC] = "Offset e sincronizzazione tra i video",
    [LOGGER_MODULE_MARKER] = "Creazione ed elenco dei marker",
    [LOGGER_MODULE_GST] = "GStreamer: stati, ASYNC_DONE, decoder in uso",
    [LOGGER_MODULE_UI] = "Eventi dell'interfaccia e del tema",
};

typedef struct {
    GHashTable *widgets;
    GtkWidget *switches[LOGGER_MODULE_COUNT];
} ModulesWindow;

static ModulesWindow *
mw_of(GtkWidget *window)
{
    return window ? g_object_get_data(G_OBJECT(window), MODULES_WINDOW_DATA) : NULL;
}

/* L'interruttore è collegato direttamente al filtro del logger: vale subito su file, stderr e finestra Log. */
static gboolean
on_switch_state_set(GtkSwitch *toggle, gboolean state, gpointer user_data)
{
    LoggerModule module = (LoggerModule)GPOINTER_TO_INT(user_data);

    logger_set_module_enabled(module, state);
    gtk_switch_set_state(toggle, state);
    return TRUE;
}

static void
on_all_clicked(GtkButton *button, gpointer user_data)
{
    ModulesWindow *mw = user_data;

    (void)button;
    for (int m = 0; m < LOGGER_MODULE_COUNT; m++) {
        gtk_switch_set_active(GTK_SWITCH(mw->switches[m]), TRUE);
    }
}

static void
on_close_clicked(GtkButton *button, gpointer user_data)
{
    (void)user_data;
    gtk_window_close(GTK_WINDOW(gtk_widget_get_root(GTK_WIDGET(button))));
}

static void
on_window_destroy(GtkWidget *window, gpointer user_data)
{
    ModulesWindow *mw = user_data;

    (void)window;
    g_hash_table_destroy(mw->widgets);
    g_free(mw);
}

GtkWidget *
syncview_debug_modules_window_new(GtkApplication *app, GtkWindow *parent)
{
    ModulesWindow *mw = g_new0(ModulesWindow, 1);
    GtkWidget *window = gtk_application_window_new(app);

    syncview_theme_init(gdk_display_get_default());

    mw->widgets = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    gtk_window_set_title(GTK_WINDOW(window), "Debug · Moduli di log");
    gtk_window_set_default_size(GTK_WINDOW(window), 560, 560);
    gtk_widget_add_css_class(window, "syncview");
    if (parent) {
        gtk_window_set_transient_for(GTK_WINDOW(window), parent);
    }
    g_object_set_data(G_OBJECT(window), MODULES_WINDOW_DATA, mw);

    GtkWidget *header = gtk_header_bar_new();
    GtkWidget *title = gtk_label_new("Debug · Moduli di log");

    gtk_widget_add_css_class(title, "sv-title");
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header), title);
    gtk_window_set_titlebar(GTK_WINDOW(window), header);

    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);

    gtk_widget_set_margin_top(content, 14);
    gtk_widget_set_margin_bottom(content, 14);
    gtk_widget_set_margin_start(content, 14);
    gtk_widget_set_margin_end(content, 14);

    GtkWidget *intro = gtk_label_new("Scegli quali moduli scrivono nel log. Le modifiche valgono subito. "
                                     "Gli errori si registrano sempre, anche con il modulo spento.");

    gtk_widget_add_css_class(intro, "sv-log-time");
    gtk_label_set_wrap(GTK_LABEL(intro), TRUE);
    gtk_label_set_xalign(GTK_LABEL(intro), 0.0f);
    gtk_box_append(GTK_BOX(content), intro);

    for (int m = 0; m < LOGGER_MODULE_COUNT; m++) {
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        GtkWidget *texts = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        GtkWidget *name = gtk_label_new(logger_module_name((LoggerModule)m));
        GtkWidget *desc = gtk_label_new(MODULE_DESCRIPTIONS[m]);
        GtkWidget *toggle = gtk_switch_new();
        char *key = g_strdup_printf("switch-%s", logger_module_name((LoggerModule)m));

        gtk_widget_add_css_class(row, "sv-module-row");
        gtk_widget_add_css_class(name, "sv-module-name");
        gtk_widget_add_css_class(desc, "sv-module-desc");
        gtk_label_set_xalign(GTK_LABEL(name), 0.0f);
        gtk_label_set_xalign(GTK_LABEL(desc), 0.0f);
        gtk_label_set_wrap(GTK_LABEL(desc), TRUE);
        gtk_widget_set_hexpand(texts, TRUE);
        gtk_widget_set_valign(toggle, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(texts), name);
        gtk_box_append(GTK_BOX(texts), desc);
        gtk_box_append(GTK_BOX(row), texts);
        gtk_box_append(GTK_BOX(row), toggle);
        gtk_box_append(GTK_BOX(content), row);

        gtk_switch_set_state(GTK_SWITCH(toggle), logger_is_module_enabled((LoggerModule)m));
        gtk_switch_set_active(GTK_SWITCH(toggle), logger_is_module_enabled((LoggerModule)m));
        g_signal_connect(toggle, "state-set", G_CALLBACK(on_switch_state_set), GINT_TO_POINTER(m));
        mw->switches[m] = toggle;
        g_hash_table_insert(mw->widgets, key, toggle);
    }

    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *all = gtk_button_new_with_label("Tutti");
    GtkWidget *close = gtk_button_new_with_label("Chiudi");

    gtk_widget_set_vexpand(spacer, TRUE);
    gtk_widget_set_halign(buttons, GTK_ALIGN_END);
    gtk_widget_add_css_class(all, "sv-btn");
    gtk_widget_add_css_class(close, "sv-btn");
    gtk_widget_add_css_class(close, "sv-primary");
    g_signal_connect(all, "clicked", G_CALLBACK(on_all_clicked), mw);
    g_signal_connect(close, "clicked", G_CALLBACK(on_close_clicked), NULL);
    g_hash_table_insert(mw->widgets, g_strdup("all"), all);
    g_hash_table_insert(mw->widgets, g_strdup("close"), close);
    gtk_box_append(GTK_BOX(buttons), all);
    gtk_box_append(GTK_BOX(buttons), close);
    gtk_box_append(GTK_BOX(content), spacer);
    gtk_box_append(GTK_BOX(content), buttons);

    gtk_window_set_child(GTK_WINDOW(window), content);
    g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy), mw);
    return window;
}

GtkWidget *
syncview_debug_modules_window_get_widget(GtkWidget *window, const char *name)
{
    ModulesWindow *mw = mw_of(window);

    g_return_val_if_fail(mw != NULL && name != NULL, NULL);
    return g_hash_table_lookup(mw->widgets, name);
}
