#ifndef SYNCVIEW_UI_THEME_H
#define SYNCVIEW_UI_THEME_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

/*
 * Tema di SyncView: i fogli di stile generati da design/tokens_tool.py (colori + componenti, tema chiaro e scuro) sono
 * incorporati come risorse e applicati al display. Il tema segue il sistema (GtkSettings), a meno che la variabile
 * d'ambiente SYNCVIEW_THEME valga "light" o "dark" (utile per prove e screenshot).
 */
typedef enum {
    SYNCVIEW_THEME_AUTO,   /* come il sistema */
    SYNCVIEW_THEME_LIGHT,
    SYNCVIEW_THEME_DARK,
} SyncviewThemeChoice;

/* Registra le risorse e applica il tema al display (idempotente: le chiamate successive non fanno nulla). */
void syncview_theme_init(GdkDisplay *display);

/* Forza una scelta (AUTO = segue il sistema). Per i test e per un'eventuale preferenza dell'utente. */
void syncview_theme_set_choice(SyncviewThemeChoice choice);

/* TRUE se il tema attualmente applicato è quello scuro. */
gboolean syncview_theme_is_dark(void);

/* Errori di analisi del CSS incontrati finora (dovrebbe restare 0: i test lo verificano). */
guint syncview_theme_parse_errors(void);

G_END_DECLS

#endif /* SYNCVIEW_UI_THEME_H */
