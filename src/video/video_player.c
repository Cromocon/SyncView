#include "video/video_player.h"

#include "core/logger.h"
#include "core/settings.h"

G_DEFINE_QUARK(syncview-video-player-error-quark, syncview_video_player_error)

enum {
    SIGNAL_LOAD_STATE_CHANGED,
    SIGNAL_ERROR,
    SIGNAL_PLAYBACK_STATE_CHANGED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

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

    guint bus_watch_id;      /* sorgente del bus nel main context; rimossa in dispose */
    char *path;              /* ultimo file passato a load() con successo, owned */
    gboolean loading;        /* load() accettato, ASYNC_DONE/errore non ancora arrivati */
    gboolean loaded;

    SyncviewPlaybackState playback_state;
    gboolean at_end;         /* il video è arrivato in fondo: il prossimo play() riparte dall'inizio */
};

G_DEFINE_FINAL_TYPE(SyncviewVideoPlayer, syncview_video_player, G_TYPE_OBJECT)

static void
syncview_video_player_dispose(GObject *object)
{
    SyncviewVideoPlayer *self = SYNCVIEW_VIDEO_PLAYER(object);

    /* Prima di tutto si ferma il bus: nessun callback deve vedere un player a metà distruzione. */
    if (self->bus_watch_id) {
        g_source_remove(self->bus_watch_id);
        self->bus_watch_id = 0;
    }

    if (self->pipeline) {
        /* Sempre riportata a NULL prima del rilascio: libera risorse di decoder/sink. */
        gst_element_set_state(self->pipeline, GST_STATE_NULL);
    }

    g_clear_pointer(&self->path, g_free);

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

    signals[SIGNAL_LOAD_STATE_CHANGED] = g_signal_new(
        "load-state-changed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
        G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
    signals[SIGNAL_ERROR] = g_signal_new(
        "error", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
        G_TYPE_NONE, 1, G_TYPE_STRING);
    signals[SIGNAL_PLAYBACK_STATE_CHANGED] = g_signal_new(
        "playback-state-changed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
        G_TYPE_NONE, 1, G_TYPE_UINT);
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

static gboolean on_bus_message(GstBus *bus, GstMessage *message, gpointer user_data);

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

    /* Messaggi del bus nel main context corrente (quello GTK); rimosso in dispose. */
    GstBus *bus = gst_element_get_bus(self->pipeline);
    self->bus_watch_id = gst_bus_add_watch(bus, on_bus_message, self);
    gst_object_unref(bus);

    return TRUE;
}

/* --- Decoder in uso (ottimizzazione O6) --- */

#define DECODER_FACTORY_TYPE (GST_ELEMENT_FACTORY_TYPE_DECODER | GST_ELEMENT_FACTORY_TYPE_MEDIA_VIDEO)

/* Primo elemento decoder video nella pipeline (ricorsivo: playbin3 annida decodebin3); ref al chiamante. */
static GstElement *
find_video_decoder(GstElement *pipeline)
{
    GstIterator *it = gst_bin_iterate_recurse(GST_BIN(pipeline));
    GValue value = G_VALUE_INIT;
    GstElement *found = NULL;
    gboolean done = FALSE;

    while (!done) {
        switch (gst_iterator_next(it, &value)) {
        case GST_ITERATOR_OK: {
            GstElement *element = g_value_get_object(&value);
            GstElementFactory *factory = gst_element_get_factory(element);

            if (factory && gst_element_factory_list_is_type(factory, DECODER_FACTORY_TYPE)) {
                found = gst_object_ref(element);
                done = TRUE;
            }
            g_value_reset(&value);
            break;
        }
        case GST_ITERATOR_RESYNC:
            gst_iterator_resync(it);
            break;
        default:
            done = TRUE;
        }
    }

    g_value_unset(&value);
    gst_iterator_free(it);
    return found;
}

static gboolean
decoder_is_hardware(GstElement *decoder)
{
    GstElementFactory *factory = gst_element_get_factory(decoder);
    return factory && gst_element_factory_list_is_type(factory, GST_ELEMENT_FACTORY_TYPE_HARDWARE);
}

char *
syncview_video_player_get_decoder_description(SyncviewVideoPlayer *self)
{
    g_return_val_if_fail(SYNCVIEW_IS_VIDEO_PLAYER(self), NULL);

    if (!self->loaded) {
        return NULL;
    }

    GstElement *decoder = find_video_decoder(self->pipeline);
    if (!decoder) {
        return NULL;
    }

    char *description = g_strdup_printf("%s (%s)", GST_OBJECT_NAME(gst_element_get_factory(decoder)),
                                        decoder_is_hardware(decoder) ? "hardware" : "software");
    gst_object_unref(decoder);
    return description;
}

/* Elenco (separato da virgole) dei decoder hardware installati che accetterebbero questi caps; NULL se nessuno. */
static char *
hardware_alternatives_for(GstCaps *caps)
{
    GList *factories = gst_element_factory_list_get_elements(
        DECODER_FACTORY_TYPE | GST_ELEMENT_FACTORY_TYPE_HARDWARE, GST_RANK_MARGINAL);
    GPtrArray *names = g_ptr_array_new_with_free_func(g_free);

    for (GList *l = factories; l; l = l->next) {
        GstElementFactory *factory = l->data;

        if (caps && gst_element_factory_can_sink_any_caps(factory, caps)) {
            g_ptr_array_add(names, g_strdup(GST_OBJECT_NAME(factory)));
        }
    }
    gst_plugin_feature_list_free(factories);

    char *joined = NULL;
    if (names->len > 0) {
        g_ptr_array_add(names, NULL);
        joined = g_strjoinv(", ", (char **)names->pdata);
    }
    g_ptr_array_free(names, TRUE);
    return joined;
}

/* Logga (debug) il decoder in uso e, se è software pur essendo disponibile l'hardware, lo segnala. */
static void
log_decoder_in_use(SyncviewVideoPlayer *self)
{
    GstElement *decoder = find_video_decoder(self->pipeline);

    if (!decoder) {
        log_gst("player %d: nessun decoder video (file senza flusso video?)", self->video_index + 1);
        return;
    }

    const char *name = GST_OBJECT_NAME(gst_element_get_factory(decoder));

    if (decoder_is_hardware(decoder)) {
        log_gst("player %d: decoder video in uso: %s (hardware)", self->video_index + 1, name);
    } else {
        GstPad *sink_pad = gst_element_get_static_pad(decoder, "sink");
        GstCaps *caps = sink_pad ? gst_pad_get_current_caps(sink_pad) : NULL;
        char *alternatives = hardware_alternatives_for(caps);

        if (alternatives) {
            log_gst("player %d: decoder video in uso: %s (software) — ATTENZIONE: disponibili decoder "
                    "hardware per questo formato: %s", self->video_index + 1, name, alternatives);
        } else {
            log_gst("player %d: decoder video in uso: %s (software), nessun decoder hardware disponibile "
                    "per questo formato", self->video_index + 1, name);
        }

        g_free(alternatives);
        if (caps) {
            gst_caps_unref(caps);
        }
        if (sink_pad) {
            gst_object_unref(sink_pad);
        }
    }

    gst_object_unref(decoder);
}

/* --- Stato di riproduzione --- */

/* Aggiorna lo stato ed emette il segnale solo se cambia. */
static void
set_playback_state(SyncviewVideoPlayer *self, SyncviewPlaybackState state)
{
    if (self->playback_state == state) {
        return;
    }

    self->playback_state = state;
    g_object_ref(self);  /* un handler potrebbe rilasciare l'ultimo riferimento */
    g_signal_emit(self, signals[SIGNAL_PLAYBACK_STATE_CHANGED], 0, (guint)state);
    g_object_unref(self);
}

/* --- Bus --- */

static const char *
state_name(GstState state)
{
    return gst_element_state_get_name(state);
}

static void
handle_error_message(SyncviewVideoPlayer *self, GstMessage *message)
{
    GError *gst_error = NULL;
    char *debug = NULL;
    gst_message_parse_error(message, &gst_error, &debug);

    char *text = g_strdup_printf("%s", gst_error ? gst_error->message : "errore sconosciuto");
    log_gst("player %d: ERROR dal bus: %s (debug: %s)", self->video_index + 1, text, debug ? debug : "-");

    char *log_text = g_strdup_printf("Errore caricamento video Feed-%d: %s", self->video_index + 1, text);
    log_error(log_text, NULL);
    g_free(log_text);

    gboolean was_loaded = self->loaded;

    /* Pipeline riportata a NULL e stato azzerato prima di avvisare chi ascolta. */
    gst_element_set_state(self->pipeline, GST_STATE_NULL);
    self->loading = FALSE;
    self->loaded = FALSE;

    self->at_end = FALSE;
    set_playback_state(self, SYNCVIEW_PLAYBACK_STOPPED);

    g_object_ref(self);  /* un handler potrebbe rilasciare l'ultimo riferimento */
    g_signal_emit(self, signals[SIGNAL_ERROR], 0, text);
    if (was_loaded) {
        g_signal_emit(self, signals[SIGNAL_LOAD_STATE_CHANGED], 0, FALSE);
    }
    g_object_unref(self);

    g_free(text);
    g_free(debug);
    g_clear_error(&gst_error);
}

static gboolean
on_bus_message(GstBus *bus, GstMessage *message, gpointer user_data)
{
    (void)bus;
    SyncviewVideoPlayer *self = user_data;
    gboolean from_pipeline = GST_MESSAGE_SRC(message) == GST_OBJECT(self->pipeline);

    switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_STATE_CHANGED:
        if (from_pipeline) {
            GstState old_state, new_state, pending;
            gst_message_parse_state_changed(message, &old_state, &new_state, &pending);
            log_gst("player %d: GST_STATE_CHANGED %s -> %s (pending %s)", self->video_index + 1,
                    state_name(old_state), state_name(new_state), state_name(pending));
        }
        break;

    case GST_MESSAGE_ASYNC_DONE:
        log_gst("player %d: ASYNC_DONE%s", self->video_index + 1, from_pipeline ? "" : " (non dalla pipeline)");
        /* Solo il primo dopo un load(): i successivi (seek, M2.7) non sono un nuovo caricamento. */
        if (from_pipeline && self->loading) {
            self->loading = FALSE;
            self->loaded = TRUE;
            set_playback_state(self, SYNCVIEW_PLAYBACK_PAUSED);  /* come l'originale: dopo il load, in pausa sul primo frame */

            char *name = g_path_get_basename(self->path ? self->path : "");
            log_video_action(self->video_index, "Video caricato (async)", name);
            g_free(name);
            log_decoder_in_use(self);

            g_object_ref(self);
            g_signal_emit(self, signals[SIGNAL_LOAD_STATE_CHANGED], 0, TRUE);
            g_object_unref(self);
        }
        break;

    case GST_MESSAGE_EOS:
        /* Fine del video (come QMediaPlayer::EndOfMedia -> Stopped): resta caricato e fermo sull'ultimo frame. */
        log_gst("player %d: EOS", self->video_index + 1);
        if (from_pipeline && self->loaded && self->playback_state == SYNCVIEW_PLAYBACK_PLAYING) {
            gst_element_set_state(self->pipeline, GST_STATE_PAUSED);
            self->at_end = TRUE;
            set_playback_state(self, SYNCVIEW_PLAYBACK_STOPPED);
        }
        break;

    case GST_MESSAGE_ERROR:
        handle_error_message(self, message);
        break;

    case GST_MESSAGE_WARNING: {
        GError *warning = NULL;
        char *debug = NULL;
        gst_message_parse_warning(message, &warning, &debug);
        log_gst("player %d: WARNING dal bus: %s (debug: %s)", self->video_index + 1,
                warning ? warning->message : "?", debug ? debug : "-");
        g_clear_error(&warning);
        g_free(debug);
        break;
    }

    default:
        break;
    }

    return G_SOURCE_CONTINUE;
}

