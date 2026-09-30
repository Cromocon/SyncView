#include "video/video_player.h"

#include "core/logger.h"
#include "core/settings.h"

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

/* --- Posizione, durata e polling (M2.6) --- */

static void start_ticker(SyncviewVideoPlayer *self);

static gint64
query_position_ms(SyncviewVideoPlayer *self, gboolean *ok)
{
    gint64 position_ns = 0;

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
            /* Fine di un seek (stop(), e i seek di M2.7): la nuova posizione è quella da mostrare. */
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
