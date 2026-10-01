#include "video/video_player.h"

#include "core/logger.h"
#include "core/settings.h"

#include <math.h>

/* Aggiornamenti di posizione non più frequenti di così durante la riproduzione (~50 Hz): basta per la timeline. */
#define POSITION_MIN_INTERVAL_US 20000
/* Intervallo del timer di ripiego (senza widget/frame clock), ~30 Hz. */
#define POSITION_FALLBACK_INTERVAL_MS 33

G_DEFINE_QUARK(syncview-video-player-error-quark, syncview_video_player_error)

enum {
    SIGNAL_LOAD_STATE_CHANGED,
    SIGNAL_ERROR,
    SIGNAL_PLAYBACK_STATE_CHANGED,
    SIGNAL_POSITION_CHANGED,
    SIGNAL_DURATION_CHANGED,
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

/*
 * Il sink riceve i buffer sul thread di streaming: un probe sul suo pad di ingresso registra il timestamp (in stream
 * time, lo stesso della posizione) dell'ultimo frame arrivato e il framerate dai caps. Serve al frame-step esatto
 * (O1), che deve sapere QUALE frame è mostrato, non solo la posizione richiesta: nei container con timestamp in ms
 * (WebM/Matroska) i pts di un video a 30 fps non sono multipli esatti della durata nominale del frame.
 * Dopo un seek accurato il decoder ritaglia il frame all'istante richiesto (pts = punto di arrivo, durata ridotta di
 * conseguenza): il pts non dice più dove il frame INIZIA, ma pts + durata resta la sua fine esatta.
 * Con conteggio dei riferimenti perché il probe può girare mentre il player è già stato distrutto (teardown differito).
 */
typedef struct {
    GMutex lock;
    GstSegment segment;
    gboolean have_segment;
    gint64 pts_ns;       /* stream time dell'ultimo buffer, -1 se nessuno */
    gint64 end_ns;       /* pts + durata dell'ultimo buffer: la FINE del frame, -1 se nessuno (vedi sotto) */
    int fps_n, fps_d;    /* 0/1 = sconosciuto o variabile */
} FrameInfo;

static void
frame_info_clear(gpointer data)
{
    g_mutex_clear(&((FrameInfo *)data)->lock);
}

static FrameInfo *
frame_info_new(void)
{
    FrameInfo *info = g_rc_box_new0(FrameInfo);

    g_mutex_init(&info->lock);
    gst_segment_init(&info->segment, GST_FORMAT_TIME);
    info->pts_ns = -1;
    info->end_ns = -1;
    info->fps_d = 1;
    return info;
}

static void
frame_info_unref(gpointer data)
{
    g_rc_box_release_full(data, frame_info_clear);
}

static void
frame_info_reset(FrameInfo *info)
{
    g_mutex_lock(&info->lock);
    info->pts_ns = -1;
    info->end_ns = -1;
    info->fps_n = 0;
    info->fps_d = 1;
    g_mutex_unlock(&info->lock);
}

static GstPadProbeReturn
on_sink_pad_probe(GstPad *pad, GstPadProbeInfo *probe, gpointer user_data)
{
    FrameInfo *info = user_data;

    (void)pad;
    g_mutex_lock(&info->lock);
    if (probe->type & GST_PAD_PROBE_TYPE_BUFFER) {
        GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(probe);
        if (buffer && GST_BUFFER_PTS_IS_VALID(buffer)) {
            guint64 st = info->have_segment
                             ? gst_segment_to_stream_time(&info->segment, GST_FORMAT_TIME, GST_BUFFER_PTS(buffer))
                             : GST_BUFFER_PTS(buffer);
            info->pts_ns = st == GST_CLOCK_TIME_NONE ? -1 : (gint64)st;
            info->end_ns = (info->pts_ns >= 0 && GST_BUFFER_DURATION_IS_VALID(buffer))
                               ? info->pts_ns + (gint64)GST_BUFFER_DURATION(buffer)
                               : -1;
        }
    } else if (probe->type & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) {
        GstEvent *event = GST_PAD_PROBE_INFO_EVENT(probe);
        if (GST_EVENT_TYPE(event) == GST_EVENT_SEGMENT) {
            const GstSegment *segment;
            gst_event_parse_segment(event, &segment);
            if (segment->format == GST_FORMAT_TIME) {
                gst_segment_copy_into(segment, &info->segment);
                info->have_segment = TRUE;
            }
        } else if (GST_EVENT_TYPE(event) == GST_EVENT_CAPS) {
            GstCaps *caps;
            int n = 0, d = 1;
            gst_event_parse_caps(event, &caps);
            GstStructure *st = gst_caps_get_structure(caps, 0);
            if (st && gst_structure_get_fraction(st, "framerate", &n, &d) && n > 0 && d > 0) {
                info->fps_n = n;
                info->fps_d = d;
            } else {
                info->fps_n = 0;
                info->fps_d = 1;
            }
        }
    }
    g_mutex_unlock(&info->lock);
    return GST_PAD_PROBE_OK;
}

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

    /* Posizione e durata, in ms */
    gint64 duration_ms;
    gint64 last_position_ms;     /* ultimo valore emesso con "position-changed" */
    gint64 last_position_emit_us;

    /* Smontaggio asincrono della pipeline (vedi pipeline_to_null_async) */
    gboolean teardown_running;   /* un set_state(NULL) è in corso nel thread di lavoro */
    char *pending_uri;           /* URI da caricare appena lo smontaggio è finito (load() che sostituisce un video) */