gboolean
syncview_video_player_load(SyncviewVideoPlayer *self, const char *path, GError **error)
{
    g_return_val_if_fail(SYNCVIEW_IS_VIDEO_PLAYER(self), FALSE);

    if (!path || !g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
        g_set_error_literal(error, SYNCVIEW_VIDEO_PLAYER_ERROR,
                            SYNCVIEW_VIDEO_PLAYER_ERROR_FILE_NOT_FOUND, "File non trovato");
        return FALSE;
    }

    GFile *file = g_file_new_for_path(path);
    char *uri = g_file_get_uri(file);
    g_object_unref(file);

    /*
     * Sostituzione di un eventuale video precedente o in caricamento: pipeline a NULL.
     * GstPipeline ha `auto-flush-bus` attivo (default) e svuota il bus scendendo a NULL, quindi
     * un ASYNC_DONE/ERROR del caricamento vecchio già pubblicato ma non ancora consegnato viene
     * scartato e non è scambiato per quello nuovo (verificato dai test).
     */
    gst_pipeline_set_auto_flush_bus(GST_PIPELINE(self->pipeline), TRUE);
    gst_element_set_state(self->pipeline, GST_STATE_NULL);

    self->loaded = FALSE;
    self->loading = FALSE;
    self->at_end = FALSE;
    g_free(self->path);
    self->path = NULL;
    set_playback_state(self, SYNCVIEW_PLAYBACK_STOPPED);  /* il video precedente è stato scartato */

    g_object_set(self->pipeline, "uri", uri, NULL);

    char *name = g_path_get_basename(path);
    log_video_action(self->video_index, "Caricamento avviato", name);
    log_gst("player %d: uri=%s -> PAUSED", self->video_index + 1, uri);
    g_free(name);

    GstStateChangeReturn ret = gst_element_set_state(self->pipeline, GST_STATE_PAUSED);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        g_set_error(error, SYNCVIEW_VIDEO_PLAYER_ERROR, SYNCVIEW_VIDEO_PLAYER_ERROR_PIPELINE,
                    "La pipeline non è riuscita a passare in PAUSED per %s", uri);
        gst_element_set_state(self->pipeline, GST_STATE_NULL);
        g_free(uri);
        return FALSE;
    }

    self->path = g_strdup(path);
    self->loading = TRUE;
    g_free(uri);
    return TRUE;
}

