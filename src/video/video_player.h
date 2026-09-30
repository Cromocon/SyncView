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
 * M2.5: play()/pause()/stop()/toggle, stato di riproduzione e segnale
 * playback-state-changed, fine del video (EOS).
 * Posizione/durata/seek arrivano in M2.6+.
 *
 * Segnali (emessi nel main context):
 *   "playback-state-changed" (guint state): SyncviewPlaybackState, solo quando cambia.
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
    SYNCVIEW_VIDEO_PLAYER_ERROR_NOT_LOADED,       /* play/pause/stop senza un video caricato */
} SyncviewVideoPlayerError;

/*
 * Stato di riproduzione, sul modello di QMediaPlayer.PlaybackState dell'originale:
 *  STOPPED  nessun video, oppure video fermo all'inizio (dopo stop()) o arrivato in fondo (fine del video);
 *  PLAYING  in riproduzione;
 *  PAUSED   in pausa (anche subito dopo il caricamento: il primo frame è mostrato).
 */
typedef enum {
    SYNCVIEW_PLAYBACK_STOPPED,
    SYNCVIEW_PLAYBACK_PLAYING,
    SYNCVIEW_PLAYBACK_PAUSED,
} SyncviewPlaybackState;

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

/*
 * Riproduzione (M2.5). Come nell'originale agiscono solo a video caricato: altrimenti ritornano
 * FALSE con error NOT_LOADED (chi non è interessato passa NULL) senza cambiare stato né emettere segnali.
 * Se la pipeline non riesce a cambiare stato: FALSE con error PIPELINE. Ogni chiamata registra
 * log_playback ("PLAY"/"PAUSA"/"STOP"), anche se lo stato non cambia.
 *
 *  play():  avvia (PLAYING). Da un video arrivato in fondo riparte dall'inizio.
 *  pause(): mette in pausa (PAUSED), mantenendo la posizione.
 *  stop():  ferma e riporta all'inizio (STOPPED) mantenendo il video CARICATO e il primo frame
 *           visibile, come QMediaPlayer.stop() — NON riporta la pipeline a NULL (che scaricherebbe il
 *           video: per quello c'è unload, M3.8). Deviazione dal piano, che diceva NULL.
 *  toggle_play_pause(): pause() se è in PLAYING, altrimenti play().
 *
 * Il segnale "playback-state-changed" parte solo se lo stato cambia. A fine video il player passa da
 * solo a STOPPED (il video resta caricato e fermo sull'ultimo frame) e un play() successivo riparte da 0.
 */
gboolean syncview_video_player_play(SyncviewVideoPlayer *self, GError **error);
gboolean syncview_video_player_pause(SyncviewVideoPlayer *self, GError **error);
gboolean syncview_video_player_stop(SyncviewVideoPlayer *self, GError **error);
gboolean syncview_video_player_toggle_play_pause(SyncviewVideoPlayer *self, GError **error);

SyncviewPlaybackState syncview_video_player_get_playback_state(SyncviewVideoPlayer *self);

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