    /* Seek, velocità e frame-step (M2.7) */
    double rate;                 /* velocità di riproduzione (> 0), 1.0 dopo ogni load */
    FrameInfo *frame_info;       /* timestamp dell'ultimo frame e fps, scritti dal thread di streaming (probe sul sink) */
    gint64 pending_seek_ns;      /* destinazione dell'ultimo seek/step non ancora concluso, -1 se nessuno */
    gint64 pending_seek_us;      /* quando è stato emesso (monotonic), per scartarlo se non si conclude mai */
    gboolean snap_to_frame;      /* dopo un frame-step esatto la posizione riportata è l'INIZIO del frame mostrato */
    gint64 anchor_ns;            /* metà del frame a cui l'ultimo frame-step esatto ha portato il video, -1 se non valida */

    /* Polling della posizione, attivo solo in PLAYING */
    GtkWidget *tick_widget;      /* non posseduto: weak ref, vedi on_tick_widget_gone */
    guint tick_callback_id;      /* gtk_widget_add_tick_callback, 0 se assente */
    guint fallback_source_id;    /* timer di ripiego, 0 se assente */
};

G_DEFINE_FINAL_TYPE(SyncviewVideoPlayer, syncview_video_player, G_TYPE_OBJECT)

static void stop_ticker(SyncviewVideoPlayer *self);
static void on_tick_widget_gone(gpointer data, GObject *where_the_object_was);

/* --- Smontaggio della pipeline --- */

/*
 * Portare la pipeline a NULL mentre sta ancora salendo a PAUSED (cambio di stato asincrono in corso) può bloccare
 * il thread principale per sempre: set_state() aspetta i thread di streaming, e quelli — in particolare
 * gtk4paintablesink — possono aspettare il thread principale. Non si può spostare la chiamata in un thread di
 * lavoro: il sink è scritto in Rust e lega i suoi oggetti al thread che li ha creati (un accesso da un altro thread
 * manda in panic il processo). Soluzione: se la transizione è ancora in corso si ATTENDE che si assesti lasciando
 * girare il main loop (così il sink può fare ciò che gli serve) e solo dopo si scende a NULL, sempre sul thread
 * principale. Se è già assestata, si scende subito.
 */
#define TEARDOWN_POLL_INTERVAL_MS 10
#define TEARDOWN_SETTLE_TIMEOUT_US (10 * G_USEC_PER_SEC)

static gint pending_teardowns = 0;  /* smontaggi rimandati in corso, accesso atomico */

guint
syncview_video_player_pending_teardowns(void)
{
    return (guint)g_atomic_int_get(&pending_teardowns);
}

typedef void (*TeardownDone)(gpointer data);

typedef struct {
    GstElement *pipeline;   /* riferimento proprio */
    TeardownDone done;
    gpointer data;
    gint64 deadline_us;
} DeferredTeardown;

static gboolean
poll_until_settled(gpointer user_data)
{
    DeferredTeardown *t = user_data;
    GstState current, pending;
    GstStateChangeReturn ret = gst_element_get_state(t->pipeline, &current, &pending, 0);

    if (ret == GST_STATE_CHANGE_ASYNC && g_get_monotonic_time() < t->deadline_us) {
        return G_SOURCE_CONTINUE;  /* ancora in transizione: il main loop continua a girare */
    }
    if (ret == GST_STATE_CHANGE_ASYNC) {
        log_gst("smontaggio forzato: la pipeline non si è assestata entro %d s", (int)(TEARDOWN_SETTLE_TIMEOUT_US / G_USEC_PER_SEC));
    }

    gst_element_set_state(t->pipeline, GST_STATE_NULL);
    g_atomic_int_dec_and_test(&pending_teardowns);
    t->done(t->data);
    gst_object_unref(t->pipeline);
    g_free(t);
    return G_SOURCE_REMOVE;
}

/*
 * Porta la pipeline a NULL (sul thread principale) e poi chiama done(data). Se la pipeline è in un cambio di stato
 * asincrono aspetta che finisca, altrimenti lo fa subito e done() gira prima del ritorno.
 */
static void
teardown_pipeline(GstElement *pipeline, TeardownDone done, gpointer data)
{
    GstState current, pending;

    if (gst_element_get_state(pipeline, &current, &pending, 0) != GST_STATE_CHANGE_ASYNC) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        done(data);
        return;
    }

    DeferredTeardown *t = g_new0(DeferredTeardown, 1);

    t->pipeline = gst_object_ref(pipeline);
    t->done = done;
    t->data = data;
    t->deadline_us = g_get_monotonic_time() + TEARDOWN_SETTLE_TIMEOUT_US;
    g_atomic_int_inc(&pending_teardowns);
    g_timeout_add(TEARDOWN_POLL_INTERVAL_MS, poll_until_settled, t);
}

/* Oggetti ceduti da dispose() e rilasciati a smontaggio concluso, sul thread principale. */
typedef struct {
    GstElement *pipeline;
    GstElement *video_sink;
    GdkPaintable *paintable;
} DisposedPipeline;

static void
on_disposed_pipeline_stopped(gpointer user_data)
{
    DisposedPipeline *d = user_data;

    g_clear_object(&d->paintable);
    g_clear_pointer(&d->video_sink, gst_object_unref);
    g_clear_pointer(&d->pipeline, gst_object_unref);
    g_free(d);
}

