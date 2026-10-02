#ifndef SYNCVIEW_CORE_DEP_INSTALLER_H
#define SYNCVIEW_CORE_DEP_INSTALLER_H

#include "core/dep_manifest.h"
#include "core/deps_check.h"

#include <gio/gio.h>
#include <glib.h>

/*
 * Installazione delle dipendenze mancanti (M2.11), senza GTK: «piano → esecuzione con progresso e annullamento».
 *
 * Percorsi (scelti per componente e piattaforma, vedi PLAN.md «Dipendenze al primo avvio»):
 *  - SYSTEM_PACKAGES (Linux): pacchetti della distribuzione in UN'unica transazione tramite `pkexec <gestore> <opzioni
 *    non interattive> <pacchetti>`. Elevazione SOLO con polkit: la password la chiede il sistema, l'app non la vede mai.
 *    argv fisso, senza shell; i nomi dei pacchetti vengono solo dalla tabella interna di deps_check.c (altro è rifiutato).
 *  - ADD_REPOSITORY + pacchetti di terze parti (solo Fedora, solo con consenso dedicato): RPM Fusion per gstreamer1-libav.
 *    openSUSE/Packman NON si automatizza (procedura invasiva non verificata): resta «istruzioni».
 *  - PLATFORM_INSTALLER (Windows/macOS): scarica l'installer ufficiale di GStreamer dal manifest incorporato, ne verifica
 *    dimensione e SHA-256 e lo lancia con l'elevazione del sistema (UAC / Installer di macOS).
 *  - ARCHIVE: archivio .zip in ~/.syncview/deps, senza privilegi (vedi dep_archive.h); nessuna voce nel manifest per ora.
 *
 * Sicurezza: un passo alla volta, mai un processo elevato in parallelo; nessun passo parte senza che il chiamante abbia
 * mostrato l'elenco esatto (dep_plan_describe) e ottenuto il consenso; i passi di terze parti richiedono un consenso a
 * parte (DepRunOptions.consent_third_party). Un rifiuto o un'autorizzazione negata NON si ripete da sola.
 *
 * Annullamento: prima dell'autorizzazione (finestra di polkit aperta) e per i download è immediato. Una volta che il
 * gestore di pacchetti gira come root l'app non può più fermarlo (non può inviare segnali a un processo root, e
 * interrompere una transazione a metà sarebbe peggio): l'annullamento è richiesto ma il passo finisce, e l'esito è
 * quello vero.
 */
#define DEP_INSTALLER_ERROR (dep_installer_error_quark())
GQuark dep_installer_error_quark(void);

typedef enum {
    DEP_INSTALLER_ERROR_INVALID,        /* piano o argomenti non validi (gestore o pacchetto non in tabella, nome sospetto) */
    DEP_INSTALLER_ERROR_NOTHING_TO_DO,  /* niente che l'app possa installare da sola */
    DEP_INSTALLER_ERROR_NO_ELEVATION,   /* pkexec/polkit non disponibile: solo istruzioni a mano */
    DEP_INSTALLER_ERROR_CONSENT,        /* passo di terze parti senza il consenso dedicato */
    DEP_INSTALLER_ERROR_DENIED,         /* autorizzazione negata o finestra del sistema chiusa: nessuna modifica */
    DEP_INSTALLER_ERROR_FAILED,         /* il gestore o l'installer è terminato con errore */
    DEP_INSTALLER_ERROR_CANCELLED,
    DEP_INSTALLER_ERROR_TIMEOUT,
    DEP_INSTALLER_ERROR_DOWNLOAD,       /* rete, HTTP, URL non consentito, spazio insufficiente */
    DEP_INSTALLER_ERROR_VERIFY,         /* dimensione o SHA-256 non corrispondono: file scartato */
    DEP_INSTALLER_ERROR_EXTRACT,        /* archivio non valido o pericoloso */
    DEP_INSTALLER_ERROR_UNSUPPORTED,    /* non disponibile su questa piattaforma */
} DepInstallerError;

typedef enum {
    DEP_STEP_SYSTEM_PACKAGES,
    DEP_STEP_ADD_REPOSITORY,
    DEP_STEP_PLATFORM_INSTALLER,
    DEP_STEP_ARCHIVE,
} DepStepKind;

typedef struct {
    DepStepKind kind;
    char *title;                  /* es. «Installa i pacchetti di sistema» */
    char *summary;                /* cosa verrà fatto, esattamente (pacchetti o URL, dimensioni): per il consenso */
    gboolean needs_elevation;     /* il sistema chiederà la password di amministratore */
    gboolean third_party;         /* richiede il consenso dedicato */
    char **argv;                  /* SYSTEM_PACKAGES/ADD_REPOSITORY: comando SENZA pkexec (argv[0] = gestore), NULL-terminato */
    const DepArtifact *artifact;  /* PLATFORM_INSTALLER/ARCHIVE: voce del manifest (non posseduta) */
    gint64 download_bytes;        /* da scaricare per questo passo (0 se nulla) */
} DepStep;

