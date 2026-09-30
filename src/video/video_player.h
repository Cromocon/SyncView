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
 * M2.3 (questo file): solo lo scheletro — la pipeline viene creata ma
 * resta ferma in GST_STATE_NULL. Load/play/seek/segnali arrivano in M2.4+.
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
    SYNCVIEW_VIDEO_PLAYER_ERROR_PIPELINE,         /* pipeline non costruibile / paintable non disponibile */
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

/* La pipeline `playbin3` sottostante (transfer none). Per uso interno dei moduli video e dei test. */
GstElement *syncview_video_player_get_pipeline(SyncviewVideoPlayer *self);

G_END_DECLS

#endif /* SYNCVIEW_VIDEO_VIDEO_PLAYER_H */