static void
syncview_video_player_dispose(GObject *object)
{
    SyncviewVideoPlayer *self = SYNCVIEW_VIDEO_PLAYER(object);

    /* Prima di tutto si fermano ticker e bus: nessun callback deve vedere un player a metà distruzione. */
    stop_ticker(self);
    if (self->tick_widget) {
        g_object_weak_unref(G_OBJECT(self->tick_widget), on_tick_widget_gone, self);
        self->tick_widget = NULL;
    }
    g_clear_pointer(&self->frame_info, frame_info_unref);
    if (self->bus_watch_id) {
        g_source_remove(self->bus_watch_id);
        self->bus_watch_id = 0;
    }

    g_clear_pointer(&self->path, g_free);
    g_clear_pointer(&self->pending_uri, g_free);

    if (self->pipeline) {
        /*
         * La pipeline va sempre riportata a NULL prima del rilascio; se sta ancora salendo a PAUSED si attende che si
         * assesti (vedi teardown_pipeline). Pipeline, sink e paintable passano a un contesto che li rilascia, sul thread
         * principale, a smontaggio concluso: di solito subito, altrimenti appena finita la transizione.
         */
        DisposedPipeline *d = g_new0(DisposedPipeline, 1);

        d->pipeline = self->pipeline;
        d->video_sink = self->video_sink;
        d->paintable = self->paintable;
        self->pipeline = NULL;
        self->video_sink = NULL;
        self->paintable = NULL;
        teardown_pipeline(d->pipeline, on_disposed_pipeline_stopped, d);
    }

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
    signals[SIGNAL_POSITION_CHANGED] = g_signal_new(
        "position-changed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
        G_TYPE_NONE, 1, G_TYPE_INT64);
    signals[SIGNAL_DURATION_CHANGED] = g_signal_new(
        "duration-changed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
        G_TYPE_NONE, 1, G_TYPE_INT64);
}

