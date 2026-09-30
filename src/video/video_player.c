#include "video/video_player.h"

#include "core/logger.h"
#include "core/settings.h"

G_DEFINE_QUARK(syncview-video-player-error-quark, syncview_video_player_error)

enum {
    PROP_0,
    PROP_VIDEO_INDEX,
    PROP_PAINTABLE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

struct _SyncviewVideoPlayer {
    GObject parent_instance;

    int video_index;
    GstElement *pipeline;    /* playbin3, owned */
    GstElement *video_sink;  /* gtk4paintablesink, owned (ref propria oltre a quella di playbin3) */
    GdkPaintable *paintable; /* owned */
};

G_DEFINE_FINAL_TYPE(SyncviewVideoPlayer, syncview_video_player, G_TYPE_OBJECT)

static void
syncview_video_player_dispose(GObject *object)
{
    SyncviewVideoPlayer *self = SYNCVIEW_VIDEO_PLAYER(object);

    if (self->pipeline) {
        /* Sempre riportata a NULL prima del rilascio: libera risorse di decoder/sink. */
        gst_element_set_state(self->pipeline, GST_STATE_NULL);
    }

    g_clear_object(&self->paintable);
    g_clear_pointer(&self->video_sink, gst_object_unref);
    g_clear_pointer(&self->pipeline, gst_object_unref);

    G_OBJECT_CLASS(syncview_video_player_parent_class)->dispose(object);
}

static void
syncview_video_player_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
    SyncviewVideoPlayer *self = SYNCVIEW_VIDEO_PLAYER(object);

    switch (prop_id) {
    case PROP_VIDEO_INDEX:
        g_value_set_int(value, self->video_index);
        break;
    case PROP_PAINTABLE:
        g_value_set_object(value, self->paintable);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    }
}

static void
syncview_video_player_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
    SyncviewVideoPlayer *self = SYNCVIEW_VIDEO_PLAYER(object);

    switch (prop_id) {
    case PROP_VIDEO_INDEX:  /* construct-only */
        self->video_index = g_value_get_int(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    }
}

static void
syncview_video_player_class_init(SyncviewVideoPlayerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->dispose = syncview_video_player_dispose;
    object_class->get_property = syncview_video_player_get_property;
    object_class->set_property = syncview_video_player_set_property;

    properties[PROP_VIDEO_INDEX] = g_param_spec_int(
        "video-index", NULL, "Indice dello slot video (0-based)", 0, SYNCVIEW_MAX_VIDEOS - 1, 0,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
    properties[PROP_PAINTABLE] = g_param_spec_object(
        "paintable", NULL, "GdkPaintable su cui il sink disegna i frame", GDK_TYPE_PAINTABLE,
        G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties(object_class, N_PROPS, properties);
}

static void
syncview_video_player_init(SyncviewVideoPlayer *self)
{
    self->video_index = 0;
}

/* Crea un elemento GStreamer o imposta un errore che nomina l'elemento mancante. */
static GstElement *
make_element(const char *factory, const char *name, GError **error)
{
    GstElement *element = gst_element_factory_make(factory, name);

    if (!element) {
        g_set_error(error, SYNCVIEW_VIDEO_PLAYER_ERROR, SYNCVIEW_VIDEO_PLAYER_ERROR_MISSING_ELEMENT,
                    "Elemento GStreamer mancante: %s", factory);
    }
    return element;
}

static gboolean
setup_pipeline(SyncviewVideoPlayer *self, GError **error)
{
    char *playbin_name = g_strdup_printf("player-%d", self->video_index + 1);
    char *sink_name = g_strdup_printf("videosink-%d", self->video_index + 1);

    /*
     * Costruzione esplicita (non gst_parse_launch("playbin3 video-sink=...")):
     * permette di dare un errore preciso per ogni elemento mancante e di
     * tenere un riferimento al sink per leggerne il paintable.
     */
    GstElement *playbin = make_element("playbin3", playbin_name, error);
    GstElement *sink = playbin ? make_element("gtk4paintablesink", sink_name, error) : NULL;
    g_free(playbin_name);
    g_free(sink_name);

    if (!playbin || !sink) {
        if (playbin) {
            gst_object_unref(playbin);
        }
        return FALSE;
    }

    self->pipeline = gst_object_ref_sink(playbin);
    self->video_sink = gst_object_ref_sink(sink);
    g_object_set(self->pipeline, "video-sink", self->video_sink, NULL);

    g_object_get(self->video_sink, "paintable", &self->paintable, NULL);
    if (!self->paintable) {
        g_set_error(error, SYNCVIEW_VIDEO_PLAYER_ERROR, SYNCVIEW_VIDEO_PLAYER_ERROR_PIPELINE,
                    "gtk4paintablesink non ha fornito un paintable");
        return FALSE;
    }

    return TRUE;
}

SyncviewVideoPlayer *
syncview_video_player_new(int video_index, GError **error)
{
    if (video_index < 0 || video_index >= SYNCVIEW_MAX_VIDEOS) {
        g_set_error(error, SYNCVIEW_VIDEO_PLAYER_ERROR, SYNCVIEW_VIDEO_PLAYER_ERROR_INDEX,
                    "Indice video non valido: %d", video_index);
        return NULL;
    }

    if (!gst_is_initialized()) {
        gst_init(NULL, NULL);
    }

    SyncviewVideoPlayer *self = g_object_new(SYNCVIEW_TYPE_VIDEO_PLAYER, "video-index", video_index, NULL);

    if (!setup_pipeline(self, error)) {
        log_gst("player %d: creazione pipeline fallita", video_index + 1);
        g_object_unref(self);
        return NULL;
    }

    log_gst("player %d: pipeline creata (playbin3 + gtk4paintablesink), ferma in stato NULL",
            video_index + 1);
    return self;
}

int
syncview_video_player_get_video_index(SyncviewVideoPlayer *self)
{
    g_return_val_if_fail(SYNCVIEW_IS_VIDEO_PLAYER(self), -1);
    return self->video_index;
}

GdkPaintable *
syncview_video_player_get_paintable(SyncviewVideoPlayer *self)
{
    g_return_val_if_fail(SYNCVIEW_IS_VIDEO_PLAYER(self), NULL);
    return self->paintable;
}

GstElement *
syncview_video_player_get_pipeline(SyncviewVideoPlayer *self)
{
    g_return_val_if_fail(SYNCVIEW_IS_VIDEO_PLAYER(self), NULL);
    return self->pipeline;
}