typedef struct DepPlan DepPlan;

typedef struct {
    gboolean include_optional;     /* includere anche i componenti opzionali mancanti */
    gboolean allow_third_party;    /* consenso dedicato ricevuto: includere i passi di terze parti (solo Fedora) */
    const char *os_release_path;   /* NULL = /etc/os-release (per i test) */
} DepPlanOptions;

/*
 * Costruisce il piano dal report: solo ciò che l'app sa installare. Errore NOTHING_TO_DO se non c'è nulla, INVALID se
 * un nome non passa i controlli. `options` NULL = default (niente opzionali, niente terze parti).
 */
DepPlan *dep_plan_new(const DepsReport *report, const DepPlanOptions *options, GError **error);

/* Piano di un solo artefatto (per i test e per chi scarica un singolo file). Errore INVALID se la voce non è ben formata. */
DepPlan *dep_plan_new_for_artifact(const DepArtifact *artifact, GError **error);

void dep_plan_free(DepPlan *plan);

size_t dep_plan_step_count(const DepPlan *plan);
const DepStep *dep_plan_step(const DepPlan *plan, size_t index);
gboolean dep_plan_needs_elevation(const DepPlan *plan);
gboolean dep_plan_has_third_party(const DepPlan *plan);
gint64 dep_plan_download_bytes(const DepPlan *plan);

/* Testo per il consenso: un blocco per passo con l'elenco esatto di cosa verrà installato. Da liberare con g_free. */
char *dep_plan_describe(const DepPlan *plan);

/*
 * Argv (senza pkexec) per installare `packages` (NULL-terminato) con `package_manager` ("pacman", "apt-get", "dnf",
 * "zypper"). Funzione pura. Rifiuta gestori sconosciuti, nomi non presenti nella tabella interna e nomi con caratteri
 * sospetti (INVALID). Da liberare con g_strfreev.
 */
char **dep_installer_build_package_argv(const char *package_manager, const char *const *packages, GError **error);

typedef enum {
    DEP_PROGRESS_STEP_STARTED,
    DEP_PROGRESS_OUTPUT,       /* una riga di output del gestore (campo `line`) */
    DEP_PROGRESS_DOWNLOAD,     /* bytes_done / bytes_total */
    DEP_PROGRESS_VERIFYING,
    DEP_PROGRESS_STEP_FINISHED,
} DepProgressKind;

typedef struct {
    DepProgressKind kind;
    size_t step_index;
    size_t step_count;
    const char *title;
    const char *line;
    gint64 bytes_done;
    gint64 bytes_total;
} DepProgress;

typedef void (*DepProgressFunc)(const DepProgress *progress, gpointer user_data);

typedef struct {
    DepProgressFunc progress;      /* chiamata nel thread che esegue dep_installer_run() */
    gpointer user_data;
    GCancellable *cancellable;
    guint step_timeout_seconds;    /* 0 = 1800 (30 minuti) per passo */
    gboolean consent_third_party;  /* DEVE essere TRUE per eseguire i passi di terze parti */
    const char *deps_dir;          /* NULL = deps_default_dir() */

    /* Solo per i test: */
    const char *elevation_program; /* NULL = pkexec nel PATH */
    gboolean allow_loopback_http;  /* consente http://127.0.0.1 / localhost (altrimenti solo https) */
    gboolean (*run_installer)(const char *path, GCancellable *cancellable, GError **error, gpointer user_data);
    gpointer run_installer_data;   /* NULL con run_installer NULL = lancio predefinito di piattaforma */
} DepRunOptions;

/*
 * Esegue il piano, un passo alla volta, BLOCCANDO il thread chiamante: l'interfaccia lo esegue in un thread di lavoro e
 * riporta i progressi nel main thread. TRUE se tutti i passi sono riusciti; altrimenti FALSE con l'errore del primo passo
 * fallito (i passi successivi non partono). Dopo il successo il chiamante ricontrolla con deps_check_run().
 */
gboolean dep_installer_run(const DepPlan *plan, const DepRunOptions *options, GError **error);

/*
 * Scarica `artifact` in `dest_path` verificando dimensione esatta e SHA-256 prima di renderlo visibile (scrive su
 * `<dest_path>.part` e rinomina): su qualunque errore o annullamento non resta nulla. Solo https (o loopback con
 * allow_loopback_http, per i test); il controllo vale anche dopo i redirect. Se `dest_path` esiste già ed è corretto
 * non riscarica. Esposta per i test; il normale percorso è dep_installer_run().
 */
gboolean dep_installer_download(const DepArtifact *artifact, const char *dest_path, const DepRunOptions *options,
                                GError **error);

#endif /* SYNCVIEW_CORE_DEP_INSTALLER_H */