static void
syncview_video_player_init(SyncviewVideoPlayer *self)
{
    self->video_index = 0;
    self->rate = 1.0;
    self->anchor_ns = -1;
    self->pending_seek_ns = -1;
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

    self->frame_info = frame_info_new();
    GstPad *sink_pad = gst_element_get_static_pad(self->video_sink, "sink");
    if (sink_pad) {
        /* Il probe ha il suo riferimento a frame_info, rilasciato quando il pad viene distrutto. */
        gst_pad_add_probe(sink_pad, GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
                          on_sink_pad_probe, g_rc_box_acquire(self->frame_info), frame_info_unref);
        gst_object_unref(sink_pad);
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

/* --- Posizione, durata e polling (M2.6) --- */

static void start_ticker(SyncviewVideoPlayer *self);

static gint64
query_position_ms(SyncviewVideoPlayer *self, gboolean *ok)
{
    gint64 position_ns = 0;

    /*
     * Dopo un frame-step esatto la posizione è l'inizio del frame mostrato (fine − durata nominale), non il punto di
     * arrivo del seek (che è il centro del frame): avanti e indietro danno così la stessa lettura, sui confini dei frame.
     */
    if (self->snap_to_frame && self->frame_info) {
        g_mutex_lock(&self->frame_info->lock);
        gint64 frame_ns = self->frame_info->fps_n > 0
                              ? (gint64)gst_util_uint64_scale(GST_SECOND, self->frame_info->fps_d, self->frame_info->fps_n)
                              : 0;
        gint64 pts_ns = self->frame_info->pts_ns;
        gint64 end_ns = self->frame_info->end_ns;
        g_mutex_unlock(&self->frame_info->lock);

        if (frame_ns > 0 && pts_ns >= 0 && end_ns > pts_ns) {
            gint64 start_ns = MAX(end_ns - frame_ns, 0);

            *ok = TRUE;
            return (start_ns + GST_MSECOND / 2) / GST_MSECOND;
        }
    }

    *ok = gst_element_query_position(self->pipeline, GST_FORMAT_TIME, &position_ns) && position_ns >= 0;
    /* GStreamer lavora in nanosecondi: conversione in ms una volta sola, qui. */
    return *ok ? (gint64)GST_TIME_AS_MSECONDS(position_ns) : 0;
}

static gint64
query_duration_ms(SyncviewVideoPlayer *self)
{
    gint64 duration_ns = 0;

    if (!gst_element_query_duration(self->pipeline, GST_FORMAT_TIME, &duration_ns) || duration_ns < 0) {
        return 0;
    }
    return (gint64)GST_TIME_AS_MSECONDS(duration_ns);
}

static void
emit_duration(SyncviewVideoPlayer *self, gint64 duration_ms)
{
    if (duration_ms == self->duration_ms) {
        return;
    }

    self->duration_ms = duration_ms;
    log_gst("player %d: durata %lldms", self->video_index + 1, (long long)duration_ms);
    g_object_ref(self);
    g_signal_emit(self, signals[SIGNAL_DURATION_CHANGED], 0, duration_ms);
    g_object_unref(self);
}

/* Emette "position-changed" se il valore (ms) è cambiato; con throttle limita la frequenza (durante la riproduzione). */
static void
emit_position(SyncviewVideoPlayer *self, gint64 position_ms, gboolean throttle)
{
    gint64 now = g_get_monotonic_time();

    if (position_ms == self->last_position_ms) {
        return;
    }
    if (throttle && now - self->last_position_emit_us < POSITION_MIN_INTERVAL_US) {
        return;
    }

    self->last_position_ms = position_ms;
    self->last_position_emit_us = now;
    g_object_ref(self);
    g_signal_emit(self, signals[SIGNAL_POSITION_CHANGED], 0, position_ms);
    g_object_unref(self);
}

/* Legge la posizione dalla pipeline e la pubblica (se cambiata). */
static void
publish_position(SyncviewVideoPlayer *self, gboolean throttle)
{
    gboolean ok;
    gint64 position_ms = query_position_ms(self, &ok);

    if (ok) {
        emit_position(self, position_ms, throttle);
    }
}

/* Il video è stato scartato (nuovo load, errore): durata e posizione tornano a 0, come QMediaPlayer::setSource. */
static void
reset_position_and_duration(SyncviewVideoPlayer *self)
{
    emit_position(self, 0, FALSE);
    emit_duration(self, 0);
    /* Velocità e frame-step non sopravvivono al video: ogni load riparte a 1.0x (come fa l'originale). */
    self->rate = 1.0;
    self->anchor_ns = -1;
    self->snap_to_frame = FALSE;
    self->pending_seek_ns = -1;
    if (self->frame_info) {
        frame_info_reset(self->frame_info);
    }
}

static gboolean
on_fallback_tick(gpointer user_data)
{
    SyncviewVideoPlayer *self = user_data;

    publish_position(self, TRUE);
    return G_SOURCE_CONTINUE;
}

static gboolean
on_widget_tick(GtkWidget *widget, GdkFrameClock *clock, gpointer user_data)
{
    (void)widget;
    (void)clock;
    publish_position(user_data, TRUE);
    return G_SOURCE_CONTINUE;
}

static void
stop_ticker(SyncviewVideoPlayer *self)
{
    if (self->tick_callback_id) {
        if (self->tick_widget) {
            gtk_widget_remove_tick_callback(self->tick_widget, self->tick_callback_id);
        }
        self->tick_callback_id = 0;
    }
    if (self->fallback_source_id) {
        g_source_remove(self->fallback_source_id);
        self->fallback_source_id = 0;
    }
}

/* Avvia il polling: frame clock del widget se c'è, altrimenti timer di ripiego. Idempotente. */
static void
start_ticker(SyncviewVideoPlayer *self)
{
    if (self->tick_callback_id || self->fallback_source_id) {
        return;
    }

    if (self->tick_widget) {
        self->tick_callback_id = gtk_widget_add_tick_callback(self->tick_widget, on_widget_tick, self, NULL);
    } else {
        self->fallback_source_id = g_timeout_add(POSITION_FALLBACK_INTERVAL_MS, on_fallback_tick, self);
    }
}

/*
 * Il widget del tick è stato distrutto (i suoi tick callback spariscono con lui): se si stava riproducendo
 * il polling prosegue con il timer di ripiego, così le posizioni non smettono di arrivare.
 */
static void
on_tick_widget_gone(gpointer data, GObject *where_the_object_was)
{
    SyncviewVideoPlayer *self = data;

    (void)where_the_object_was;
    self->tick_widget = NULL;
    self->tick_callback_id = 0;

    if (self->playback_state == SYNCVIEW_PLAYBACK_PLAYING) {
        start_ticker(self);
    }
}

void
syncview_video_player_set_tick_widget(SyncviewVideoPlayer *self, GtkWidget *widget)
{
    g_return_if_fail(SYNCVIEW_IS_VIDEO_PLAYER(self));
    g_return_if_fail(widget == NULL || GTK_IS_WIDGET(widget));

    if (widget == self->tick_widget) {
        return;
    }

    gboolean was_ticking = self->tick_callback_id || self->fallback_source_id;

    stop_ticker(self);
    if (self->tick_widget) {
        g_object_weak_unref(G_OBJECT(self->tick_widget), on_tick_widget_gone, self);
    }
    self->tick_widget = widget;
    if (widget) {
        g_object_weak_ref(G_OBJECT(widget), on_tick_widget_gone, self);
    }

    if (was_ticking || self->playback_state == SYNCVIEW_PLAYBACK_PLAYING) {
        start_ticker(self);
    }
}

gint64
syncview_video_player_get_position(SyncviewVideoPlayer *self)
{
    g_return_val_if_fail(SYNCVIEW_IS_VIDEO_PLAYER(self), 0);

    if (!self->loaded) {
        return 0;
    }

    gboolean ok;
    gint64 position_ms = query_position_ms(self, &ok);
    return ok ? position_ms : self->last_position_ms;
}

gint64
syncview_video_player_get_duration(SyncviewVideoPlayer *self)
{
    g_return_val_if_fail(SYNCVIEW_IS_VIDEO_PLAYER(self), 0);
    return self->loaded ? self->duration_ms : 0;
}

gboolean
syncview_video_player_is_ticking(SyncviewVideoPlayer *self)
{
    g_return_val_if_fail(SYNCVIEW_IS_VIDEO_PLAYER(self), FALSE);
    return self->tick_callback_id != 0 || self->fallback_source_id != 0;
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

    /* Il polling della posizione gira SOLO in PLAYING (O5): da fermo nessun risveglio periodico. */
    if (state == SYNCVIEW_PLAYBACK_PLAYING) {
        start_ticker(self);
    } else {
        stop_ticker(self);
    }

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

static gboolean pipeline_is_null(SyncviewVideoPlayer *self);

/* Avvia (se non è già in corso) lo smontaggio della pipeline di questo player. */
static void begin_teardown(SyncviewVideoPlayer *self);

/* Il video è perso per un errore: stato azzerato, pipeline in smontaggio e segnale "error" (vedi handle_error_message). */
static void
report_failure(SyncviewVideoPlayer *self, const char *text)
{
    gboolean was_loaded = self->loaded;

    self->loading = FALSE;
    self->loaded = FALSE;
    g_clear_pointer(&self->pending_uri, g_free);
    begin_teardown(self);

    self->at_end = FALSE;
    set_playback_state(self, SYNCVIEW_PLAYBACK_STOPPED);
    reset_position_and_duration(self);

    g_object_ref(self);  /* un handler potrebbe rilasciare l'ultimo riferimento */
    g_signal_emit(self, signals[SIGNAL_ERROR], 0, text);
    if (was_loaded) {
        g_signal_emit(self, signals[SIGNAL_LOAD_STATE_CHANGED], 0, FALSE);
    }
    g_object_unref(self);
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

    report_failure(self, text);

    g_free(text);
    g_free(debug);
    g_clear_error(&gst_error);
}

/*
 * Un seek/step si è concluso (ASYNC_DONE, STEP_DONE). Con più seek in volo i messaggi possono riferirsi a uno precedente:
 * la destinazione pendente si scarta solo se la posizione raggiunta la conferma (o è passato troppo tempo).
 */
#define SEEK_CONFIRM_TOLERANCE_NS (150 * GST_MSECOND)
#define SEEK_PENDING_MAX_US (2 * G_USEC_PER_SEC)

static void
settle_pending_seek(SyncviewVideoPlayer *self)
{
    if (self->pending_seek_ns < 0) {
        return;
    }

    gint64 position_ns = 0;
    gboolean reached = gst_element_query_position(self->pipeline, GST_FORMAT_TIME, &position_ns) &&
                       llabs(position_ns - self->pending_seek_ns) <= SEEK_CONFIRM_TOLERANCE_NS;
    if (reached || g_get_monotonic_time() - self->pending_seek_us > SEEK_PENDING_MAX_US) {
        self->pending_seek_ns = -1;
    }
}

static gboolean
on_bus_message(GstBus *bus, GstMessage *message, gpointer user_data)
{
    (void)bus;
    SyncviewVideoPlayer *self = user_data;
    gboolean from_pipeline = GST_MESSAGE_SRC(message) == GST_OBJECT(self->pipeline);

    /*
     * Pipeline in smontaggio (nuovo load() o errore): i messaggi del video vecchio non contano più. Senza questa
     * guardia un ASYNC_DONE già consegnato al main loop ma non ancora svuotato dal bus verrebbe scambiato per il
     * completamento del NUOVO caricamento (loading è già TRUE).
     */
    if (self->teardown_running) {
        return G_SOURCE_CONTINUE;
    }

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

            /* Come QMediaPlayer: durata e posizione note prima che il video risulti "caricato". */
            emit_duration(self, query_duration_ms(self));
            publish_position(self, FALSE);

            g_object_ref(self);
            g_signal_emit(self, signals[SIGNAL_LOAD_STATE_CHANGED], 0, TRUE);
            g_object_unref(self);
        } else if (from_pipeline && self->loaded) {
            /* Fine di un seek (stop(), seek/step di M2.7): la nuova posizione è quella da mostrare. */
            settle_pending_seek(self);
            publish_position(self, FALSE);
        }
        break;

    case GST_MESSAGE_STEP_DONE:
        /* Fine di un frame-step avanti: il frame mostrato è cambiato. */
        if (from_pipeline && self->loaded) {
            log_gst("player %d: STEP_DONE", self->video_index + 1);
            settle_pending_seek(self);
            publish_position(self, FALSE);
        }
        break;

    case GST_MESSAGE_DURATION_CHANGED:
        if (from_pipeline && self->loaded) {
            emit_duration(self, query_duration_ms(self));
        }
        break;

    case GST_MESSAGE_EOS:
        /* Fine del video (come QMediaPlayer::EndOfMedia -> Stopped): resta caricato e fermo sull'ultimo frame. */
        log_gst("player %d: EOS", self->video_index + 1);
        self->anchor_ns = -1;  /* un frame-step oltre l'ultimo frame non ha spostato nulla */
        self->pending_seek_ns = -1;
        if (from_pipeline && self->loaded && self->playback_state == SYNCVIEW_PLAYBACK_PLAYING) {
            gst_element_set_state(self->pipeline, GST_STATE_PAUSED);
            self->at_end = TRUE;
            set_playback_state(self, SYNCVIEW_PLAYBACK_STOPPED);
            publish_position(self, FALSE);  /* posizione finale */
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

static gboolean
pipeline_is_null(SyncviewVideoPlayer *self)
{
    GstState current, pending;

    gst_element_get_state(self->pipeline, &current, &pending, 0);
    return current == GST_STATE_NULL && pending == GST_STATE_VOID_PENDING;
}

/* Imposta l'URI in attesa e porta la pipeline (che deve essere in NULL) in PAUSED. */
static gboolean
start_pending_load(SyncviewVideoPlayer *self, GError **error)
{
    char *uri = self->pending_uri;

    self->pending_uri = NULL;
    g_object_set(self->pipeline, "uri", uri, NULL);
    log_gst("player %d: uri=%s -> PAUSED", self->video_index + 1, uri);

    if (gst_element_set_state(self->pipeline, GST_STATE_PAUSED) == GST_STATE_CHANGE_FAILURE) {
        g_set_error(error, SYNCVIEW_VIDEO_PLAYER_ERROR, SYNCVIEW_VIDEO_PLAYER_ERROR_PIPELINE,
                    "La pipeline non è riuscita a passare in PAUSED per %s", uri);
        gst_element_set_state(self->pipeline, GST_STATE_NULL);  /* appena avviata: niente preroll in corso, non può bloccarsi */
        g_free(uri);
        return FALSE;
    }

    self->loading = TRUE;
    g_free(uri);
    return TRUE;
}

/* Smontaggio concluso: se nel frattempo c'era un load() in attesa, parte adesso. `data` è un GWeakRef* al player. */
static void
on_reset_done(gpointer data)
{
    GWeakRef *weak = data;
    SyncviewVideoPlayer *self = g_weak_ref_get(weak);

    g_weak_ref_clear(weak);
    g_free(weak);
    if (!self) {
        return;  /* il player è stato distrutto: se ne occupa dispose() */
    }

    self->teardown_running = FALSE;
    if (self->pending_uri) {
        GError *error = NULL;

        if (!start_pending_load(self, &error)) {
            report_failure(self, error->message);
            g_error_free(error);
        }
    }
    g_object_unref(self);
}

static void
begin_teardown(SyncviewVideoPlayer *self)
{
    if (self->teardown_running) {
        return;
    }

    GWeakRef *weak = g_new0(GWeakRef, 1);

    g_weak_ref_init(weak, self);
    self->teardown_running = TRUE;
    teardown_pipeline(self->pipeline, on_reset_done, weak);  /* di norma conclude subito e on_reset_done() gira qui */
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

    /* Il video precedente (o in caricamento) è scartato: stato, posizione e durata tornano a zero. */
    gst_pipeline_set_auto_flush_bus(GST_PIPELINE(self->pipeline), TRUE);
    self->loaded = FALSE;
    self->loading = FALSE;
    self->at_end = FALSE;
    g_free(self->path);
    self->path = NULL;
    g_free(self->pending_uri);
    self->pending_uri = NULL;
    set_playback_state(self, SYNCVIEW_PLAYBACK_STOPPED);
    reset_position_and_duration(self);

    char *name = g_path_get_basename(path);
    log_video_action(self->video_index, "Caricamento avviato", name);
    g_free(name);

    self->path = g_strdup(path);
    self->pending_uri = uri;

    if (!self->teardown_running && pipeline_is_null(self)) {
        /* Pipeline mai avviata o già ferma: si parte subito. */
        if (!start_pending_load(self, error)) {
            g_clear_pointer(&self->path, g_free);
            return FALSE;
        }
        return TRUE;
    }

    /*
     * Pipeline in uso: va portata a NULL. Se è assestata succede subito e il nuovo video parte subito; se sta ancora
     * salendo a PAUSED si aspetta (senza bloccare il main loop) e il nuovo video parte da on_reset_done().
     * GstPipeline ha `auto-flush-bus`: scendendo a NULL il bus viene svuotato, quindi i messaggi del caricamento
     * vecchio sono scartati.
     */
    self->loading = TRUE;
    begin_teardown(self);
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
    /*
     * Già nello stato richiesto (e nessun cambio in corso): non si rimanda lo stesso set_state. Un cambio di stato
     * ridondante non è innocuo: su una pipeline già in PAUSED sposta la posizione riportata di 1 ms.
     */
    GstState current, pending;
    gst_element_get_state(self->pipeline, &current, &pending, 0);
    if (current == state && pending == GST_STATE_VOID_PENDING) {
        return TRUE;
    }

    if (gst_element_set_state(self->pipeline, state) == GST_STATE_CHANGE_FAILURE) {
        g_set_error(error, SYNCVIEW_VIDEO_PLAYER_ERROR, SYNCVIEW_VIDEO_PLAYER_ERROR_PIPELINE,
                    "La pipeline non è riuscita a passare in %s", gst_element_state_get_name(state));
        return FALSE;
    }
    return TRUE;
}

/* Seek accurato con flush alla velocità corrente (un seek_simple la riporterebbe a 1.0x). Invalida l'ancora dello step. */
static gboolean
seek_to_ns(SyncviewVideoPlayer *self, gint64 position_ns, GError **error)
{
    self->anchor_ns = -1;
    self->snap_to_frame = FALSE;
    self->pending_seek_ns = position_ns;
    self->pending_seek_us = g_get_monotonic_time();
    if (!gst_element_seek(self->pipeline, self->rate, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE,
                          GST_SEEK_TYPE_SET, position_ns, GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE)) {
        g_set_error(error, SYNCVIEW_VIDEO_PLAYER_ERROR, SYNCVIEW_VIDEO_PLAYER_ERROR_PIPELINE,
                    "Seek a %lld ms non riuscito", (long long)(position_ns / GST_MSECOND));
        self->pending_seek_ns = -1;
        return FALSE;
    }
    return TRUE;
}

/* Riporta il video all'inizio (seek con flush): primo frame visibile, nessun cambio di stato della pipeline. */
static gboolean
rewind_to_start(SyncviewVideoPlayer *self, GError **error)
{
    if (!seek_to_ns(self, 0, error)) {
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
    self->anchor_ns = -1;
    self->snap_to_frame = FALSE;
    self->pending_seek_ns = -1;  /* da qui la posizione è quella che scorre */

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
    publish_position(self, FALSE);  /* posizione finale della riproduzione */
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

/* --- Seek, velocità e frame-step (M2.7) --- */

static gint64
query_position_ns(SyncviewVideoPlayer *self)
{
    gint64 position_ns = 0;

    if (!gst_element_query_position(self->pipeline, GST_FORMAT_TIME, &position_ns) || position_ns < 0) {
        return 0;
    }
    return position_ns;
}

/* Posizione da cui partire per un movimento relativo: la destinazione di un seek ancora in corso, altrimenti quella letta. */
static gint64
current_position_ns(SyncviewVideoPlayer *self)
{
    return self->pending_seek_ns >= 0 ? self->pending_seek_ns : query_position_ns(self);
}

/* Porta `position_ns` dentro 0..durata (la durata si applica solo se nota). */
static gint64
clamp_to_duration_ns(SyncviewVideoPlayer *self, gint64 position_ns)
{
    if (position_ns < 0) {
        return 0;
    }
    gint64 limit = self->duration_ms * GST_MSECOND;
    return (limit > 0 && position_ns > limit) ? limit : position_ns;
}

gboolean
syncview_video_player_seek(SyncviewVideoPlayer *self, gint64 position_ms, GError **error)
{
    g_return_val_if_fail(SYNCVIEW_IS_VIDEO_PLAYER(self), FALSE);

    if (!require_loaded(self, error)) {
        return FALSE;
    }

    gint64 target_ns = clamp_to_duration_ns(self, position_ms * GST_MSECOND);
    if (!seek_to_ns(self, target_ns, error)) {
        return FALSE;
    }

    /* Da un video arrivato in fondo ci si è spostati: un play() successivo riprende da qui, non da 0. */
    self->at_end = FALSE;
    log_timeline_seek(self->video_index, target_ns / GST_MSECOND);
    return TRUE;
}

gboolean
syncview_video_player_step_ms(SyncviewVideoPlayer *self, gint64 delta_ms, GError **error)
{
    g_return_val_if_fail(SYNCVIEW_IS_VIDEO_PLAYER(self), FALSE);

    if (!require_loaded(self, error)) {
        return FALSE;
    }

    /* Come nell'originale, lo step mette in pausa se il video sta girando. */
    if (self->playback_state == SYNCVIEW_PLAYBACK_PLAYING && !syncview_video_player_pause(self, error)) {
        return FALSE;
    }

    gint64 target_ns = clamp_to_duration_ns(self, current_position_ns(self) + delta_ms * GST_MSECOND);
    if (!seek_to_ns(self, target_ns, error)) {
        return FALSE;
    }

    self->at_end = FALSE;
    char *details = g_strdup_printf("%+lld ms, posizione %lld ms", (long long)delta_ms,
                                    (long long)(target_ns / GST_MSECOND));
    log_video_action(self->video_index, "Step (ms)", details);
    g_free(details);
    return TRUE;
}

double
syncview_video_player_get_frame_rate(SyncviewVideoPlayer *self)
{
    g_return_val_if_fail(SYNCVIEW_IS_VIDEO_PLAYER(self), 0.0);

    double fps = 0.0;
    g_mutex_lock(&self->frame_info->lock);
    if (self->frame_info->fps_n > 0) {
        fps = (double)self->frame_info->fps_n / self->frame_info->fps_d;
    }
    g_mutex_unlock(&self->frame_info->lock);
    return fps;
}

gint64
syncview_video_player_get_frame_end_ns(SyncviewVideoPlayer *self)
{
    g_return_val_if_fail(SYNCVIEW_IS_VIDEO_PLAYER(self), -1);

    g_mutex_lock(&self->frame_info->lock);
    gint64 end = self->frame_info->end_ns >= 0 ? self->frame_info->end_ns : self->frame_info->pts_ns;
    g_mutex_unlock(&self->frame_info->lock);
    return end;
}

/*
 * Frame-step esatto (O1). Si ragiona sul PUNTO CENTRALE del frame mostrato (l'«ancora»): un seek accurato a un
 * punto qualsiasi dell'intervallo di un frame mostra quel frame, quindi mirare al centro tollera errori di arrotondamento
 * dei timestamp (±mezzo frame) che mirando al pts esatto farebbero cadere nel frame sbagliato. L'ancora si ricava dal pts
 * dell'ultimo buffer arrivato al sink e poi si fa avanzare di frame interi a ogni step, così passi ripetuti in fretta
 * (prima che il seek precedente sia concluso) non perdono il conto.
 */
gboolean
syncview_video_player_step_frames(SyncviewVideoPlayer *self, int frame_count, GError **error)
{
    g_return_val_if_fail(SYNCVIEW_IS_VIDEO_PLAYER(self), FALSE);

    if (!require_loaded(self, error)) {
        return FALSE;
    }
    if (frame_count == 0) {
        return TRUE;
    }

    double fps = syncview_video_player_get_frame_rate(self);
    if (fps <= 0.0) {
        /* Frame rate sconosciuto o variabile: ricade sullo step in ms dell'originale (40 ms = 25 fps). */
        log_gst("player %d: frame rate sconosciuto, step di %d x %d ms", self->video_index + 1, frame_count,
                SYNCVIEW_DEFAULT_FRAME_STEP_MS);
        return syncview_video_player_step_ms(self, (gint64)frame_count * SYNCVIEW_DEFAULT_FRAME_STEP_MS, error);
    }

    gboolean was_playing = self->playback_state == SYNCVIEW_PLAYBACK_PLAYING;
    if (was_playing) {
        if (!syncview_video_player_pause(self, error)) {
            return FALSE;
        }
        /* In riproduzione il frame mostrato era ancora in movimento: nessuna ancora o destinazione precedente vale più. */
        self->anchor_ns = -1;
        self->pending_seek_ns = -1;
    }

    g_mutex_lock(&self->frame_info->lock);
    gint64 frame_ns = gst_util_uint64_scale(GST_SECOND, self->frame_info->fps_d, self->frame_info->fps_n);
    gint64 pts_ns = self->frame_info->pts_ns;
    gint64 end_ns = self->frame_info->end_ns;
    g_mutex_unlock(&self->frame_info->lock);

    /*
     * Un seek/step «in corso» la cui destinazione cade già dentro il frame mostrato è concluso, anche se nessun messaggio
     * lo ha ancora confermato (su macOS l'ASYNC_DONE può non confermare la posizione): vale il frame, non la destinazione.
     * Un seek davvero in volo ha invece una destinazione fuori dal frame ancora mostrato (di almeno mezzo frame).
     */
    if (self->pending_seek_ns >= 0 && pts_ns >= 0 && end_ns > pts_ns &&
        self->pending_seek_ns >= end_ns - frame_ns - GST_MSECOND && self->pending_seek_ns <= end_ns + GST_MSECOND) {
        self->pending_seek_ns = -1;
    }
    /* Senza movimenti in volo l'ancora deve cadere nel frame mostrato; altrimenti è vecchia (il video si è mosso) e si ricalcola. */
    if (self->pending_seek_ns < 0 && self->anchor_ns >= 0 && pts_ns >= 0 && end_ns > pts_ns &&
        (self->anchor_ns < end_ns - frame_ns - GST_MSECOND || self->anchor_ns > end_ns + GST_MSECOND)) {
        self->anchor_ns = -1;
    }

    gint64 anchor = self->anchor_ns;
    if (anchor < 0) {
        if (end_ns > pts_ns && pts_ns >= 0) {
            anchor = end_ns - frame_ns / 2;  /* la fine del frame è esatta anche dopo un seek accurato */
        } else {
            anchor = (pts_ns >= 0 ? pts_ns : query_position_ns(self)) + frame_ns / 2;
        }
        if (self->pending_seek_ns >= 0) {
            /*
             * Un seek non ancora concluso: il frame mostrato è ancora il vecchio, conta la destinazione. Un seek
             * utente cade spesso esattamente sul confine di un frame, dove l'arrotondamento dei timestamp potrebbe
             * far finire uno step indietro nel frame sbagliato: 1 ms di margine lo evita (un frame dura >= 10 ms).
             */
            anchor = self->pending_seek_ns + GST_MSECOND;
        }
    }
    gboolean busy = self->pending_seek_ns >= 0;  /* un seek o uno step precedente non è ancora concluso */
    anchor += (gint64)frame_count * frame_ns;
    gint64 duration_ns = self->duration_ms * GST_MSECOND;
    if (duration_ns > 0 && anchor > duration_ns - frame_ns / 2) {
        anchor = duration_ns - frame_ns / 2;  /* ultimo frame */
    }
    if (anchor < frame_ns / 2) {
        anchor = frame_ns / 2;  /* primo frame */
    }

    if (frame_count > 0 && !busy) {
        /*
         * Via veloce: GST_EVENT_STEP, senza rifare la decodifica da un keyframe. Un nuovo step mentre ne è in corso un
         * altro LO SOSTITUISCE (i frame del primo andrebbero persi), quindi si usa solo a pipeline ferma.
         */
        GstEvent *step = gst_event_new_step(GST_FORMAT_BUFFERS, (guint64)frame_count, 1.0, TRUE, FALSE);
        if (!gst_element_send_event(self->pipeline, step)) {
            g_set_error_literal(error, SYNCVIEW_VIDEO_PLAYER_ERROR, SYNCVIEW_VIDEO_PLAYER_ERROR_PIPELINE,
                                "Frame-step non riuscito");
            return FALSE;
        }
        if (was_playing) {
            /* Partito da un frame in movimento: la destinazione stimata non è affidabile, si ripartirà dal frame mostrato. */
            self->pending_seek_ns = -1;
        } else {
            self->pending_seek_ns = anchor;
            self->pending_seek_us = g_get_monotonic_time();
        }
    } else if (!seek_to_ns(self, anchor, error)) {
        /* Indietro, oppure avanti con un movimento già in corso: seek accurato al frame di destinazione (stesso frame
         * che lo step avrebbe mostrato, e si somma correttamente ai passi ancora in volo). */
        return FALSE;
    } else {
        self->at_end = FALSE;
    }
    self->anchor_ns = (frame_count > 0 && !busy && was_playing) ? -1 : anchor;  /* dopo seek_to_ns, che la invalida */
    self->snap_to_frame = TRUE;

    char fps_text[G_ASCII_DTOSTR_BUF_SIZE];
    g_ascii_formatd(fps_text, sizeof(fps_text), "%.3f", fps);
    char *details = g_strdup_printf("%+d frame (esatto, %s fps)", frame_count, fps_text);
    log_video_action(self->video_index, "Step Frame", details);
    g_free(details);
    return TRUE;
}

gboolean
syncview_video_player_set_playback_rate(SyncviewVideoPlayer *self, double rate, GError **error)
{
    g_return_val_if_fail(SYNCVIEW_IS_VIDEO_PLAYER(self), FALSE);

    if (!(rate > 0.0) || !isfinite(rate)) {
        g_set_error(error, SYNCVIEW_VIDEO_PLAYER_ERROR, SYNCVIEW_VIDEO_PLAYER_ERROR_INVALID_ARGUMENT,
                    "Velocità non valida: %g (deve essere > 0)", rate);
        return FALSE;
    }
    if (!require_loaded(self, error)) {
        return FALSE;
    }
    if (rate == self->rate) {
        return TRUE;  /* un seek ridondante non è innocuo (vedi change_pipeline_state) */
    }

    /*
     * Seek accurato alla posizione corrente con la nuova velocità. Il cambio istantaneo senza flush
     * (GST_SEEK_FLAG_INSTANT_RATE_CHANGE) sarebbe più fluido, ma con matroskademux provoca un CRITICAL di GStreamer
     * (gst_segment_position_from_running_time_full): provato e scartato.
     */
    gint64 position_ns = self->anchor_ns >= 0 ? self->anchor_ns : current_position_ns(self);
    gint64 anchor = self->anchor_ns;
    gboolean snap = self->snap_to_frame;
    double previous = self->rate;
    self->rate = rate;
    if (!seek_to_ns(self, position_ns, error)) {
        self->rate = previous;
        return FALSE;
    }
    self->anchor_ns = anchor;  /* stesso frame: l'ancora e la lettura sul frame restano valide */
    self->snap_to_frame = snap;

    char rate_text[G_ASCII_DTOSTR_BUF_SIZE];
    g_ascii_formatd(rate_text, sizeof(rate_text), "%.2f", rate);
    char *details = g_strdup_printf("%sx", rate_text);
    log_video_action(self->video_index, "Velocità", details);
    g_free(details);
    return TRUE;
}

double
syncview_video_player_get_playback_rate(SyncviewVideoPlayer *self)
{
    g_return_val_if_fail(SYNCVIEW_IS_VIDEO_PLAYER(self), 1.0);
    return self->rate;
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
