#ifndef SYNCVIEW_UI_DEPS_DIALOG_H
#define SYNCVIEW_UI_DEPS_DIALOG_H

#include "core/dep_installer.h"
#include "core/deps_check.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

/*
 * Finestra «Preparazione di SyncView» (M2.12): primo avvio e voce di menu «Verifica dipendenze». Mostra il report di
 * deps_check, l'elenco ESATTO di ciò che verrà installato (pacchetti o URL, dimensioni) e, solo dopo il consenso
 * dell'utente (clic su «Installa»), esegue l'installazione di core/dep_installer in un thread di lavoro riportando
 * progresso e output nel main thread. Nessuna installazione parte da sola e un rifiuto o un'autorizzazione negata NON
 * si ripetono da soli (si riprova solo con «Riprova installazione»).
 *
 * Fasi (schermate del design, direzione 1b):
 *   REVIEW   cosa manca, cosa verrà installato, avviso sulla password; Installa / Continua senza / Annulla / Installa a mano
 *   RUNNING  righe con stato, barra di avanzamento, output in tempo reale, «Annulla installazione»
 *   SUCCESS  tutto installato e ricontrollato
 *   FAILED   installazione fallita, negata o annullata: motivo, tabella di installazione manuale, Riprova
 *   MANUAL   istruzioni per ogni sistema (comando copiabile) e «Ricontrolla»
 *   ALL_OK   verifica richiesta dall'utente e tutto a posto
 *
 * «Continua senza» ricorda la scelta in ~/.syncview/deps_state.json per esattamente quell'insieme di componenti mancanti
 * (non si ripropone all'avvio finché non cambia); chiudere il dialogo in altro modo non ricorda nulla.
 */
typedef enum {
    SYNCVIEW_DEPS_DIALOG_REVIEW,
    SYNCVIEW_DEPS_DIALOG_RUNNING,
    SYNCVIEW_DEPS_DIALOG_SUCCESS,
    SYNCVIEW_DEPS_DIALOG_FAILED,
    SYNCVIEW_DEPS_DIALOG_MANUAL,
    SYNCVIEW_DEPS_DIALOG_ALL_OK,
} SyncviewDepsDialogPhase;

typedef enum {
    SYNCVIEW_DEPS_OUTCOME_CLOSED,           /* chiusa senza scegliere (Annulla, X) */
    SYNCVIEW_DEPS_OUTCOME_INSTALLED,        /* installazione riuscita e ricontrollata */
    SYNCVIEW_DEPS_OUTCOME_CONTINUED_WITHOUT /* «Continua senza» */
} SyncviewDepsOutcome;

typedef struct {
    /* Controllo delle dipendenze (viene eseguito in un thread di lavoro). NULL = deps_check_run(NULL, NULL). */
    DepsReport *(*check)(gpointer user_data);
    gpointer check_data;

    /* Opzioni dell'installatore (solo per i test: pkexec finto, http su loopback, directory...). progress, user_data,
     * cancellable e consent_third_party sono impostati dal dialogo. NULL = predefinite. */
    const DepRunOptions *run_options;
    const char *os_release_path;  /* per i test dei piani (Fedora): NULL = /etc/os-release */

    const char *state_file;       /* NULL = ~/.syncview/deps_state.json */

    /* Chiamata quando la finestra si chiude (dopo aver salvato lo stato). */
    void (*done)(GtkWidget *dialog, SyncviewDepsOutcome outcome, gpointer user_data);
    gpointer done_data;

    gboolean restart_hint;        /* dopo l'installazione avvisa di riavviare SyncView (il player non era disponibile) */
} SyncviewDepsDialogOptions;

/*
 * Crea la finestra (modale rispetto a `parent`, che può essere NULL). Prende il possesso di `report`. Non la mostra:
 * il chiamante usa gtk_window_present().
 */
GtkWidget *syncview_deps_dialog_new(GtkWindow *parent, DepsReport *report, const SyncviewDepsDialogOptions *options);

SyncviewDepsDialogPhase syncview_deps_dialog_get_phase(GtkWidget *dialog);
SyncviewDepsOutcome syncview_deps_dialog_get_outcome(GtkWidget *dialog);
/* Ultimo report (transfer none). */
const DepsReport *syncview_deps_dialog_get_report(GtkWidget *dialog);

/*
 * Widget per nome, per i test (NULL se nella fase corrente non esiste): pulsanti "install", "continue", "cancel", "manual",
 * "retry", "close", "copy", "recheck", "cancel-install", "back"; "banner", "plan", "status", "output", "opt-check",
 * "third-check", "note"; "badge:<id componente>"; "os:<apt-get|pacman|dnf|zypper|windows|macos>".
 */
GtkWidget *syncview_deps_dialog_get_widget(GtkWidget *dialog, const char *name);

typedef enum {
    SYNCVIEW_DEPS_SHOW_STARTUP,         /* avvio: solo se manca qualcosa che l'utente non ha già scelto di ignorare */
    SYNCVIEW_DEPS_SHOW_USER_REQUESTED,  /* voce di menu «Verifica dipendenze»: sempre (tutto a posto = schermata ALL_OK) */
    SYNCVIEW_DEPS_SHOW_AFTER_ERROR,     /* il player ha segnalato un plugin mancante: se manca qualcosa, anche se ignorato prima */
} SyncviewDepsShowMode;

/*
 * Esegue il controllo in un thread di lavoro e poi apre il dialogo secondo `mode`. Se esiste già un dialogo per `parent`,
 * lo porta in primo piano. Le opzioni vengono copiate.
 */
void syncview_deps_check_and_show(GtkWindow *parent, SyncviewDepsShowMode mode, const SyncviewDepsDialogOptions *options);

/*
 * Copia profonda delle opzioni (stringhe e opzioni dell'installatore comprese), per chi le conserva (la finestra
 * principale). Da liberare con syncview_deps_dialog_options_free(). NULL se `options` è NULL.
 */
SyncviewDepsDialogOptions *syncview_deps_dialog_options_dup(const SyncviewDepsDialogOptions *options);
void syncview_deps_dialog_options_free(SyncviewDepsDialogOptions *options);

/* Il dialogo aperto per `parent`, o NULL. */
GtkWidget *syncview_deps_dialog_find(GtkWindow *parent);

G_END_DECLS

#endif /* SYNCVIEW_UI_DEPS_DIALOG_H */
