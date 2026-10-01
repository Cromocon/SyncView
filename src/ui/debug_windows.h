#ifndef SYNCVIEW_UI_DEBUG_WINDOWS_H
#define SYNCVIEW_UI_DEBUG_WINDOWS_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

/*
 * Apre le finestre di debug (Log e Moduli) SOLO se il logger è in modalità debug; ritorna quante ne ha aperte (0 o 2).
 * Sono legate alla finestra principale: chiudendo questa si chiudono anche le altre. `log_out`/`modules_out` (opzionali)
 * ricevono le finestre, NULL se non aperte.
 */
guint syncview_debug_windows_open(GtkApplication *app, GtkWindow *main_window, GtkWidget **log_out,
                                  GtkWidget **modules_out);

G_END_DECLS

#endif /* SYNCVIEW_UI_DEBUG_WINDOWS_H */
