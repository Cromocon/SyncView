#ifndef SYNCVIEW_CORE_DEPS_STATE_H
#define SYNCVIEW_CORE_DEPS_STATE_H

#include "core/deps_check.h"

#include <glib.h>

/*
 * Stato delle dipendenze tra un avvio e l'altro (M2.12), in ~/.syncview/deps_state.json. Serve a non riproporre
 * l'installazione a ogni avvio a chi ha scelto «Continua senza»: la scelta vale finché l'insieme dei componenti mancanti
 * resta lo stesso; se cambia (ne manca uno nuovo, o uno è stato risolto) la richiesta si ripresenta. Annullare il
 * dialogo NON è una scelta e non viene ricordato. Nessuna dipendenza da GTK.
 *
 * Un file illegibile o corrotto equivale a «nessuna scelta»: non è mai un errore per l'avvio.
 */
typedef struct DepsState DepsState;

/* ~/.syncview/deps_state.json (g_free). */
char *deps_state_default_file(void);

/* Legge lo stato; non ritorna mai NULL. path NULL = file predefinito. */
DepsState *deps_state_load(const char *path);
void deps_state_free(DepsState *state);

/* Scrive il file (crea la cartella; scrittura atomica). */
gboolean deps_state_save(DepsState *state, GError **error);

/*
 * «Firma» dei componenti mancanti di un report: gli id dei componenti non OK, in ordine alfabetico, separati da virgola
 * ("" se non manca nulla). Da liberare con g_free.
 */
char *deps_report_missing_signature(const DepsReport *report);

/*
 * TRUE se all'avvio va proposta l'installazione: manca qualcosa (richiesto o opzionale) e l'utente non ha già scelto
 * «Continua senza» per esattamente questo insieme di componenti.
 */
gboolean deps_state_should_prompt(const DepsState *state, const DepsReport *report);

/* Ricorda «Continua senza» per i componenti mancanti del report (non salva su disco: serve deps_state_save). */
void deps_state_set_declined(DepsState *state, const DepsReport *report);
/* Dimentica la scelta (dopo un'installazione riuscita o una verifica richiesta dall'utente che trova tutto a posto). */
void deps_state_clear_declined(DepsState *state);
/* Firma per cui l'utente ha scelto «Continua senza», o NULL. */
const char *deps_state_get_declined(const DepsState *state);

/* Ultimo controllo eseguito (secondi Unix, 0 = mai). Si aggiorna con deps_state_mark_checked(). */
gint64 deps_state_get_last_check(const DepsState *state);
void deps_state_mark_checked(DepsState *state);

#endif /* SYNCVIEW_CORE_DEPS_STATE_H */