/* --- Riproduzione (M2.5) --- */

static gboolean
require_loaded(SyncviewVideoPlayer *self, GError **error)
{
    if (self->loaded) {
        return TRUE;
    }

    g_set_error_literal(error, SYNCVIEW_VIDEO_PLAYER_ERROR, SYNCVIEW_VIDEO_PLAYER_ERROR_NOT_LOADED,
                        self->loading ? "Video ancora in caricamento" : "Nessun video caricato");
    return FALSE;
}

static gboolean
change_pipeline_state(SyncviewVideoPlayer *self, GstState state, GError **error)
{
    if (gst_element_set_state(self->pipeline, state) == GST_STATE_CHANGE_FAILURE) {
        g_set_error(error, SYNCVIEW_VIDEO_PLAYER_ERROR, SYNCVIEW_VIDEO_PLAYER_ERROR_PIPELINE,
                    "La pipeline non è riuscita a passare in %s", gst_element_state_get_name(state));
        return FALSE;
    }
    return TRUE;
}

/* Riporta il video all'inizio (seek con flush): primo frame visibile, nessun cambio di stato della pipeline. */
static gboolean
rewind_to_start(SyncviewVideoPlayer *self, GError **error)
{
    if (!gst_element_seek_simple(self->pipeline, GST_FORMAT_TIME,
                                 GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE, 0)) {
        g_set_error_literal(error, SYNCVIEW_VIDEO_PLAYER_ERROR, SYNCVIEW_VIDEO_PLAYER_ERROR_PIPELINE,
                            "Impossibile tornare all'inizio del video");
        return FALSE;
    }
    self->at_end = FALSE;
    return TRUE;
}

