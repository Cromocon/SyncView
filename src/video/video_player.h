#ifndef SYNCVIEW_VIDEO_VIDEO_PLAYER_H
#define SYNCVIEW_VIDEO_VIDEO_PLAYER_H

#include <gdk/gdk.h>
#include <glib-object.h>
#include <gst/gst.h>

G_BEGIN_DECLS

/*
 * Player video: un GObject che possiede direttamente una pipeline GStreamer
 * `playbin3` con `gtk4paintablesink` come video sink (al posto del
 * QMediaPlayer + QVideoWidget dell'originale; vedi PLAN.md, "Video
 * playback"). Il rendering avviene in un GdkPaintable da mostrare in un
 * GtkPicture.
 *
 * M2.3: scheletro (pipeline creata e ferma in GST_STATE_NULL).
 * M2.4: load() asincrono (PAUSED + ASYNC_DONE dal bus) con primo frame nel
 * paintable, segnali load-state-changed/error e log del decoder in uso.
 * Play/seek/posizione arrivano in M2.5+.
 *
 * Segnali (emessi nel main context, dal bus GStreamer):
 *   "load-state-changed" (gboolean loaded): TRUE quando un load() è pronto;
 *                        FALSE se un video caricato viene perso per un errore.
 *   "error" (const char *message): errore GStreamer durante/dopo il load();
 *                        la pipeline viene riportata a NULL.
 *
 * Va usato dal main thread GTK. Vive in una libreria separata da
 * libsyncview_core perché dipende da GTK/GDK.
 */
#define SYNCVIEW_TYPE_VIDEO_PLAYER (syncview_video_player_get_type())
G_DECLARE_FINAL_TYPE(SyncviewVideoPlayer, syncview_video_player, SYNCVIEW, VIDEO_PLAYER, GObject)

#define SYNCVIEW_VIDEO_PLAYER_ERROR (syncview_video_player_error_quark())
GQuark syncview_video_player_error_quark(void);

typedef enum {
    SYNCVIEW_VIDEO_PLAYER_ERROR_INDEX,            /* indice video fuori da 0..SYNCVIEW_MAX_VIDEOS-1 */
    SYNCVIEW_VIDEO_PLAYER_ERROR_MISSING_ELEMENT,  /* elemento GStreamer assente: il messaggio lo nomina */
    SYNCVIEW_VIDEO_PLAYER_ERROR_PIPELINE,         /* pipeline non costruibile / cambio di stato fallito */
    SYNCVIEW_VIDEO_PLAYER_ERROR_FILE_NOT_FOUND,   /* il file da caricare non esiste ("File non trovato") */
    SYNCVIEW_VIDEO_PLAYER_ERROR_PLAYBACK,         /* errore GStreamer durante il caricamento (segnale "error") */
} SyncviewVideoPlayerError;

/*
 * Crea un player per lo slot video_index (0-based). Inizializza GStreamer se
 * serve. Ritorna NULL con error se l'indice non è valido o se mancano
 * `playbin3`/`gtk4paintablesink` (il messaggio nomina l'elemento: tipicamente
 * il plugin gst-plugin-gtk4 non è installato).
 *
 * Proprietà: "video-index" (int, sola lettura dopo la creazione) e
 * "paintable" (GdkPaintable, sola lettura).
 */
SyncviewVideoPlayer *syncview_video_player_new(int video_index, GError **error);

int syncview_video_player_get_video_index(SyncviewVideoPlayer *self);

/*
 * Il GdkPaintable su cui il sink disegna i frame (transfer none: vale finché
 * vive il player). Va assegnato a un GtkPicture con gtk_picture_set_paintable();
 * prima che un video sia caricato non ha dimensioni intrinseche (0x0).
 */
GdkPaintable *syncview_video_player_get_paintable(SyncviewVideoPlayer *self);

/*
 * Carica un file (equivalente di `media_player.setSource(...)` dell'originale)
 * e porta la pipeline in PAUSED: il primo frame viene mostrato nel paintable.
 *
 * Asincrono: ritorna subito TRUE se il caricamento è partito; l'esito arriva
 * dal bus GStreamer, nel main context GTK, con il segnale "load-state-changed"
 * (TRUE all'ASYNC_DONE, cioè a pipeline pronta e primo frame disponibile;
 * il paintable può ricevere le dimensioni intrinseche un istante dopo) oppure
 * "error" (file illeggibile, nessun decoder, ...). Ritorna FALSE con error se
 * il file non esiste (FILE_NOT_FOUND) o se la pipeline non si avvia (PIPELINE),
 * senza emettere segnali.
 *
 * Se era già caricato un video (o uno era in caricamento) viene sostituito: la
 * pipeline torna a NULL e i messaggi del caricamento precedente sono scartati.
 * Il probing dei metadati (GstDiscoverer) resta a carico del chiamante, come
 * nell'originale dove il media veniva caricato *dopo* il probing.
 */
gboolean syncview_video_player_load(SyncviewVideoPlayer *self, const char *path, GError **error);

/* TRUE dopo l'ASYNC_DONE di un load() riuscito, finché non c'è un errore o un nuovo load(). */
gboolean syncview_video_player_is_loaded(SyncviewVideoPlayer *self);

/* TRUE tra un load() accettato e il suo esito (load-state-changed o error). */
gboolean syncview_video_player_is_loading(SyncviewVideoPlayer *self);

/* Percorso dell'ultimo file passato a load() con successo (transfer none), o NULL. */
const char *syncview_video_player_get_path(SyncviewVideoPlayer *self);

/*
 * Decoder video effettivamente in uso, es. "vp8dec (software)" o
 * "vah264dec (hardware)" (ottimizzazione O6). NULL se non c'è un video caricato
 * o il file non ha un flusso video. Il chiamante libera con g_free().
 */
char *syncview_video_player_get_decoder_description(SyncviewVideoPlayer *self);

/* La pipeline `playbin3` sottostante (transfer none). Per uso interno dei moduli video e dei test. */
GstElement *syncview_video_player_get_pipeline(SyncviewVideoPlayer *self);

G_END_DECLS

#endif /* SYNCVIEW_VIDEO_VIDEO_PLAYER_H */
