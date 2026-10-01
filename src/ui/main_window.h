#ifndef SYNCVIEW_UI_MAIN_WINDOW_H
#define SYNCVIEW_UI_MAIN_WINDOW_H

#include "video/video_player.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

/*
 * Finestra principale, versione minima (M2.8): un solo video, apri file, play/pausa, passo per frame, barra di
 * avanzamento. Lo stile viene dal design system (design/, direzione 1b), non dal tema GTK di sistema.
 *
 * Stato del riquadro video:
 *   EMPTY    nessun video (scheda «Nessun video» con il pulsante per aprirne uno)
 *   LOADING  load() accettato, in attesa del primo frame
 *   LOADED   video pronto (in pausa, in riproduzione o fermo)
 *   ERROR    file non trovato o non riproducibile, oppure componente mancante (scheda con il messaggio)
 */
typedef enum {
    SYNCVIEW_MAIN_WINDOW_EMPTY,
    SYNCVIEW_MAIN_WINDOW_LOADING,
    SYNCVIEW_MAIN_WINDOW_LOADED,
    SYNCVIEW_MAIN_WINDOW_ERROR,
} SyncviewMainWindowState;

/*
 * Crea la finestra. `user_paths_file` è il file dei percorsi salvati (NULL = ~/.syncview/user_paths.json): all'avvio, se
 * lo slot 1 contiene un file che esiste ancora, viene ricaricato (come l'originale). Il percorso viene salvato SOLO a
 * caricamento riuscito (ottimizzazione O3): un file non riproducibile non tocca il file dei percorsi.
 */
GtkWidget *syncview_main_window_new(GtkApplication *app, const char *user_paths_file);

/* Avvia il caricamento di un file (come «Apri video…»). FALSE se il file non esiste (la scheda di errore è già mostrata). */
gboolean syncview_main_window_open_file(GtkWidget *window, const char *path);

SyncviewMainWindowState syncview_main_window_get_state(GtkWidget *window);

/* Il player incorporato (transfer none), o NULL se non è stato possibile crearlo (manca un componente GStreamer). */
SyncviewVideoPlayer *syncview_main_window_get_player(GtkWidget *window);

/*
 * Widget per nome, per i test: "play", "step-m10", "step-m1", "step-p1", "step-p10", "seek", "time", "open",
 * "card-title", "card-detail". NULL se il nome non esiste.
 */
GtkWidget *syncview_main_window_get_widget(GtkWidget *window, const char *name);

G_END_DECLS

#endif /* SYNCVIEW_UI_MAIN_WINDOW_H */
