#ifndef SYNCVIEW_CORE_DEPS_CHECK_H
#define SYNCVIEW_CORE_DEPS_CHECK_H

#include <glib.h>

/*
 * Verifica delle dipendenze runtime (M2.10): plugin GStreamer per il
 * playback e ffmpeg per l'export. Non scarica né installa nulla (M2.11):
 * produce un report con, per ogni componente, stato, funzione che ne
 * dipende e *come risolvere* (scaricabile / istruzioni). Nessuna
 * dipendenza da GTK. Le sonde sul sistema sono iniettabili, così i test
 * usano sistemi finti.
 *
 * I componenti e i loro id:
 *   gst-playbin3        (playback)  playbin3
 *   gst-gtk4sink        (playback)  gtk4paintablesink
 *   gst-demuxers        (playback)  un demuxer per ogni formato supportato
 *   gst-decoder-h264    (playback)  decoder H.264, software o hardware
 *   gst-decoder-hevc    (opzionale) H.265/HEVC
 *   gst-decoder-vp9     (opzionale) VP9
 *   gst-decoder-av1     (opzionale) AV1
 *   gst-hw-decoders     (opzionale) decoder hardware di piattaforma
 *   ffmpeg              (export)    ffmpeg con un encoder H.264
 */

typedef enum {
    DEPS_PLATFORM_LINUX,
    DEPS_PLATFORM_WINDOWS,
    DEPS_PLATFORM_MACOS,
} DepsPlatform;

/* Piattaforma su cui gira il programma (compile-time). */
DepsPlatform deps_current_platform(void);

typedef enum {
    DEPS_FEATURE_PLAYBACK,  /* senza questo componente non si riproduce */
    DEPS_FEATURE_EXPORT,    /* senza questo componente non si esporta (il resto funziona) */
    DEPS_FEATURE_OPTIONAL,  /* migliora/estende: mancanza = avviso, nessuna funzione persa del tutto */
} DepsFeature;

typedef enum {
    DEPS_STATUS_OK,
    DEPS_STATUS_MISSING,           /* componente richiesto assente */
    DEPS_STATUS_OPTIONAL_MISSING,  /* componente opzionale assente */
} DepsStatus;

/*
 * Come si ottiene un componente mancante. L'app deve poterlo installare da sola
 * (M2.11); `INSTRUCTIONS` è solo il ripiego quando non esiste un modo automatico
 * verificato.
 */
typedef enum {
    DEPS_RESOLUTION_NONE,             /* niente da fare (stato OK) */
    DEPS_RESOLUTION_SYSTEM_PACKAGES,  /* pacchetti della distribuzione, installati dall'app tramite il gestore
                                         di pacchetti con elevazione dei privilegi gestita dal sistema (la
                                         password la chiede il sistema, mai l'app): vedi package_manager/packages */
    DEPS_RESOLUTION_DOWNLOADABLE,     /* artefatto scaricato dall'app in ~/.syncview/deps, senza privilegi */
    DEPS_RESOLUTION_INSTRUCTIONS,     /* nessun modo automatico verificato: va installato a mano (vedi `instructions`) */
} DepsResolution;

typedef struct {
    char *id;            /* id stabile, vedi elenco sopra */
    char *title;         /* nome leggibile */
    DepsFeature feature;
    DepsStatus status;
    DepsResolution resolution;
    char *detail;        /* cosa è stato trovato / cosa manca (mai NULL) */
    char *instructions;  /* come risolvere a mano (mai NULL; "" se status OK): ripiego anche quando l'app può installare */

    /*
     * Solo per resolution == DEPS_RESOLUTION_SYSTEM_PACKAGES (altrimenti NULL): programma del gestore di pacchetti
     * ("pacman", "apt-get", "dnf", "zypper") e nomi dei pacchetti, NULL-terminati. Sono nomi da una tabella interna
     * verificata, mai testo preso da input esterno: l'installer li passa al gestore come argomenti, senza shell.
     */
    char *package_manager;
    char **packages;
} DepsItem;

typedef struct DepsReport DepsReport;

/*
 * Sonde sul sistema. Ogni campo può essere NULL nell'insieme di default
 * (deps_probes_default riempie tutto); nei test si sostituiscono.
 */
typedef struct {
    /* TRUE se l'elemento GStreamer `name` è nel registry (non carica il plugin). */
    gboolean (*has_gst_element)(const char *name, gpointer user_data);
    /* Percorso del programma: prima in `extra_dir` (se non NULL: <dir>/bin e <dir>), poi nel PATH. Da liberare con g_free. */
    char *(*find_program)(const char *name, const char *extra_dir, gpointer user_data);
    /* Esegue `path` con gli argomenti (NULL-terminati) e ritorna lo stdout (g_free), o NULL se fallisce/timeout. */
    char *(*run_program)(const char *path, const char *const *args, gpointer user_data);
    DepsPlatform platform;
    gpointer user_data;
} DepsProbes;

/* Sonde reali: registry GStreamer, ricerca nel PATH, esecuzione con timeout di 5 s. */
void deps_probes_default(DepsProbes *probes);

/* ~/.syncview/deps: dove M2.11 installa ciò che scarica. Da liberare con g_free. */
char *deps_default_dir(void);

/*
 * Esegue tutti i controlli. probes NULL = sonde reali. deps_dir = directory
 * in cui cercare per prima ffmpeg scaricato (NULL = deps_default_dir()).
 * Inizializza GStreamer se servono le sonde reali. Non ritorna mai NULL.
 */
DepsReport *deps_check_run(const DepsProbes *probes, const char *deps_dir);

void deps_report_free(DepsReport *report);

DepsPlatform deps_report_get_platform(const DepsReport *report);
size_t deps_report_count(const DepsReport *report);
const DepsItem *deps_report_get(const DepsReport *report, size_t index);
/* Componente per id, o NULL. */
const DepsItem *deps_report_find(const DepsReport *report, const char *id);

/* TRUE se nessun componente PLAYBACK è mancante. */
gboolean deps_report_can_play(const DepsReport *report);
/* TRUE se nessun componente EXPORT è mancante. */
gboolean deps_report_can_export(const DepsReport *report);
/* TRUE se tutto è OK, opzionali compresi. */
gboolean deps_report_is_complete(const DepsReport *report);

/*
 * Pacchetti di sistema da installare per risolvere i componenti mancanti con resolution SYSTEM_PACKAGES:
 * senza duplicati, nell'ordine dei componenti, NULL-terminati (da liberare con g_strfreev); NULL se non ce ne
 * sono. I componenti opzionali sono inclusi solo con include_optional. *package_manager riceve il programma
 * del gestore (g_free), o NULL. Tutti i componenti di un report usano lo stesso gestore.
 */
char **deps_report_collect_packages(const DepsReport *report, gboolean include_optional, char **package_manager);

/* Report leggibile, una riga per componente + istruzioni per i mancanti. Da liberare con g_free. */
char *deps_report_to_text(const DepsReport *report);

/* Scrive un riepilogo nel log (azione utente + dettagli [GST] in debug). */
void deps_report_log(const DepsReport *report);

#endif /* SYNCVIEW_CORE_DEPS_CHECK_H */
