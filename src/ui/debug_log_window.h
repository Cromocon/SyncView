#ifndef SYNCVIEW_UI_DEBUG_LOG_WINDOW_H
#define SYNCVIEW_UI_DEBUG_LOG_WINDOW_H

#include "core/logger.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

/*
 * Finestra «Debug · Log in tempo reale» (M2.9). Mostra le righe di log mentre vengono emesse, ricevendole dal sink del
 * logger (che gira nel thread di chi logga, spesso un thread GStreamer): il sink le accoda e le righe vengono mostrate
 * dal main thread. Funziona solo in modalità debug (il logger invoca i sink solo allora).
 *
 * Controlli: filtro per modulo (chip) e per livello minimo (solo nella vista: non cambiano cosa viene scritto, quello è
 * compito della finestra Moduli), Pausa (le righe nuove restano in coda e compaiono alla ripresa), Copia (le righe
 * visibili negli appunti), Svuota. Lo scorrimento automatico segue il fondo e si sospende se si scorre verso l'alto o
 * si mette in pausa. La vista tiene al più SYNCVIEW_LOG_WINDOW_MAX_LINES righe (le più vecchie si scartano).
 */
#define SYNCVIEW_LOG_WINDOW_MAX_LINES 5000

GtkWidget *syncview_debug_log_window_new(GtkApplication *app, GtkWindow *parent);

/*
 * Per i test. Righe nel modello (prima del filtro di vista), righe visibili (dopo il filtro), righe in coda in attesa
 * (in pausa o non ancora mostrate), righe scartate per eccesso di coda.
 */
guint syncview_debug_log_window_get_line_count(GtkWidget *window);
guint syncview_debug_log_window_get_visible_count(GtkWidget *window);
guint syncview_debug_log_window_get_pending_count(GtkWidget *window);
guint syncview_debug_log_window_get_dropped_count(GtkWidget *window);

/* Riga `index` del modello (0 = la più vecchia). FALSE se l'indice non esiste. `message` vale fino alla prossima modifica. */
gboolean syncview_debug_log_window_get_line(GtkWidget *window, guint index, LoggerLevel *level, LoggerModule *module,
                                            const char **message);

/* Mostra subito le righe in coda (come farebbe il main loop). In pausa non fa nulla. */
void syncview_debug_log_window_flush(GtkWidget *window);

/* Widget per nome: "pause", "clear", "copy", "level", "live", "footer", "autoscroll", "chip-<MODULO>" (es. "chip-SYNC"). */
GtkWidget *syncview_debug_log_window_get_widget(GtkWidget *window, const char *name);

G_END_DECLS

#endif /* SYNCVIEW_UI_DEBUG_LOG_WINDOW_H */
