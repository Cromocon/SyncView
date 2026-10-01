#ifndef SYNCVIEW_UI_DEBUG_MODULES_WINDOW_H
#define SYNCVIEW_UI_DEBUG_MODULES_WINDOW_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

/*
 * Finestra «Debug · Moduli di log» (M2.9): un interruttore per ogni modulo del logger, collegato a
 * logger_set_module_enabled(): vale subito su tutte le destinazioni (file, stderr, finestra Log). Gli errori si
 * registrano sempre, anche con il modulo spento. «Tutti» riaccende ogni modulo.
 */
GtkWidget *syncview_debug_modules_window_new(GtkApplication *app, GtkWindow *parent);

/* Widget per nome: "switch-<MODULO>" (es. "switch-SYNC"), "all", "close". */
GtkWidget *syncview_debug_modules_window_get_widget(GtkWidget *window, const char *name);

G_END_DECLS

#endif /* SYNCVIEW_UI_DEBUG_MODULES_WINDOW_H */