gboolean
syncview_video_player_play(SyncviewVideoPlayer *self, GError **error)
{
    g_return_val_if_fail(SYNCVIEW_IS_VIDEO_PLAYER(self), FALSE);

    if (!require_loaded(self, error)) {
        return FALSE;
    }

    log_playback(self->video_index, "PLAY");

    /* Dopo la fine del video si riparte dall'inizio (come QMediaPlayer::play a EndOfMedia). */
    if (self->at_end && !rewind_to_start(self, error)) {
        return FALSE;
    }
    if (!change_pipeline_state(self, GST_STATE_PLAYING, error)) {
        return FALSE;
    }

    set_playback_state(self, SYNCVIEW_PLAYBACK_PLAYING);
    return TRUE;
}

gboolean
syncview_video_player_pause(SyncviewVideoPlayer *self, GError **error)
{
    g_return_val_if_fail(SYNCVIEW_IS_VIDEO_PLAYER(self), FALSE);

    if (!require_loaded(self, error)) {
        return FALSE;
    }

    log_playback(self->video_index, "PAUSA");

    if (!change_pipeline_state(self, GST_STATE_PAUSED, error)) {
        return FALSE;
    }

    /* Fermo in fondo al video: resta STOPPED (un pause() non lo trasforma in "in pausa a metà"). */
    if (!self->at_end) {
        set_playback_state(self, SYNCVIEW_PLAYBACK_PAUSED);
    }
    return TRUE;
}

