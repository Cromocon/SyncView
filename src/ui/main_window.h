#ifndef SYNCVIEW_UI_MAIN_WINDOW_H
#define SYNCVIEW_UI_MAIN_WINDOW_H

#include "ui/deps_dialog.h"
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

/*
 * Verifica delle dipendenze (M2.12). Con `user_requested` FALSE è il controllo d'avvio: la finestra «Preparazione di
 * SyncView» compare solo se manca qualcosa che l'utente non ha già scelto di ignorare; con TRUE (voce di menu «Verifica
 * dipendenze») compare sempre. Il controllo gira in un thread di lavoro: la finestra non si blocca.
 * Se il player segnala un decoder/plugin mancante, la finestra rilancia da sola il controllo (se manca qualcosa).
 */
void syncview_main_window_check_dependencies(GtkWidget *window, gboolean user_requested);

/* Opzioni del dialogo delle dipendenze (sostituzione del controllo, installatore finto...): per i test. NULL = predefinite. */
void syncview_main_window_set_deps_options(GtkWidget *window, const SyncviewDepsDialogOptions *options);

SyncviewMainWindowState syncview_main_window_get_state(GtkWidget *window);

/* Il player incorporato (transfer none), o NULL se non è stato possibile crearlo (manca un componente GStreamer). */
SyncviewVideoPlayer *syncview_main_window_get_player(GtkWidget *window);

/*
 * Widget per nome, per i test: "play", "step-m10", "step-m1", "step-p1", "step-p10", "seek", "time", "open",
 * "card-title", "card-detail", "menu", "menu-deps". NULL se il nome non esiste.
 */
GtkWidget *syncview_main_window_get_widget(GtkWidget *window, const char *name);

G_END_DECLS

#endif /* SYNCVIEW_UI_MAIN_WINDOW_H */