gboolean
syncview_video_player_stop(SyncviewVideoPlayer *self, GError **error)
{
    g_return_val_if_fail(SYNCVIEW_IS_VIDEO_PLAYER(self), FALSE);

    if (!require_loaded(self, error)) {
        return FALSE;
    }

    log_playback(self->video_index, "STOP");

    /* Pausa + ritorno all'inizio: il video resta caricato e il primo frame visibile (non si scende a NULL). */
    if (!change_pipeline_state(self, GST_STATE_PAUSED, error) || !rewind_to_start(self, error)) {
        return FALSE;
    }

    set_playback_state(self, SYNCVIEW_PLAYBACK_STOPPED);
    return TRUE;
}

gboolean
syncview_video_player_toggle_play_pause(SyncviewVideoPlayer *self, GError **error)
{
    g_return_val_if_fail(SYNCVIEW_IS_VIDEO_PLAYER(self), FALSE);

    return self->playback_state == SYNCVIEW_PLAYBACK_PLAYING ? syncview_video_player_pause(self, error)
                                                             : syncview_video_player_play(self, error);
}

SyncviewPlaybackState
syncview_video_player_get_playback_state(SyncviewVideoPlayer *self)
{
    g_return_val_if_fail(SYNCVIEW_IS_VIDEO_PLAYER(self), SYNCVIEW_PLAYBACK_STOPPED);
    return self->playback_state;
}

gboolean
syncview_video_player_is_loaded(SyncviewVideoPlayer *self)
{
    g_return_val_if_fail(SYNCVIEW_IS_VIDEO_PLAYER(self), FALSE);
    return self->loaded;
}

gboolean
syncview_video_player_is_loading(SyncviewVideoPlayer *self)
{
    g_return_val_if_fail(SYNCVIEW_IS_VIDEO_PLAYER(self), FALSE);
    return self->loading;
}

const char *
syncview_video_player_get_path(SyncviewVideoPlayer *self)
{
    g_return_val_if_fail(SYNCVIEW_IS_VIDEO_PLAYER(self), NULL);
    return self->path;
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
