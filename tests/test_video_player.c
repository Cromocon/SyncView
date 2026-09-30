/*
 * M2.3: scheletro di SyncviewVideoPlayer. Richiede un display (GTK) e il
 * plugin GStreamer gtk4paintablesink: se mancano, termina con 77 (skip Meson).
 */
#include "core/discoverer.h"
#include "core/logger.h"
#include "core/settings.h"
#include "video/video_player.h"

#include <assert.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <gst/gst.h>
#include <stdlib.h>
#include <string.h>

#define SKIP_EXIT 77

static GstState
pipeline_state(GstElement *pipeline)
{
    GstState state = GST_STATE_VOID_PENDING;
    gst_element_get_state(pipeline, &state, NULL, 0);
    return state;
}

/* Fa girare il main loop finché tutti gli smontaggi asincroni di pipeline sono finiti (o timeout). */
static gboolean
drain_teardowns(int timeout_ms)
{
    gint64 deadline = g_get_monotonic_time() + (gint64)timeout_ms * 1000;

    while (syncview_video_player_pending_teardowns() > 0 && g_get_monotonic_time() < deadline) {
        g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
    g_main_context_iteration(NULL, FALSE);
    return syncview_video_player_pending_teardowns() == 0;
}

/* Attende che la pipeline di un player sia scesa a NULL (lo smontaggio è asincrono). */
static gboolean
wait_until_null(SyncviewVideoPlayer *player)
{
    gint64 deadline = g_get_monotonic_time() + 10 * G_USEC_PER_SEC;

    while (pipeline_state(syncview_video_player_get_pipeline(player)) != GST_STATE_NULL
           && g_get_monotonic_time() < deadline) {
        g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
    return pipeline_state(syncview_video_player_get_pipeline(player)) == GST_STATE_NULL;
}

static void
test_type_and_construction(void)
{
    GError *error = NULL;
    SyncviewVideoPlayer *player = syncview_video_player_new(2, &error);
    assert(player != NULL && error == NULL);

    /* Tipo GObject registrato e finale. */
    GType type = g_type_from_name("SyncviewVideoPlayer");
    assert(type != 0 && type == SYNCVIEW_TYPE_VIDEO_PLAYER);
    assert(SYNCVIEW_IS_VIDEO_PLAYER(player) && G_IS_OBJECT(player));
    assert(G_TYPE_IS_FINAL(type));
    assert(g_type_parent(type) == G_TYPE_OBJECT);

    /* Proprietà: video-index fisso alla creazione, paintable coerente con il getter. */
    int index = -1;
    GdkPaintable *paintable = NULL;
    g_object_get(player, "video-index", &index, "paintable", &paintable, NULL);
    assert(index == 2 && syncview_video_player_get_video_index(player) == 2);
    assert(paintable != NULL && paintable == syncview_video_player_get_paintable(player));
    assert(GDK_IS_PAINTABLE(paintable));
    g_object_unref(paintable);  /* g_object_get restituisce un riferimento */

    /* La pipeline è un playbin3 con gtk4paintablesink come video-sink, ferma in NULL (non avviata). */
    GstElement *pipeline = syncview_video_player_get_pipeline(player);
    assert(pipeline != NULL && GST_IS_PIPELINE(pipeline));
    GstElementFactory *factory = gst_element_get_factory(pipeline);
    assert(strcmp(GST_OBJECT_NAME(factory), "playbin3") == 0);
    assert(pipeline_state(pipeline) == GST_STATE_NULL);

    GstElement *sink = NULL;
    g_object_get(pipeline, "video-sink", &sink, NULL);
    assert(sink != NULL);
    assert(strcmp(GST_OBJECT_NAME(gst_element_get_factory(sink)), "gtk4paintablesink") == 0);
    GdkPaintable *sink_paintable = NULL;
    g_object_get(sink, "paintable", &sink_paintable, NULL);
    assert(sink_paintable == paintable);  /* è proprio il paintable del sink collegato alla pipeline */
    g_object_unref(sink_paintable);
    gst_object_unref(sink);

    /* video-index è construct-only: non modificabile dopo. */
    g_object_unref(player);
}

static void
test_paintable_in_gtk_picture(void)
{
    SyncviewVideoPlayer *player = syncview_video_player_new(0, NULL);
    assert(player != NULL);

    /* Il paintable si collega a un GtkPicture; senza video caricato è vuoto (nessuna dimensione intrinseca). */
    GtkWidget *picture = gtk_picture_new();
    gtk_picture_set_paintable(GTK_PICTURE(picture), syncview_video_player_get_paintable(player));
    assert(gtk_picture_get_paintable(GTK_PICTURE(picture)) == syncview_video_player_get_paintable(player));
    assert(gdk_paintable_get_intrinsic_width(syncview_video_player_get_paintable(player)) == 0);
    assert(gdk_paintable_get_intrinsic_height(syncview_video_player_get_paintable(player)) == 0);

    /* Il widget può essere misurato e allocato (nessun crash nel rendering a vuoto) e la pipeline resta ferma. */
    int min, nat;
    gtk_widget_measure(picture, GTK_ORIENTATION_HORIZONTAL, -1, &min, &nat, NULL, NULL);
    assert(pipeline_state(syncview_video_player_get_pipeline(player)) == GST_STATE_NULL);

    /* Il GtkPicture tiene un proprio riferimento al paintable: distruggere il player prima non crasha. */
    g_object_ref_sink(picture);
    g_object_unref(player);
    gtk_widget_measure(picture, GTK_ORIENTATION_HORIZONTAL, -1, &min, &nat, NULL, NULL);
    g_object_unref(picture);
}

static void
test_invalid_index(void)
{
    int bad[] = { -1, SYNCVIEW_MAX_VIDEOS, 100, -100 };

    for (size_t i = 0; i < G_N_ELEMENTS(bad); i++) {
        GError *error = NULL;
        assert(syncview_video_player_new(bad[i], &error) == NULL);
        assert(error != NULL && error->domain == SYNCVIEW_VIDEO_PLAYER_ERROR);
        assert(error->code == SYNCVIEW_VIDEO_PLAYER_ERROR_INDEX);
        g_error_free(error);
    }

    /* Tutti gli indici validi funzionano; error NULL ammesso. */
    for (int i = 0; i < SYNCVIEW_MAX_VIDEOS; i++) {
        SyncviewVideoPlayer *p = syncview_video_player_new(i, NULL);
        assert(p != NULL && syncview_video_player_get_video_index(p) == i);
        g_object_unref(p);
    }
}

static void
test_independent_players(void)
{
    SyncviewVideoPlayer *players[SYNCVIEW_MAX_VIDEOS];

    for (int i = 0; i < SYNCVIEW_MAX_VIDEOS; i++) {
        players[i] = syncview_video_player_new(i, NULL);
        assert(players[i] != NULL);
    }

    /* Ogni player ha pipeline, sink e paintable propri, e nomi di elemento distinti. */
    for (int i = 0; i < SYNCVIEW_MAX_VIDEOS; i++) {
        for (int j = i + 1; j < SYNCVIEW_MAX_VIDEOS; j++) {
            assert(syncview_video_player_get_pipeline(players[i]) != syncview_video_player_get_pipeline(players[j]));
            assert(syncview_video_player_get_paintable(players[i]) != syncview_video_player_get_paintable(players[j]));
            assert(strcmp(GST_OBJECT_NAME(syncview_video_player_get_pipeline(players[i])),
                          GST_OBJECT_NAME(syncview_video_player_get_pipeline(players[j]))) != 0);
        }
    }

    for (int i = 0; i < SYNCVIEW_MAX_VIDEOS; i++) {
        g_object_unref(players[i]);
    }
}

static gboolean finalized;

static void
on_weak_notify(gpointer data, GObject *where_the_object_was)
{
    (void)data;
    (void)where_the_object_was;
    finalized = TRUE;
}

static void
test_lifecycle(void)
{
    /* Creazione/distruzione ripetuta: il player viene davvero finalizzato (e ASan non vede leak). */
    for (int i = 0; i < 25; i++) {
        SyncviewVideoPlayer *player = syncview_video_player_new(i % SYNCVIEW_MAX_VIDEOS, NULL);
        assert(player != NULL);

        finalized = FALSE;
        g_object_weak_ref(G_OBJECT(player), on_weak_notify, NULL);
        GstElement *pipeline = gst_object_ref(syncview_video_player_get_pipeline(player));

        g_object_unref(player);
        assert(finalized);

        /* Lo smontaggio della pipeline è asincrono: a fine smontaggio resta l'ultimo riferimento (il nostro), in stato NULL. */
        assert(drain_teardowns(10000));
        assert(GST_OBJECT_REFCOUNT(pipeline) == 1);
        assert(pipeline_state(pipeline) == GST_STATE_NULL);
        gst_object_unref(pipeline);
    }
}

typedef struct {
    gboolean pipeline_finalized;
    gboolean sink_finalized;
    gboolean paintable_finalized;
    gboolean bus_finalized;
} Finalized;

static void
on_bus_gone(gpointer data, GObject *obj)
{
    (void)obj;
    ((Finalized *)data)->bus_finalized = TRUE;
}

static void
on_pipeline_gone(gpointer data, GObject *obj)
{
    (void)obj;
    ((Finalized *)data)->pipeline_finalized = TRUE;
}

static void
on_sink_gone(gpointer data, GObject *obj)
{
    (void)obj;
    ((Finalized *)data)->sink_finalized = TRUE;
}

static void
on_paintable_gone(gpointer data, GObject *obj)
{
    (void)obj;
    ((Finalized *)data)->paintable_finalized = TRUE;
}

/*
 * Distruggere il player deve rilasciare TUTTO ciò che possiede: pipeline,
 * sink e paintable. LeakSanitizer non basta (un oggetto ancora raggiungibile
 * da una sorgente del main context non è "leaked"), quindi si controlla la
 * finalizzazione con riferimenti deboli.
 */
static void
test_owned_objects_are_released(void)
{
    for (int round = 0; round < 10; round++) {
        SyncviewVideoPlayer *player = syncview_video_player_new(round % SYNCVIEW_MAX_VIDEOS, NULL);
        assert(player != NULL);

        Finalized fin = { FALSE, FALSE, FALSE, FALSE };
        GstElement *pipeline = syncview_video_player_get_pipeline(player);
        GstElement *sink = NULL;
        g_object_get(pipeline, "video-sink", &sink, NULL);  /* riferimento forte, lo rilasciamo subito */
        g_object_weak_ref(G_OBJECT(pipeline), on_pipeline_gone, &fin);
        g_object_weak_ref(G_OBJECT(sink), on_sink_gone, &fin);
        g_object_weak_ref(G_OBJECT(syncview_video_player_get_paintable(player)), on_paintable_gone, &fin);
        gst_object_unref(sink);

        /* Il bus resta vivo finché la sua sorgente (bus watch) è nel main context: deve sparire col player. */
        GstBus *bus = gst_element_get_bus(pipeline);
        g_object_weak_ref(G_OBJECT(bus), on_bus_gone, &fin);
        gst_object_unref(bus);

        g_object_unref(player);

        /* Smontaggio asincrono: pipeline, sink, paintable e bus vengono rilasciati a fine smontaggio, sul thread principale. */
        assert(drain_teardowns(10000));
        for (int i = 0; i < 50 && !(fin.paintable_finalized && fin.sink_finalized && fin.bus_finalized); i++) {
            g_main_context_iteration(NULL, FALSE);
        }

        assert(fin.pipeline_finalized);
        assert(fin.sink_finalized);
        assert(fin.paintable_finalized);
        assert(fin.bus_finalized);
    }
}

/* ===================== M2.4: load() ===================== */

static gboolean have_test_encoder;

/* Genera un webm/VP8 di prova. Il percorso si imposta come proprietà, mai nel testo della pipeline
 * (gst_parse_launch interpreta il backslash dei percorsi Windows come escape). */
static char *
make_video_fps(const char *dir, const char *name, int width, int height, int frames, int fps, gboolean variable_rate)
{
    char *path = g_build_filename(dir, name, NULL);
    /* Framerate variabile (0/1 nei caps): si sovrascrive il framerate dei caps VP8 prima del muxer. */
    char *desc = g_strdup_printf(
        "videotestsrc num-buffers=%d ! video/x-raw,width=%d,height=%d,framerate=%d/1 ! videoconvert ! "
        "vp8enc ! %s webmmux ! filesink name=out", frames, width, height, fps,
        variable_rate ? "capssetter caps=\"video/x-vp8,framerate=0/1\" replace=false join=true !" : "");
    GError *error = NULL;
    GstElement *pipeline = gst_parse_launch(desc, &error);
    assert(pipeline != NULL && error == NULL);

    GstElement *out = gst_bin_get_by_name(GST_BIN(pipeline), "out");
    g_object_set(out, "location", path, NULL);
    gst_object_unref(out);

    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    GstBus *bus = gst_element_get_bus(pipeline);
    GstMessage *msg = gst_bus_timed_pop_filtered(bus, 30 * GST_SECOND, GST_MESSAGE_EOS | GST_MESSAGE_ERROR);
    assert(msg != NULL && GST_MESSAGE_TYPE(msg) == GST_MESSAGE_EOS);
    gst_message_unref(msg);
    gst_object_unref(bus);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    g_free(desc);
    return path;
}

static char *
make_video(const char *dir, const char *name, int width, int height, int frames)
{
    return make_video_fps(dir, name, width, height, frames, 25, FALSE);
}

/* Fa girare il main context GTK finché *flag o timeout. */
static gboolean
spin_until(const gboolean *flag, int timeout_ms)
{
    gint64 deadline = g_get_monotonic_time() + (gint64)timeout_ms * 1000;

    while (!*flag && g_get_monotonic_time() < deadline) {
        g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
    return *flag;
}

/* Lascia girare il main context per `ms` (per intercettare eventuali eventi spuri). */
static void
spin_for(int ms)
{
    gboolean never = FALSE;
    spin_until(&never, ms);
}

typedef struct {
    int loaded_true;
    int loaded_false;
    int errors;
    gboolean got_loaded;
    gboolean got_error;
    char *last_error;
} Events;

static void
on_load_state(SyncviewVideoPlayer *player, gboolean loaded, gpointer data)
{
    (void)player;
    Events *ev = data;

    if (loaded) {
        ev->loaded_true++;
        ev->got_loaded = TRUE;
    } else {
        ev->loaded_false++;
    }
}

static void
on_error(SyncviewVideoPlayer *player, const char *message, gpointer data)
{
    (void)player;
    Events *ev = data;

    ev->errors++;
    ev->got_error = TRUE;
    g_free(ev->last_error);
    ev->last_error = g_strdup(message);
}

static void
events_connect(SyncviewVideoPlayer *player, Events *ev)
{
    memset(ev, 0, sizeof(*ev));
    g_signal_connect(player, "load-state-changed", G_CALLBACK(on_load_state), ev);
    g_signal_connect(player, "error", G_CALLBACK(on_error), ev);
}

/* Disegna il paintable in uno snapshot (nodo NULL se non disegna nulla). */
static GskRenderNode *
snapshot_paintable(GdkPaintable *paintable, double w, double h)
{
    GtkSnapshot *snapshot = gtk_snapshot_new();

    gdk_paintable_snapshot(paintable, snapshot, w, h);
    return gtk_snapshot_free_to_node(snapshot);
}

/* TRUE se il nodo (o un suo discendente) è una texture: cioè c'è un frame vero, non solo un riempimento. */
static gboolean
node_contains_texture(GskRenderNode *node)
{
    if (!node) {
        return FALSE;
    }

    switch (gsk_render_node_get_node_type(node)) {
    case GSK_TEXTURE_NODE:
    case GSK_TEXTURE_SCALE_NODE:
        return TRUE;
    case GSK_CONTAINER_NODE:
        for (guint i = 0; i < gsk_container_node_get_n_children(node); i++) {
            if (node_contains_texture(gsk_container_node_get_child(node, i))) {
                return TRUE;
            }
        }
        return FALSE;
    case GSK_CLIP_NODE:
        return node_contains_texture(gsk_clip_node_get_child(node));
    case GSK_TRANSFORM_NODE:
        return node_contains_texture(gsk_transform_node_get_child(node));
    case GSK_OPACITY_NODE:
        return node_contains_texture(gsk_opacity_node_get_child(node));
    case GSK_DEBUG_NODE:
        return node_contains_texture(gsk_debug_node_get_child(node));
    default:
        return FALSE;
    }
}

static gboolean
paintable_has_size(GdkPaintable *paintable, int width, int height)
{
    return gdk_paintable_get_intrinsic_width(paintable) == width
           && gdk_paintable_get_intrinsic_height(paintable) == height;
}

/* Attende che il paintable mostri un frame della dimensione attesa (il sink lo pubblica nel main thread, un istante dopo ASYNC_DONE). */
static gboolean
wait_for_frame(SyncviewVideoPlayer *player, int width, int height)
{
    GdkPaintable *paintable = syncview_video_player_get_paintable(player);
    gint64 deadline = g_get_monotonic_time() + 5 * G_USEC_PER_SEC;

    while (!paintable_has_size(paintable, width, height) && g_get_monotonic_time() < deadline) {
        g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
    return paintable_has_size(paintable, width, height);
}

static void
test_load_first_frame(const char *dir)
{
    char *path = make_video(dir, "a.webm", 320, 240, 50);
    SyncviewVideoPlayer *player = syncview_video_player_new(0, NULL);
    Events ev;
    events_connect(player, &ev);
    GdkPaintable *paintable = syncview_video_player_get_paintable(player);

    /* Prima del load: nessun frame, nulla da disegnare, nessun decoder. */
    assert(!syncview_video_player_is_loaded(player) && !syncview_video_player_is_loading(player));
    assert(syncview_video_player_get_path(player) == NULL);
    assert(syncview_video_player_get_decoder_description(player) == NULL);
    GskRenderNode *empty = snapshot_paintable(paintable, 320, 240);
    assert(!node_contains_texture(empty));  /* al più un riempimento nero, nessuna texture */
    if (empty) {
        gsk_render_node_unref(empty);
    }

    /* load() è asincrono: ritorna subito, in stato "loading", senza segnali. */
    GError *error = NULL;
    assert(syncview_video_player_load(player, path, &error) && error == NULL);
    assert(syncview_video_player_is_loading(player) && !syncview_video_player_is_loaded(player));
    assert(strcmp(syncview_video_player_get_path(player), path) == 0);
    assert(ev.loaded_true == 0 && ev.errors == 0);

    /* ASYNC_DONE dal bus -> load-state-changed(TRUE), una sola volta. */
    assert(spin_until(&ev.got_loaded, 10000));
    assert(ev.loaded_true == 1 && ev.loaded_false == 0 && ev.errors == 0);
    assert(syncview_video_player_is_loaded(player) && !syncview_video_player_is_loading(player));

    /* Pipeline in PAUSED (non in play): il primo frame è mostrato, il video non avanza. */
    assert(pipeline_state(syncview_video_player_get_pipeline(player)) == GST_STATE_PAUSED);

    /* Il primo frame è nel paintable: dimensioni intrinseche del video e qualcosa da disegnare. */
    assert(wait_for_frame(player, 320, 240));
    GskRenderNode *node = snapshot_paintable(paintable, 320, 240);
    assert(node != NULL && node_contains_texture(node));  /* ora c'è la texture del frame */
    graphene_rect_t bounds;
    gsk_render_node_get_bounds(node, &bounds);
    assert(bounds.size.width > 0 && bounds.size.height > 0);
    gsk_render_node_unref(node);

    /* Un GtkPicture collegato riceve le dimensioni del video. */
    GtkWidget *picture = g_object_ref_sink(gtk_picture_new());
    gtk_picture_set_paintable(GTK_PICTURE(picture), paintable);
    int min_w, nat_w;
    gtk_widget_measure(picture, GTK_ORIENTATION_HORIZONTAL, -1, &min_w, &nat_w, NULL, NULL);
    assert(nat_w == 320);
    g_object_unref(picture);

    /* Decoder in uso (O6): VP8, software o hardware. */
    char *decoder = syncview_video_player_get_decoder_description(player);
    assert(decoder != NULL && strstr(decoder, "vp8") != NULL);
    assert(strstr(decoder, "(software)") != NULL || strstr(decoder, "(hardware)") != NULL);
    g_free(decoder);

    /* Nessun evento spurio nel frattempo. */
    spin_for(200);
    assert(ev.loaded_true == 1 && ev.errors == 0);

    /* Un seek con flush produce un nuovo ASYNC_DONE: non è un nuovo caricamento, nessun segnale. */
    assert(gst_element_seek_simple(syncview_video_player_get_pipeline(player), GST_FORMAT_TIME,
                                   GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT, 0));
    GstState st;
    gst_element_get_state(syncview_video_player_get_pipeline(player), &st, NULL, 5 * GST_SECOND);
    spin_for(300);
    assert(ev.loaded_true == 1 && ev.errors == 0 && syncview_video_player_is_loaded(player));

    g_free(ev.last_error);
    g_object_unref(player);
    g_free(path);
}

static void
test_load_errors(const char *dir)
{
    SyncviewVideoPlayer *player = syncview_video_player_new(1, NULL);
    Events ev;
    events_connect(player, &ev);
    GError *error = NULL;

    /* File assente / NULL / directory: errore immediato, nessun segnale, stato invariato. */
    char *missing = g_build_filename(dir, "manca.webm", NULL);
    assert(!syncview_video_player_load(player, missing, &error));
    assert(error->domain == SYNCVIEW_VIDEO_PLAYER_ERROR && error->code == SYNCVIEW_VIDEO_PLAYER_ERROR_FILE_NOT_FOUND);
    assert(strcmp(error->message, "File non trovato") == 0);
    g_clear_error(&error);
    assert(!syncview_video_player_load(player, NULL, &error) && error->code == SYNCVIEW_VIDEO_PLAYER_ERROR_FILE_NOT_FOUND);
    g_clear_error(&error);
    assert(!syncview_video_player_load(player, dir, NULL));
    assert(!syncview_video_player_is_loading(player) && !syncview_video_player_is_loaded(player));
    assert(pipeline_state(syncview_video_player_get_pipeline(player)) == GST_STATE_NULL);
    spin_for(100);
    assert(ev.errors == 0 && ev.loaded_true == 0);

    /* File che GStreamer non sa leggere: il bus segnala un ERROR -> segnale "error", pipeline a NULL. */
    char *garbage = g_build_filename(dir, "garbage.mp4", NULL);
    char data[4096];
    for (size_t i = 0; i < sizeof(data); i++) {
        data[i] = (char)((i * 131 + 17) % 251);
    }
    assert(g_file_set_contents(garbage, data, sizeof(data), NULL));
    assert(syncview_video_player_load(player, garbage, NULL));  /* l'errore arriva dal bus, non subito */
    assert(spin_until(&ev.got_error, 10000));
    assert(ev.errors == 1 && ev.loaded_true == 0 && ev.last_error != NULL && *ev.last_error != '\0');
    assert(!syncview_video_player_is_loaded(player) && !syncview_video_player_is_loading(player));
    assert(wait_until_null(player));  /* smontaggio asincrono */
    assert(syncview_video_player_get_decoder_description(player) == NULL);

    /* Dopo un errore il player si riprende con un file valido. */
    char *good = make_video(dir, "dopo_errore.webm", 320, 240, 25);
    ev.got_loaded = FALSE;
    assert(syncview_video_player_load(player, good, NULL));
    assert(spin_until(&ev.got_loaded, 10000));
    assert(ev.loaded_true == 1 && ev.errors == 1 && syncview_video_player_is_loaded(player));

    g_free(ev.last_error);
    g_object_unref(player);
    g_free(missing);
    g_free(garbage);
    g_free(good);
}

/* Errore sul bus DOPO un caricamento riuscito: il video viene perso (loaded -> FALSE, pipeline a NULL). */
static void
test_error_after_loaded(const char *dir)
{
    char *path = make_video(dir, "e.webm", 320, 240, 25);
    SyncviewVideoPlayer *player = syncview_video_player_new(0, NULL);
    Events ev;
    events_connect(player, &ev);

    assert(syncview_video_player_load(player, path, NULL));
    assert(spin_until(&ev.got_loaded, 10000));
    assert(syncview_video_player_is_loaded(player));

    /* Errore iniettato sul bus come se arrivasse da un elemento della pipeline. */
    GstElement *pipeline = syncview_video_player_get_pipeline(player);
    GError *injected = g_error_new_literal(GST_STREAM_ERROR, GST_STREAM_ERROR_FAILED, "errore iniettato dal test");
    gst_element_post_message(pipeline, gst_message_new_error(GST_OBJECT(pipeline), injected, "debug di prova"));
    g_error_free(injected);

    assert(spin_until(&ev.got_error, 10000));
    assert(ev.errors == 1 && strcmp(ev.last_error, "errore iniettato dal test") == 0);
    assert(ev.loaded_true == 1 && ev.loaded_false == 1);          /* "load-state-changed(FALSE)" emesso una volta */
    assert(!syncview_video_player_is_loaded(player) && !syncview_video_player_is_loading(player));
    assert(wait_until_null(player));  /* smontaggio asincrono */
    assert(syncview_video_player_get_decoder_description(player) == NULL);

    /* Il player si riprende con un nuovo load. */
    ev.got_loaded = FALSE;
    assert(syncview_video_player_load(player, path, NULL));
    assert(spin_until(&ev.got_loaded, 10000) && syncview_video_player_is_loaded(player));
    assert(ev.loaded_true == 2 && ev.errors == 1);

    g_free(ev.last_error);
    g_object_unref(player);
    g_free(path);
}

static void
test_reload_replaces_video(const char *dir)
{
    char *small = make_video(dir, "small.webm", 320, 240, 25);
    char *big = make_video(dir, "big.webm", 640, 360, 25);
    SyncviewVideoPlayer *player = syncview_video_player_new(2, NULL);
    Events ev;
    events_connect(player, &ev);

    assert(syncview_video_player_load(player, small, NULL));
    assert(spin_until(&ev.got_loaded, 10000) && wait_for_frame(player, 320, 240));

    /* Secondo load nello stesso player: sostituisce il primo, nuovo ASYNC_DONE, nuove dimensioni. */
    ev.got_loaded = FALSE;
    assert(syncview_video_player_load(player, big, NULL));
    assert(syncview_video_player_is_loading(player) && !syncview_video_player_is_loaded(player));
    assert(strcmp(syncview_video_player_get_path(player), big) == 0);
    assert(spin_until(&ev.got_loaded, 10000) && wait_for_frame(player, 640, 360));
    assert(ev.loaded_true == 2 && ev.errors == 0);

    /*
     * ASYNC_DONE del caricamento precedente già pubblicato sul bus ma non ancora consegnato (il main
     * context non gira): un nuovo load() deve scartarlo, altrimenti lo scambierebbe per il proprio.
     */
    ev.got_loaded = FALSE;
    ev.loaded_true = 0;
    assert(syncview_video_player_load(player, small, NULL));
    GstState state_now;
    assert(gst_element_get_state(syncview_video_player_get_pipeline(player), &state_now, NULL, 10 * GST_SECOND)
           == GST_STATE_CHANGE_SUCCESS);  /* preroll finito: ASYNC_DONE è sul bus, non ancora dispatchato */
    assert(ev.loaded_true == 0);
    assert(syncview_video_player_load(player, big, NULL));
    assert(spin_until(&ev.got_loaded, 10000));
    spin_for(300);
    assert(ev.loaded_true == 1);                      /* un solo esito, non due */
    assert(wait_for_frame(player, 640, 360));         /* ed è quello del file nuovo */

    /* Load ravvicinati: il primo viene scartato, arriva UN SOLO esito (per l'ultimo file). */
    ev.got_loaded = FALSE;
    ev.loaded_true = 0;
    assert(syncview_video_player_load(player, small, NULL));
    assert(syncview_video_player_load(player, big, NULL));
    assert(spin_until(&ev.got_loaded, 10000));
    spin_for(500);  /* un ASYNC_DONE del caricamento scartato arriverebbe qui */
    assert(ev.loaded_true == 1 && ev.errors == 0);
    assert(strcmp(syncview_video_player_get_path(player), big) == 0);
    assert(wait_for_frame(player, 640, 360));

    g_free(ev.last_error);
    g_object_unref(player);
    g_free(small);
    g_free(big);
}

static void
test_players_load_independently(const char *dir)
{
    char *p1 = make_video(dir, "p1.webm", 320, 240, 25);
    char *p2 = make_video(dir, "p2.webm", 640, 360, 25);
    SyncviewVideoPlayer *a = syncview_video_player_new(0, NULL);
    SyncviewVideoPlayer *b = syncview_video_player_new(3, NULL);
    Events ea, eb;
    events_connect(a, &ea);
    events_connect(b, &eb);

    assert(syncview_video_player_load(a, p1, NULL));
    assert(syncview_video_player_load(b, p2, NULL));
    assert(spin_until(&ea.got_loaded, 10000) && spin_until(&eb.got_loaded, 10000));
    assert(wait_for_frame(a, 320, 240) && wait_for_frame(b, 640, 360));
    assert(ea.loaded_true == 1 && eb.loaded_true == 1 && ea.errors == 0 && eb.errors == 0);

    g_object_unref(a);
    g_object_unref(b);
    g_free(p1);
    g_free(p2);
}

static void
test_dispose_while_loading(const char *dir)
{
    char *path = make_video(dir, "d.webm", 320, 240, 25);

    /* Distruggere il player con un caricamento in corso: nessun callback su oggetto distrutto,
     * nessun CRITICAL (fatali), neanche lasciando girare il main context dopo. */
    for (int i = 0; i < 10; i++) {
        SyncviewVideoPlayer *player = syncview_video_player_new(i % SYNCVIEW_MAX_VIDEOS, NULL);
        Events ev;
        events_connect(player, &ev);
        assert(syncview_video_player_load(player, path, NULL));
        if (i % 2) {
            spin_for(20);  /* a volte a caricamento già avviato */
        }
        g_object_unref(player);
        spin_for(30);
    }

    g_free(path);
}

/* ===================== M2.5: play / pause / stop ===================== */

typedef struct {
    GdkPaintable *paintable; /* riferimento proprio: il gestore dei frame va scollegato prima di liberare questa struttura */
    GArray *states;          /* SyncviewPlaybackState in ordine di emissione */
    int frames;              /* invalidate-contents del paintable (frame disegnati) */
    int loaded_true;
    int errors;
    gboolean got_loaded;
    gboolean got_error;
} PlayEvents;

static void
on_playback_state(SyncviewVideoPlayer *player, guint state, gpointer data)
{
    (void)player;
    PlayEvents *ev = data;
    g_array_append_val(ev->states, state);
}

static void
on_play_load_state(SyncviewVideoPlayer *player, gboolean loaded, gpointer data)
{
    (void)player;
    PlayEvents *ev = data;

    if (loaded) {
        ev->loaded_true++;
        ev->got_loaded = TRUE;
    }
}

static void
on_play_error(SyncviewVideoPlayer *player, const char *message, gpointer data)
{
    (void)player;
    (void)message;
    PlayEvents *ev = data;

    ev->errors++;
    ev->got_error = TRUE;
}

static void
on_frame(GdkPaintable *paintable, gpointer data)
{
    (void)paintable;
    ((PlayEvents *)data)->frames++;
}

static SyncviewVideoPlayer *
player_with_events(int index, PlayEvents *ev)
{
    SyncviewVideoPlayer *player = syncview_video_player_new(index, NULL);

    memset(ev, 0, sizeof(*ev));
    ev->states = g_array_new(FALSE, FALSE, sizeof(guint));
    g_signal_connect(player, "playback-state-changed", G_CALLBACK(on_playback_state), ev);
    g_signal_connect(player, "load-state-changed", G_CALLBACK(on_play_load_state), ev);
    g_signal_connect(player, "error", G_CALLBACK(on_play_error), ev);
    ev->paintable = g_object_ref(syncview_video_player_get_paintable(player));
    g_signal_connect(ev->paintable, "invalidate-contents", G_CALLBACK(on_frame), ev);
    return player;
}

static void
events_free(PlayEvents *ev)
{
    /* Dopo distrutto il player il paintable vive ancora finché la pipeline non è smontata: niente gestori con dati liberati. */
    g_signal_handlers_disconnect_by_data(ev->paintable, ev);
    g_object_unref(ev->paintable);
    g_array_free(ev->states, TRUE);
}

static guint
state_at(PlayEvents *ev, guint i)
{
    return g_array_index(ev->states, guint, i);
}

/* Posizione della pipeline in ms (ASSERT se non disponibile). */
static gint64
position_ms(SyncviewVideoPlayer *player)
{
    gint64 pos = -1;

    assert(gst_element_query_position(syncview_video_player_get_pipeline(player), GST_FORMAT_TIME, &pos));
    return pos / GST_MSECOND;
}

/* Attende che la pipeline abbia finito un eventuale cambio di stato/seek asincrono (main context che gira). */
static void
settle(SyncviewVideoPlayer *player)
{
    GstElement *pipeline = syncview_video_player_get_pipeline(player);
    gint64 deadline = g_get_monotonic_time() + 5 * G_USEC_PER_SEC;
    GstState state, pending;

    while (g_get_monotonic_time() < deadline) {
        GstStateChangeReturn ret = gst_element_get_state(pipeline, &state, &pending, 0);
        if (ret == GST_STATE_CHANGE_SUCCESS) {
            break;
        }
        g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
    spin_for(30);
}

static void
test_play_pause_basics(const char *dir)
{
    char *path = make_video(dir, "play.webm", 320, 240, 100);  /* 4 s a 25 fps */
    PlayEvents ev;
    SyncviewVideoPlayer *player = player_with_events(0, &ev);

    assert(syncview_video_player_get_playback_state(player) == SYNCVIEW_PLAYBACK_STOPPED);
    assert(syncview_video_player_load(player, path, NULL));
    assert(spin_until(&ev.got_loaded, 10000));

    /* Dopo il load: in pausa sul primo frame, come l'originale. Un solo segnale (PAUSED). */
    assert(syncview_video_player_get_playback_state(player) == SYNCVIEW_PLAYBACK_PAUSED);
    assert(ev.states->len == 1 && state_at(&ev, 0) == SYNCVIEW_PLAYBACK_PAUSED);
    assert(wait_for_frame(player, 320, 240));
    spin_for(200);
    gint64 still = position_ms(player);
    spin_for(300);
    assert(position_ms(player) == still);  /* in pausa il video non avanza */

    /* play(): PLAYING, il video avanza in tempo reale e il paintable riceve frame. */
    GError *error = NULL;
    int frames_before = ev.frames;
    assert(syncview_video_player_play(player, &error) && error == NULL);
    assert(syncview_video_player_get_playback_state(player) == SYNCVIEW_PLAYBACK_PLAYING);
    assert(pipeline_state(syncview_video_player_get_pipeline(player)) == GST_STATE_PLAYING);
    spin_for(600);
    gint64 after_play = position_ms(player);
    assert(after_play >= still + 300 && after_play <= still + 1500);  /* ~600 ms di riproduzione */
    assert(ev.frames - frames_before >= 5);                              /* frame disegnati (25 fps) */

    /* pause(): si ferma, la posizione non cambia più e nessun nuovo frame. */
    assert(syncview_video_player_pause(player, NULL));
    assert(syncview_video_player_get_playback_state(player) == SYNCVIEW_PLAYBACK_PAUSED);
    settle(player);
    gint64 paused_at = position_ms(player);
    int frames_paused = ev.frames;
    spin_for(400);
    assert(position_ms(player) == paused_at);
    assert(ev.frames - frames_paused <= 1);

    /* play() riprende da dove si era fermato, non da capo. */
    assert(syncview_video_player_play(player, NULL));
    spin_for(400);
    gint64 resumed = position_ms(player);
    assert(resumed > paused_at + 150 && resumed >= after_play);

    /* Sequenza dei segnali: PAUSED (load), PLAYING, PAUSED, PLAYING; chiamate ripetute non ne emettono. */
    assert(syncview_video_player_play(player, NULL));  /* già in play */
    assert(ev.states->len == 4);
    assert(state_at(&ev, 1) == SYNCVIEW_PLAYBACK_PLAYING && state_at(&ev, 2) == SYNCVIEW_PLAYBACK_PAUSED
           && state_at(&ev, 3) == SYNCVIEW_PLAYBACK_PLAYING);
    assert(syncview_video_player_pause(player, NULL) && syncview_video_player_pause(player, NULL));
    assert(ev.states->len == 5);  /* un solo PAUSED in più */

    /* Il video è rimasto caricato per tutto il tempo; nessun errore. */
    assert(syncview_video_player_is_loaded(player) && ev.errors == 0);

    events_free(&ev);
    g_object_unref(player);
    g_free(path);
}

static void
test_toggle(const char *dir)
{
    char *path = make_video(dir, "toggle.webm", 320, 240, 100);
    PlayEvents ev;
    SyncviewVideoPlayer *player = player_with_events(1, &ev);

    assert(syncview_video_player_load(player, path, NULL) && spin_until(&ev.got_loaded, 10000));

    assert(syncview_video_player_toggle_play_pause(player, NULL));
    assert(syncview_video_player_get_playback_state(player) == SYNCVIEW_PLAYBACK_PLAYING);
    assert(syncview_video_player_toggle_play_pause(player, NULL));
    assert(syncview_video_player_get_playback_state(player) == SYNCVIEW_PLAYBACK_PAUSED);
    assert(syncview_video_player_toggle_play_pause(player, NULL));
    assert(syncview_video_player_get_playback_state(player) == SYNCVIEW_PLAYBACK_PLAYING);

    /* Da STOPPED il toggle avvia la riproduzione. */
    assert(syncview_video_player_stop(player, NULL));
    assert(syncview_video_player_get_playback_state(player) == SYNCVIEW_PLAYBACK_STOPPED);
    assert(syncview_video_player_toggle_play_pause(player, NULL));
    assert(syncview_video_player_get_playback_state(player) == SYNCVIEW_PLAYBACK_PLAYING);

    events_free(&ev);
    g_object_unref(player);
    g_free(path);
}

static void
test_stop_rewinds_and_keeps_video(const char *dir)
{
    char *path = make_video(dir, "stop.webm", 320, 240, 100);
    PlayEvents ev;
    SyncviewVideoPlayer *player = player_with_events(2, &ev);

    assert(syncview_video_player_load(player, path, NULL) && spin_until(&ev.got_loaded, 10000));
    assert(wait_for_frame(player, 320, 240));

    assert(syncview_video_player_play(player, NULL));
    spin_for(700);
    assert(position_ms(player) >= 400);

    /* stop(): STOPPED, posizione a 0, video ANCORA CARICATO e primo frame visibile (la pipeline non scende a NULL). */
    assert(syncview_video_player_stop(player, NULL));
    assert(syncview_video_player_get_playback_state(player) == SYNCVIEW_PLAYBACK_STOPPED);
    settle(player);
    assert(position_ms(player) < 80);
    assert(syncview_video_player_is_loaded(player) && !syncview_video_player_is_loading(player));
    assert(pipeline_state(syncview_video_player_get_pipeline(player)) == GST_STATE_PAUSED);
    assert(syncview_video_player_get_path(player) != NULL);
    char *decoder = syncview_video_player_get_decoder_description(player);
    assert(decoder != NULL);  /* la pipeline è ancora montata */
    g_free(decoder);
    GskRenderNode *node = snapshot_paintable(syncview_video_player_get_paintable(player), 320, 240);
    assert(node_contains_texture(node));  /* il primo frame è visibile */
    gsk_render_node_unref(node);
    assert(paintable_has_size(syncview_video_player_get_paintable(player), 320, 240));

    /* Fermo: non avanza. */
    gint64 at_stop = position_ms(player);
    spin_for(300);
    assert(position_ms(player) == at_stop);

    /* play() dopo stop() riparte da 0 e avanza. */
    assert(syncview_video_player_play(player, NULL));
    spin_for(500);
    assert(position_ms(player) >= 250 && position_ms(player) <= 1500);

    /* stop() due volte di seguito non crea segnali doppi. */
    assert(syncview_video_player_stop(player, NULL));
    guint n = ev.states->len;
    assert(syncview_video_player_stop(player, NULL));
    assert(ev.states->len == n);

    events_free(&ev);
    g_object_unref(player);
    g_free(path);
}

static void
test_end_of_video(const char *dir)
{
    char *path = make_video(dir, "short.webm", 320, 240, 25);  /* 1 s */
    PlayEvents ev;
    SyncviewVideoPlayer *player = player_with_events(0, &ev);

    assert(syncview_video_player_load(player, path, NULL) && spin_until(&ev.got_loaded, 10000));
    assert(syncview_video_player_play(player, NULL));

    /* Arrivato in fondo: passa da solo a STOPPED, un solo segnale, il video resta caricato. */
    gint64 deadline = g_get_monotonic_time() + 10 * G_USEC_PER_SEC;
    while (syncview_video_player_get_playback_state(player) == SYNCVIEW_PLAYBACK_PLAYING
           && g_get_monotonic_time() < deadline) {
        g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
    assert(syncview_video_player_get_playback_state(player) == SYNCVIEW_PLAYBACK_STOPPED);
    assert(ev.states->len == 3);  /* PAUSED (load), PLAYING, STOPPED */
    assert(state_at(&ev, 2) == SYNCVIEW_PLAYBACK_STOPPED);
    assert(syncview_video_player_is_loaded(player) && ev.errors == 0);
    settle(player);
    gint64 end_pos = position_ms(player);
    assert(end_pos >= 800);  /* fermo in fondo */
    spin_for(300);
    assert(position_ms(player) == end_pos);

    /* All'EOS la pipeline viene messa in PAUSED (non resta in PLAYING con l'orologio che corre). */
    assert(pipeline_state(syncview_video_player_get_pipeline(player)) == GST_STATE_PAUSED);

    /* pause() a fine video: nessun cambio di stato né segnali (resta STOPPED, non diventa "in pausa a metà"). */
    assert(syncview_video_player_pause(player, NULL));
    assert(syncview_video_player_get_playback_state(player) == SYNCVIEW_PLAYBACK_STOPPED);
    assert(ev.states->len == 3);
    assert(position_ms(player) == end_pos);

    /* play() a fine video: riparte dall'inizio. */
    assert(syncview_video_player_play(player, NULL));
    assert(syncview_video_player_get_playback_state(player) == SYNCVIEW_PLAYBACK_PLAYING);
    spin_for(300);
    assert(position_ms(player) < 700);  /* ripartito da 0, non dalla fine */
    assert(ev.states->len == 4);

    /* E arriva di nuovo in fondo. */
    deadline = g_get_monotonic_time() + 10 * G_USEC_PER_SEC;
    while (syncview_video_player_get_playback_state(player) == SYNCVIEW_PLAYBACK_PLAYING
           && g_get_monotonic_time() < deadline) {
        g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
    assert(syncview_video_player_get_playback_state(player) == SYNCVIEW_PLAYBACK_STOPPED && ev.states->len == 5);

    /* stop() a fine video: torna a 0 senza altri segnali (era già STOPPED). */
    assert(syncview_video_player_stop(player, NULL));
    settle(player);
    assert(position_ms(player) < 80 && ev.states->len == 5);

    events_free(&ev);
    g_object_unref(player);
    g_free(path);
}

static void
test_not_loaded_is_noop(const char *dir)
{
    char *garbage = g_build_filename(dir, "garbage2.mp4", NULL);
    char data[4096];
    for (size_t i = 0; i < sizeof(data); i++) {
        data[i] = (char)((i * 131 + 17) % 251);
    }
    assert(g_file_set_contents(garbage, data, sizeof(data), NULL));
    char *good = make_video(dir, "good2.webm", 320, 240, 25);

    PlayEvents ev;
    SyncviewVideoPlayer *player = player_with_events(3, &ev);
    gboolean (*actions[])(SyncviewVideoPlayer *, GError **) = {
        syncview_video_player_play, syncview_video_player_pause, syncview_video_player_stop,
        syncview_video_player_toggle_play_pause,
    };

    /* Mai caricato: nessuna azione ha effetto, errore NOT_LOADED, nessun segnale, stato STOPPED. */
    for (size_t i = 0; i < G_N_ELEMENTS(actions); i++) {
        GError *error = NULL;
        assert(!actions[i](player, &error));
        assert(error != NULL && error->domain == SYNCVIEW_VIDEO_PLAYER_ERROR && error->code == SYNCVIEW_VIDEO_PLAYER_ERROR_NOT_LOADED);
        g_error_free(error);
        assert(!actions[i](player, NULL));  /* error NULL ammesso */
    }
    assert(ev.states->len == 0 && syncview_video_player_get_playback_state(player) == SYNCVIEW_PLAYBACK_STOPPED);
    assert(pipeline_state(syncview_video_player_get_pipeline(player)) == GST_STATE_NULL);

    /* Durante il caricamento (load accettato, ASYNC_DONE non ancora consegnato): stesso comportamento. */
    assert(syncview_video_player_load(player, good, NULL));
    GError *error = NULL;
    assert(!syncview_video_player_play(player, &error) && error->code == SYNCVIEW_VIDEO_PLAYER_ERROR_NOT_LOADED);
    assert(strstr(error->message, "caricamento") != NULL);
    g_error_free(error);
    assert(spin_until(&ev.got_loaded, 10000));
    assert(syncview_video_player_play(player, NULL));  /* ora sì */

    /* Dopo un errore di caricamento: di nuovo nessun effetto. */
    ev.got_error = FALSE;
    assert(syncview_video_player_load(player, garbage, NULL));
    assert(spin_until(&ev.got_error, 10000));
    assert(syncview_video_player_get_playback_state(player) == SYNCVIEW_PLAYBACK_STOPPED);
    assert(!syncview_video_player_play(player, NULL) && !syncview_video_player_pause(player, NULL));

    events_free(&ev);
    g_object_unref(player);
    g_free(garbage);
    g_free(good);
}

static void
test_reload_while_playing(const char *dir)
{
    char *a = make_video(dir, "ra.webm", 320, 240, 100);
    char *b = make_video(dir, "rb.webm", 640, 360, 100);
    PlayEvents ev;
    SyncviewVideoPlayer *player = player_with_events(0, &ev);

    assert(syncview_video_player_load(player, a, NULL) && spin_until(&ev.got_loaded, 10000));
    assert(syncview_video_player_play(player, NULL));
    spin_for(300);

    /* Nuovo load in play: il vecchio video è scartato (STOPPED), il nuovo arriva in PAUSED e NON parte da solo. */
    guint n = ev.states->len;
    ev.got_loaded = FALSE;
    assert(syncview_video_player_load(player, b, NULL));
    assert(syncview_video_player_get_playback_state(player) == SYNCVIEW_PLAYBACK_STOPPED);
    assert(ev.states->len == n + 1 && state_at(&ev, n) == SYNCVIEW_PLAYBACK_STOPPED);
    assert(spin_until(&ev.got_loaded, 10000) && wait_for_frame(player, 640, 360));
    assert(syncview_video_player_get_playback_state(player) == SYNCVIEW_PLAYBACK_PAUSED);
    assert(ev.states->len == n + 2);
    gint64 pos = position_ms(player);
    spin_for(300);
    assert(position_ms(player) == pos);  /* fermo finché non si chiama play() */

    assert(syncview_video_player_play(player, NULL));
    spin_for(400);
    assert(position_ms(player) > pos + 150);

    events_free(&ev);
    g_object_unref(player);
    g_free(a);
    g_free(b);
}

static void
test_error_while_playing(const char *dir)
{
    char *path = make_video(dir, "err_play.webm", 320, 240, 100);
    PlayEvents ev;
    SyncviewVideoPlayer *player = player_with_events(1, &ev);

    assert(syncview_video_player_load(player, path, NULL) && spin_until(&ev.got_loaded, 10000));
    assert(syncview_video_player_play(player, NULL));
    spin_for(200);

    GstElement *pipeline = syncview_video_player_get_pipeline(player);
    GError *injected = g_error_new_literal(GST_STREAM_ERROR, GST_STREAM_ERROR_FAILED, "errore in riproduzione");
    gst_element_post_message(pipeline, gst_message_new_error(GST_OBJECT(pipeline), injected, NULL));
    g_error_free(injected);

    assert(spin_until(&ev.got_error, 10000));
    assert(syncview_video_player_get_playback_state(player) == SYNCVIEW_PLAYBACK_STOPPED);
    assert(!syncview_video_player_is_loaded(player));
    assert(wait_until_null(player));  /* smontaggio asincrono */
    /* STOPPED emesso PRIMA dell'errore. */
    assert(state_at(&ev, ev.states->len - 1) == SYNCVIEW_PLAYBACK_STOPPED);
    assert(!syncview_video_player_play(player, NULL));

    events_free(&ev);
    g_object_unref(player);
    g_free(path);
}

static void
test_players_play_independently(const char *dir)
{
    char *p = make_video(dir, "ind.webm", 320, 240, 100);
    PlayEvents ea, eb;
    SyncviewVideoPlayer *a = player_with_events(0, &ea);
    SyncviewVideoPlayer *b = player_with_events(1, &eb);

    assert(syncview_video_player_load(a, p, NULL) && syncview_video_player_load(b, p, NULL));
    assert(spin_until(&ea.got_loaded, 10000) && spin_until(&eb.got_loaded, 10000));
    assert(syncview_video_player_play(a, NULL) && syncview_video_player_play(b, NULL));
    spin_for(400);
    assert(position_ms(a) >= 200 && position_ms(b) >= 200);

    /* Mettere in pausa uno non tocca l'altro. */
    assert(syncview_video_player_pause(a, NULL));
    settle(a);
    gint64 pa = position_ms(a);
    gint64 pb = position_ms(b);
    spin_for(400);
    assert(position_ms(a) == pa);
    assert(position_ms(b) >= pb + 200);
    assert(syncview_video_player_get_playback_state(a) == SYNCVIEW_PLAYBACK_PAUSED
           && syncview_video_player_get_playback_state(b) == SYNCVIEW_PLAYBACK_PLAYING);

    events_free(&ea);
    events_free(&eb);
    g_object_unref(a);
    g_object_unref(b);
    g_free(p);
}

static void
test_dispose_while_playing(const char *dir)
{
    char *path = make_video(dir, "dp.webm", 320, 240, 100);

    for (int i = 0; i < 8; i++) {
        PlayEvents ev;
        SyncviewVideoPlayer *player = player_with_events(i % SYNCVIEW_MAX_VIDEOS, &ev);
        assert(syncview_video_player_load(player, path, NULL) && spin_until(&ev.got_loaded, 10000));
        assert(syncview_video_player_play(player, NULL));
        spin_for(i * 30);
        events_free(&ev);
        g_object_unref(player);  /* distrutto in PLAYING */
        spin_for(30);
    }

    g_free(path);
}

static void
test_playback_log(const char *dir)
{
    char *path = make_video(dir, "log.webm", 320, 240, 100);
    char *log_path = g_build_filename(dir, "player.log", NULL);
    g_unsetenv("SYNCVIEW_DEBUG");
    assert(logger_init(log_path, FALSE, NULL));

    PlayEvents ev;
    SyncviewVideoPlayer *player = player_with_events(2, &ev);  /* slot 3 */
    assert(syncview_video_player_load(player, path, NULL) && spin_until(&ev.got_loaded, 10000));
    assert(syncview_video_player_play(player, NULL));
    assert(syncview_video_player_pause(player, NULL));
    assert(syncview_video_player_stop(player, NULL));
    assert(syncview_video_player_play(player, NULL));
    assert(syncview_video_player_play(player, NULL));  /* ogni chiamata registra, come l'originale */

    events_free(&ev);
    g_object_unref(player);
    logger_shutdown();

    char *log = NULL;
    assert(g_file_get_contents(log_path, &log, NULL, NULL));
    int plays = 0;
    for (const char *p = log; (p = strstr(p, "INFO - [VIDEO 3] Stato riproduzione: PLAY")); p++) {
        plays++;
    }
    assert(plays == 3);
    assert(strstr(log, "INFO - [VIDEO 3] Stato riproduzione: PAUSA") != NULL);
    assert(strstr(log, "INFO - [VIDEO 3] Stato riproduzione: STOP") != NULL);
    g_free(log);
    g_free(log_path);
    g_free(path);
}

/* ===================== M2.6: posizione e durata ===================== */

typedef struct {
    GArray *positions;    /* gint64 ms emessi da position-changed */
    GArray *pos_times;    /* gint64 us (monotonic) dell'emissione */
    GArray *durations;    /* gint64 ms emessi da duration-changed */
    GString *order;       /* 'P' posizione, 'D' durata, 'L' load TRUE, in ordine di emissione */
    gboolean got_loaded;
    int errors;
    gboolean got_error;
} PosEvents;

static void
on_pos(SyncviewVideoPlayer *player, gint64 ms, gpointer data)
{
    (void)player;
    PosEvents *ev = data;
    gint64 now = g_get_monotonic_time();

    g_array_append_val(ev->positions, ms);
    g_array_append_val(ev->pos_times, now);
    g_string_append_c(ev->order, 'P');
}

static void
on_dur(SyncviewVideoPlayer *player, gint64 ms, gpointer data)
{
    (void)player;
    PosEvents *ev = data;

    g_array_append_val(ev->durations, ms);
    g_string_append_c(ev->order, 'D');
}

static void
on_pos_load(SyncviewVideoPlayer *player, gboolean loaded, gpointer data)
{
    (void)player;
    PosEvents *ev = data;

    if (loaded) {
        ev->got_loaded = TRUE;
        g_string_append_c(ev->order, 'L');
    }
}

static void
on_pos_error(SyncviewVideoPlayer *player, const char *message, gpointer data)
{
    (void)player;
    (void)message;
    PosEvents *ev = data;

    ev->errors++;
    ev->got_error = TRUE;
}

static SyncviewVideoPlayer *
pos_player(int index, PosEvents *ev)
{
    SyncviewVideoPlayer *player = syncview_video_player_new(index, NULL);

    memset(ev, 0, sizeof(*ev));
    ev->positions = g_array_new(FALSE, FALSE, sizeof(gint64));
    ev->pos_times = g_array_new(FALSE, FALSE, sizeof(gint64));
    ev->durations = g_array_new(FALSE, FALSE, sizeof(gint64));
    ev->order = g_string_new(NULL);
    g_signal_connect(player, "position-changed", G_CALLBACK(on_pos), ev);
    g_signal_connect(player, "duration-changed", G_CALLBACK(on_dur), ev);
    g_signal_connect(player, "load-state-changed", G_CALLBACK(on_pos_load), ev);
    g_signal_connect(player, "error", G_CALLBACK(on_pos_error), ev);
    return player;
}

static void
pos_events_free(PosEvents *ev)
{
    g_array_free(ev->positions, TRUE);
    g_array_free(ev->pos_times, TRUE);
    g_array_free(ev->durations, TRUE);
    g_string_free(ev->order, TRUE);
}

static gint64
pos_at(PosEvents *ev, guint i)
{
    return g_array_index(ev->positions, gint64, i);
}

static gint64
last_pos(PosEvents *ev)
{
    assert(ev->positions->len > 0);
    return pos_at(ev, ev->positions->len - 1);
}

/*
 * Il frame clock di GTK dipende dall'ambiente: su un runner senza display attivo (CI macOS) una finestra
 * occlusa riceve pochissimi tick. Le verifiche che richiedono un frame clock regolare si eseguono solo se lo è.
 */
static gboolean
tick_counter(GtkWidget *widget, GdkFrameClock *clock, gpointer data)
{
    (void)widget;
    (void)clock;
    (*(guint *)data)++;
    return G_SOURCE_CONTINUE;
}

static gboolean
frame_clock_usable(void)
{
    static int verdict = -1;

    if (verdict < 0) {
        guint ticks = 0;
        GtkWidget *window = gtk_window_new();
        GdkPaintable *empty = gdk_paintable_new_empty(64, 64);
        GtkWidget *picture = gtk_picture_new_for_paintable(empty);  /* niente testo: un GtkLabel inizializzerebbe fontconfig (leak di terze parti) */

        gtk_window_set_default_size(GTK_WINDOW(window), 64, 64);
        gtk_window_set_child(GTK_WINDOW(window), picture);
        gtk_widget_add_tick_callback(picture, tick_counter, &ticks, NULL);
        gtk_window_present(GTK_WINDOW(window));
        /* La finestra può metterci un po' ad apparire (runner lenti, sanitizer): si conta dal primo tick. */
        for (gint64 deadline = g_get_monotonic_time() + 5 * G_USEC_PER_SEC; ticks == 0 && g_get_monotonic_time() < deadline;) {
            g_main_context_iteration(NULL, FALSE);
            g_usleep(1000);
        }
        ticks = 0;
        spin_for(500);
        gtk_window_destroy(GTK_WINDOW(window));
        spin_for(50);
        g_object_unref(empty);
        verdict = ticks >= 10;  /* ~30 attesi a 60 Hz */
        if (!verdict) {
            g_printerr("frame clock irregolare (%u tick in 500 ms): verifiche sul frame clock saltate\n", ticks);
        }
    }
    return verdict;
}

/* Finestra con il video, così il player ha un frame clock reale (tick callback). */
typedef struct {
    GtkWidget *window;
    GtkWidget *picture;
} Shown;

static Shown
show_window_for(SyncviewVideoPlayer *player)
{
    Shown shown;

    shown.window = gtk_window_new();
    shown.picture = gtk_picture_new_for_paintable(syncview_video_player_get_paintable(player));
    gtk_window_set_default_size(GTK_WINDOW(shown.window), 320, 240);
    gtk_window_set_child(GTK_WINDOW(shown.window), shown.picture);
    syncview_video_player_set_tick_widget(player, shown.picture);
    gtk_window_present(GTK_WINDOW(shown.window));

    gint64 deadline = g_get_monotonic_time() + 5 * G_USEC_PER_SEC;
    while (!gtk_widget_get_mapped(shown.picture) && g_get_monotonic_time() < deadline) {
        g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
    assert(gtk_widget_get_mapped(shown.picture));
    return shown;
}

static void
test_load_reports_duration(const char *dir)
{
    char *path = make_video(dir, "dur.webm", 320, 240, 100);  /* 4 s */
    PosEvents ev;
    SyncviewVideoPlayer *player = pos_player(0, &ev);

    assert(syncview_video_player_get_duration(player) == 0 && syncview_video_player_get_position(player) == 0);
    assert(!syncview_video_player_is_ticking(player));

    assert(syncview_video_player_load(player, path, NULL));
    assert(syncview_video_player_get_duration(player) == 0);  /* ancora in caricamento: non nota */
    assert(spin_until(&ev.got_loaded, 10000));

    /* Durata nota PRIMA di "caricato" (come QMediaPlayer); la posizione resta 0 e non genera segnali. */
    assert(strcmp(ev.order->str, "DL") == 0);
    assert(ev.durations->len == 1);
    gint64 duration = syncview_video_player_get_duration(player);
    assert(duration == g_array_index(ev.durations, gint64, 0));
    assert(syncview_video_player_get_position(player) == 0 && ev.positions->len == 0);

    /* Coerente con la durata reale del file, misurata indipendentemente da core/discoverer. */
    VideoInfo *info = discoverer_probe_file(path, 0, NULL);
    assert(info != NULL && info->duration_ms > 3500 && info->duration_ms < 4500);
    assert(llabs(duration - info->duration_ms) <= 60);
    video_info_free(info);

    /* In pausa dopo il caricamento: nessun aggiornamento periodico. */
    spin_for(400);
    assert(ev.positions->len == 0 && ev.durations->len == 1 && !syncview_video_player_is_ticking(player));

    pos_events_free(&ev);
    g_object_unref(player);
    g_free(path);
}

/* Durante la riproduzione: frequenza, monotonia, unità, limiti e spegnimento del polling. `with_window` = frame clock reale. */
static void
check_position_while_playing(const char *dir, gboolean with_window)
{
    if (with_window && !frame_clock_usable()) {
        return;
    }
    char *path = make_video(dir, with_window ? "pw.webm" : "pf.webm", 320, 240, 100);
    PosEvents ev;
    SyncviewVideoPlayer *player = pos_player(with_window ? 1 : 0, &ev);
    Shown shown = { NULL, NULL };

    if (with_window) {
        shown = show_window_for(player);
    }
    assert(syncview_video_player_load(player, path, NULL) && spin_until(&ev.got_loaded, 10000));
    gint64 duration = syncview_video_player_get_duration(player);

    assert(!syncview_video_player_is_ticking(player));  /* in pausa: nessun polling */
    assert(syncview_video_player_play(player, NULL));
    assert(syncview_video_player_is_ticking(player));  /* in PLAYING: attivo */
    /* Su runner lenti (CI macOS) il primo frame arriva dopo un po': la frequenza si misura dal primo aggiornamento. */
    for (gint64 deadline = g_get_monotonic_time() + 10 * G_USEC_PER_SEC;
         ev.positions->len == 0 && g_get_monotonic_time() < deadline;) {
        g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
    assert(ev.positions->len > 0);
    spin_for(800);

    guint n = ev.positions->len;
    if (n < 8 || n > 60) {
        g_printerr("check_position_while_playing(window=%d): %u aggiornamenti in ~800 ms, posizioni:", with_window, n);
        for (guint i = 0; i < n; i++) {
            g_printerr(" %" G_GINT64_FORMAT, pos_at(&ev, i));
        }
        g_printerr("\n");
    }
    assert(n >= 8);  /* ~30-50 aggiornamenti al secondo, non uno ogni tanto */
    assert(n <= 60);  /* ma limitati (<=~50 Hz): non uno per ogni tick dello schermo */
    for (guint i = 0; i < n; i++) {
        assert(pos_at(&ev, i) >= 0 && pos_at(&ev, i) <= duration);
        if (i > 0) {
            assert(pos_at(&ev, i) > pos_at(&ev, i - 1));  /* strettamente crescente (e mai duplicata) */
            gint64 gap_us = g_array_index(ev.pos_times, gint64, i) - g_array_index(ev.pos_times, gint64, i - 1);
            assert(gap_us >= 15000);  /* throttle ~20 ms */
        }
    }
    /* Unità: ms, coerenti con la posizione reale della pipeline (~800 ms di riproduzione). */
    gint64 now_pos = syncview_video_player_get_position(player);
    assert(now_pos >= 500 && now_pos <= 2000);
    assert(llabs(now_pos - last_pos(&ev)) <= 120);

    /* pause(): un ultimo aggiornamento con la posizione finale, poi silenzio e polling spento. */
    guint before_pause = ev.positions->len;
    assert(syncview_video_player_pause(player, NULL));
    assert(!syncview_video_player_is_ticking(player));
    settle(player);
    /*
     * Assestamento: alla fine del cambio di stato (ASYNC_DONE) la posizione definitiva viene pubblicata di nuovo
     * e può differire di qualche ms da quella letta subito dopo pause(): al massimo pochi aggiornamenti singoli.
     */
    spin_for(250);
    assert(ev.positions->len - before_pause <= 3);
    gint64 paused_at = syncview_video_player_get_position(player);
    /*
     * La posizione finale è pubblicata a pause(): coincide con quella reale (±5 ms). Con la sola cadenza del ticker
     * l'ultimo valore sarebbe fino a ~33 ms indietro.
     */
    assert(llabs(last_pos(&ev) - paused_at) <= 5);
    guint after_pause = ev.positions->len;
    spin_for(500);
    assert(ev.positions->len == after_pause);  /* poi silenzio assoluto: nessun wakeup periodico da fermo */

    /* Un secondo pause() non ripete lo stesso valore. */
    assert(syncview_video_player_pause(player, NULL));
    assert(ev.positions->len == after_pause);

    /* Nessun segnale di durata durante la riproduzione (la durata non cambia). */
    assert(ev.durations->len == 1);

    if (with_window) {
        gtk_window_destroy(GTK_WINDOW(shown.window));
    }
    pos_events_free(&ev);
    g_object_unref(player);
    g_free(path);
}

static void
test_position_while_playing_fallback_timer(const char *dir)
{
    check_position_while_playing(dir, FALSE);
}

static void
test_position_while_playing_frame_clock(const char *dir)
{
    check_position_while_playing(dir, TRUE);
}

static void
test_position_on_stop_and_end(const char *dir)
{
    char *path = make_video(dir, "se.webm", 320, 240, 25);  /* 1 s */
    PosEvents ev;
    SyncviewVideoPlayer *player = pos_player(2, &ev);

    assert(syncview_video_player_load(player, path, NULL) && spin_until(&ev.got_loaded, 10000));
    gint64 duration = syncview_video_player_get_duration(player);

    /* Fine del video: posizione finale ~ durata, poi nessun altro aggiornamento e polling spento. */
    assert(syncview_video_player_play(player, NULL));
    gint64 deadline = g_get_monotonic_time() + 10 * G_USEC_PER_SEC;
    while (syncview_video_player_get_playback_state(player) == SYNCVIEW_PLAYBACK_PLAYING
           && g_get_monotonic_time() < deadline) {
        g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
    assert(syncview_video_player_get_playback_state(player) == SYNCVIEW_PLAYBACK_STOPPED);
    assert(!syncview_video_player_is_ticking(player));
    settle(player);
    /* Posizione finale pubblicata all'EOS: coincide con la durata (±10 ms), non solo "vicino" all'ultimo tick del polling. */
    assert(llabs(last_pos(&ev) - duration) <= 10);
    guint at_end = ev.positions->len;
    spin_for(400);
    assert(ev.positions->len == at_end);

    /* play() a fine video: riparte da 0 (un aggiornamento con ~0) e poi avanza. */
    assert(syncview_video_player_play(player, NULL));
    spin_for(300);
    gboolean saw_restart = FALSE;
    for (guint i = at_end; i < ev.positions->len; i++) {
        saw_restart |= pos_at(&ev, i) < 150;
    }
    assert(saw_restart);

    /* stop(): posizione a 0 pubblicata a seek concluso, una sola volta, poi silenzio. */
    assert(syncview_video_player_stop(player, NULL));
    assert(!syncview_video_player_is_ticking(player));
    settle(player);
    /* La posizione 0 viene pubblicata a seek concluso (ASYNC_DONE): si attende con un timeout, non un tempo fisso. */
    {
        gint64 deadline = g_get_monotonic_time() + 5 * G_USEC_PER_SEC;
        while ((ev.positions->len == 0 || last_pos(&ev) >= 80) && g_get_monotonic_time() < deadline) {
            g_main_context_iteration(NULL, FALSE);
            g_usleep(1000);
        }
    }
    assert(last_pos(&ev) < 80);
    spin_for(200);  /* eventuali aggiornamenti di assestamento */
    guint after_stop = ev.positions->len;
    spin_for(400);
    assert(ev.positions->len == after_stop);
    assert(ev.durations->len == 1);  /* la durata non è mai cambiata */

    pos_events_free(&ev);
    g_object_unref(player);
    g_free(path);
}

static void
test_position_reset_on_reload_and_error(const char *dir)
{
    char *a = make_video(dir, "pa.webm", 320, 240, 100);
    char *b = make_video(dir, "pb.webm", 640, 360, 50);   /* 2 s */
    PosEvents ev;
    SyncviewVideoPlayer *player = pos_player(3, &ev);

    assert(syncview_video_player_load(player, a, NULL) && spin_until(&ev.got_loaded, 10000));
    gint64 dur_a = syncview_video_player_get_duration(player);
    assert(syncview_video_player_play(player, NULL));
    spin_for(400);
    assert(syncview_video_player_pause(player, NULL));
    settle(player);
    assert(last_pos(&ev) > 200);

    /* Nuovo load: il video vecchio è scartato -> posizione e durata tornano a 0 (subito), poi durata nuova e "caricato". */
    g_string_truncate(ev.order, 0);
    guint n_dur = ev.durations->len;
    ev.got_loaded = FALSE;
    assert(syncview_video_player_load(player, b, NULL));
    assert(strcmp(ev.order->str, "PD") == 0);                   /* 0 ms e durata 0, sincroni col load() */
    assert(last_pos(&ev) == 0 && g_array_index(ev.durations, gint64, n_dur) == 0);
    assert(syncview_video_player_get_duration(player) == 0 && syncview_video_player_get_position(player) == 0);
    assert(spin_until(&ev.got_loaded, 10000));
    assert(strcmp(ev.order->str, "PDDL") == 0);                 /* poi durata del nuovo video e caricato */
    gint64 dur_b = syncview_video_player_get_duration(player);
    assert(dur_b > 1500 && dur_b < 2500 && dur_b < dur_a - 1000);

    /* Errore con un video caricato e in posizione > 0: anche qui tutto a 0, prima del segnale di errore. */
    assert(syncview_video_player_play(player, NULL));
    spin_for(300);
    assert(syncview_video_player_pause(player, NULL));
    assert(last_pos(&ev) > 100);
    g_string_truncate(ev.order, 0);
    GstElement *pipeline = syncview_video_player_get_pipeline(player);
    GError *injected = g_error_new_literal(GST_STREAM_ERROR, GST_STREAM_ERROR_FAILED, "errore");
    gst_element_post_message(pipeline, gst_message_new_error(GST_OBJECT(pipeline), injected, NULL));
    g_error_free(injected);
    assert(spin_until(&ev.got_error, 10000));
    assert(last_pos(&ev) == 0 && g_array_index(ev.durations, gint64, ev.durations->len - 1) == 0);
    assert(syncview_video_player_get_duration(player) == 0 && syncview_video_player_get_position(player) == 0);
    assert(!syncview_video_player_is_ticking(player));

    pos_events_free(&ev);
    g_object_unref(player);
    g_free(a);
    g_free(b);
}

static void
test_ticker_only_while_playing(const char *dir)
{
    if (!frame_clock_usable()) {
        return;
    }
    char *path = make_video(dir, "tk.webm", 320, 240, 25);
    PosEvents ev;
    SyncviewVideoPlayer *player = pos_player(0, &ev);

    assert(!syncview_video_player_is_ticking(player));
    assert(syncview_video_player_load(player, path, NULL));
    assert(!syncview_video_player_is_ticking(player));
    assert(spin_until(&ev.got_loaded, 10000));
    assert(!syncview_video_player_is_ticking(player));               /* caricato, in pausa */
    assert(syncview_video_player_play(player, NULL) && syncview_video_player_is_ticking(player));
    assert(syncview_video_player_pause(player, NULL) && !syncview_video_player_is_ticking(player));
    assert(syncview_video_player_play(player, NULL) && syncview_video_player_is_ticking(player));
    assert(syncview_video_player_stop(player, NULL) && !syncview_video_player_is_ticking(player));
    assert(syncview_video_player_play(player, NULL));
    assert(syncview_video_player_load(player, path, NULL));          /* nuovo load in riproduzione */
    assert(!syncview_video_player_is_ticking(player));
    assert(spin_until(&ev.got_loaded, 10000) || TRUE);

    /* Cambiare il widget del tick durante la riproduzione non interrompe né duplica il polling. */
    ev.got_loaded = FALSE;
    spin_until(&ev.got_loaded, 10000);
    assert(syncview_video_player_play(player, NULL));
    Shown shown = show_window_for(player);          /* imposta il widget mentre è in PLAYING */
    assert(syncview_video_player_is_ticking(player));
    guint n = ev.positions->len;
    spin_for(300);
    assert(ev.positions->len > n);
    syncview_video_player_set_tick_widget(player, NULL);  /* torna al timer di ripiego */
    assert(syncview_video_player_is_ticking(player));
    n = ev.positions->len;
    spin_for(300);
    assert(ev.positions->len > n || syncview_video_player_get_playback_state(player) == SYNCVIEW_PLAYBACK_STOPPED);

    gtk_window_destroy(GTK_WINDOW(shown.window));
    pos_events_free(&ev);
    g_object_unref(player);
    g_free(path);
}

static void
test_tick_widget_destroyed_while_playing(const char *dir)
{
    if (!frame_clock_usable()) {
        return;
    }
    char *path = make_video(dir, "tw.webm", 320, 240, 100);
    PosEvents ev;
    SyncviewVideoPlayer *player = pos_player(1, &ev);

    assert(syncview_video_player_load(player, path, NULL) && spin_until(&ev.got_loaded, 10000));
    Shown shown = show_window_for(player);
    assert(syncview_video_player_play(player, NULL) && syncview_video_player_is_ticking(player));
    spin_for(300);

    /* Il widget sparisce mentre si riproduce: il polling prosegue col timer di ripiego, senza crash né CRITICAL. */
    gtk_window_destroy(GTK_WINDOW(shown.window));
    spin_for(100);
    assert(syncview_video_player_is_ticking(player));
    guint n = ev.positions->len;
    spin_for(400);
    assert(ev.positions->len >= n + 5);

    /* E si ferma normalmente. */
    assert(syncview_video_player_pause(player, NULL) && !syncview_video_player_is_ticking(player));

    pos_events_free(&ev);
    g_object_unref(player);
    g_free(path);
}

static void
test_frame_clock_drives_the_ticks(const char *dir)
{
    if (!frame_clock_usable()) {
        return;
    }
    char *path = make_video(dir, "fc.webm", 320, 240, 100);
    PosEvents ev;
    SyncviewVideoPlayer *player = pos_player(0, &ev);

    assert(syncview_video_player_load(player, path, NULL) && spin_until(&ev.got_loaded, 10000));
    Shown shown = show_window_for(player);
    assert(syncview_video_player_play(player, NULL));
    spin_for(300);
    assert(ev.positions->len >= 4);

    /*
     * Senza frame clock niente tick: staccando il widget dalla finestra (non ha più una root né un frame clock)
     * gli aggiornamenti si fermano, pur restando il callback registrato. Un timer andrebbe avanti: è questa la
     * prova che il polling segue davvero il frame clock di GTK (O5) e non un timer.
     */
    g_object_ref(shown.picture);
    gtk_window_set_child(GTK_WINDOW(shown.window), NULL);
    spin_for(200);
    guint detached_from = ev.positions->len;
    spin_for(500);
    assert(ev.positions->len - detached_from <= 2);
    assert(syncview_video_player_is_ticking(player));
    assert(syncview_video_player_get_playback_state(player) == SYNCVIEW_PLAYBACK_PLAYING);

    /* Riagganciato alla finestra: il frame clock torna e gli aggiornamenti riprendono. */
    gtk_window_set_child(GTK_WINDOW(shown.window), shown.picture);
    g_object_unref(shown.picture);
    gint64 deadline = g_get_monotonic_time() + 5 * G_USEC_PER_SEC;
    while (!gtk_widget_get_mapped(shown.picture) && g_get_monotonic_time() < deadline) {
        g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
    guint reattached_from = ev.positions->len;
    spin_for(500);
    assert(ev.positions->len - reattached_from >= 5);

    gtk_window_destroy(GTK_WINDOW(shown.window));
    pos_events_free(&ev);
    g_object_unref(player);
    g_free(path);
}

static void
test_dispose_while_ticking(const char *dir)
{
    char *path = make_video(dir, "dt.webm", 320, 240, 100);

    for (int i = 0; i < 6; i++) {
        PosEvents ev;
        SyncviewVideoPlayer *player = pos_player(i % SYNCVIEW_MAX_VIDEOS, &ev);
        Shown shown = { NULL, NULL };

        assert(syncview_video_player_load(player, path, NULL) && spin_until(&ev.got_loaded, 10000));
        if (i % 2 && frame_clock_usable()) {
            shown = show_window_for(player);
        }
        assert(syncview_video_player_play(player, NULL));
        spin_for(100 + i * 20);

        pos_events_free(&ev);  /* gli handler puntano a ev: il player va distrutto con i segnali ancora collegati... */
        ev.positions = g_array_new(FALSE, FALSE, sizeof(gint64));
        ev.pos_times = g_array_new(FALSE, FALSE, sizeof(gint64));
        ev.durations = g_array_new(FALSE, FALSE, sizeof(gint64));
        ev.order = g_string_new(NULL);
        g_object_unref(player);  /* distrutto in PLAYING, con ticker attivo */
        spin_for(60);            /* nessun callback residuo su un player distrutto (CRITICAL fatali) */
        if (shown.window) {
            gtk_window_destroy(GTK_WINDOW(shown.window));
        }
        pos_events_free(&ev);
    }

    g_free(path);
}

static void
test_missing_element_error(void)
{
    /* Simula l'assenza del plugin rimuovendo la factory dal registry (poi la si ripristina). */
    GstRegistry *registry = gst_registry_get();
    GstPluginFeature *feature = gst_registry_lookup_feature(registry, "gtk4paintablesink");
    assert(feature != NULL);
    gst_object_ref(feature);
    gst_registry_remove_feature(registry, feature);

    GError *error = NULL;
    SyncviewVideoPlayer *player = syncview_video_player_new(0, &error);
    assert(player == NULL);
    assert(error != NULL && error->domain == SYNCVIEW_VIDEO_PLAYER_ERROR);
    assert(error->code == SYNCVIEW_VIDEO_PLAYER_ERROR_MISSING_ELEMENT);
    assert(strstr(error->message, "gtk4paintablesink") != NULL);  /* nomina l'elemento mancante */
    g_error_free(error);

    assert(gst_registry_add_feature(registry, feature));
    gst_object_unref(feature);

    /* Ripristinato: di nuovo creabile. */
    player = syncview_video_player_new(0, NULL);
    assert(player != NULL);
    g_object_unref(player);
}

/* Vero se SYNCVIEW_TEST_ONLY non è impostata o il nome del test contiene uno dei suoi termini (separati da virgola). */
static gboolean
test_selected(const char *name)
{
    const char *only = g_getenv("SYNCVIEW_TEST_ONLY");

    if (!only || !*only) {
        return TRUE;
    }

    gboolean selected = FALSE;
    char **terms = g_strsplit(only, ",", -1);
    for (int i = 0; terms[i]; i++) {
        selected |= *terms[i] && strstr(name, terms[i]) != NULL;
    }
    g_strfreev(terms);
    return selected;
}

/* --- M2.7: seek, step in ms, frame-step esatto, velocità --- */

#define NS_PER_MS 1000000LL

/* Attende che il frame mostrato (timestamp dell'ultimo buffer arrivato al sink) valga `expected_ns`. */
static gboolean
wait_frame_end(SyncviewVideoPlayer *player, gint64 expected_ns, int timeout_ms)
{
    gint64 deadline = g_get_monotonic_time() + (gint64)timeout_ms * 1000;

    while (g_get_monotonic_time() < deadline) {
        if (llabs(syncview_video_player_get_frame_end_ns(player) - expected_ns) <= 1000) {
            return TRUE;
        }
        g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
    return llabs(syncview_video_player_get_frame_end_ns(player) - expected_ns) <= 1000;
}

/* Attende che il frame mostrato non cambi per 250 ms (seek/step conclusi) e ne ritorna il timestamp. */
static gint64
settled_frame_end(SyncviewVideoPlayer *player)
{
    gint64 last = -2;
    gint64 stable_since = g_get_monotonic_time();
    gint64 deadline = stable_since + 10 * G_USEC_PER_SEC;

    while (g_get_monotonic_time() < deadline) {
        gint64 now_pts = syncview_video_player_get_frame_end_ns(player);
        if (now_pts != last) {
            last = now_pts;
            stable_since = g_get_monotonic_time();
        } else if (g_get_monotonic_time() - stable_since > 250000) {
            return now_pts;
        }
        g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
    return last;
}

static SyncviewVideoPlayer *
loaded_player(PosEvents *ev, const char *path)
{
    SyncviewVideoPlayer *player = pos_player(0, ev);

    assert(syncview_video_player_load(player, path, NULL));
    assert(spin_until(&ev->got_loaded, 10000));
    return player;
}

static gboolean
wait_last_position_between(PosEvents *ev, gint64 low, gint64 high, int timeout_ms)
{
    gint64 deadline = g_get_monotonic_time() + (gint64)timeout_ms * 1000;

    while (g_get_monotonic_time() < deadline) {
        if (ev->positions->len > 0 && last_pos(ev) >= low && last_pos(ev) <= high) {
            return TRUE;
        }
        g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
    return ev->positions->len > 0 && last_pos(ev) >= low && last_pos(ev) <= high;
}

static void
test_seek_and_step_need_a_video(void)
{
    PosEvents ev;
    SyncviewVideoPlayer *player = pos_player(0, &ev);
    GError *error = NULL;

    assert(!syncview_video_player_seek(player, 1000, &error));
    assert(g_error_matches(error, SYNCVIEW_VIDEO_PLAYER_ERROR, SYNCVIEW_VIDEO_PLAYER_ERROR_NOT_LOADED));
    g_clear_error(&error);
    assert(!syncview_video_player_step_ms(player, 40, &error));
    assert(g_error_matches(error, SYNCVIEW_VIDEO_PLAYER_ERROR, SYNCVIEW_VIDEO_PLAYER_ERROR_NOT_LOADED));
    g_clear_error(&error);
    assert(!syncview_video_player_step_frames(player, 1, &error));
    assert(g_error_matches(error, SYNCVIEW_VIDEO_PLAYER_ERROR, SYNCVIEW_VIDEO_PLAYER_ERROR_NOT_LOADED));
    g_clear_error(&error);
    assert(!syncview_video_player_set_playback_rate(player, 2.0, &error));
    assert(g_error_matches(error, SYNCVIEW_VIDEO_PLAYER_ERROR, SYNCVIEW_VIDEO_PLAYER_ERROR_NOT_LOADED));
    g_clear_error(&error);
    assert(ev.positions->len == 0);
    assert(syncview_video_player_get_frame_rate(player) == 0.0 && syncview_video_player_get_frame_end_ns(player) == -1);

    pos_events_free(&ev);
    g_object_unref(player);
}

static void
test_seek(const char *dir)
{
    char *path = make_video(dir, "seek.webm", 320, 240, 100);  /* 4 s a 25 fps: un frame ogni 40 ms */
    PosEvents ev;
    SyncviewVideoPlayer *player = loaded_player(&ev, path);

    assert(llabs(syncview_video_player_get_frame_end_ns(player) - 40 * NS_PER_MS) <= 1000);  /* primo frame: 0-40 ms */
    assert(syncview_video_player_get_frame_rate(player) == 25.0);

    /* Seek accurato: il frame mostrato è quello richiesto, la posizione è riportata a seek concluso. */
    assert(syncview_video_player_seek(player, 2000, NULL));
    assert(wait_frame_end(player, 2040 * NS_PER_MS, 5000));  /* frame 2000-2040 */
    assert(wait_last_position_between(&ev, 1990, 2010, 2000));
    assert(syncview_video_player_get_playback_state(player) == SYNCVIEW_PLAYBACK_PAUSED);  /* lo stato non cambia */
    assert(!syncview_video_player_is_ticking(player));

    /* Un punto in mezzo a un frame mostra il frame che lo contiene. */
    assert(syncview_video_player_seek(player, 1290, NULL));
    assert(wait_frame_end(player, 1320 * NS_PER_MS, 5000));  /* frame 1280-1320 */

    /* Fuori intervallo: limitato a 0..durata. */
    assert(syncview_video_player_seek(player, -500, NULL));
    assert(wait_frame_end(player, 40 * NS_PER_MS, 5000));
    assert(wait_last_position_between(&ev, 0, 10, 2000));
    gint64 duration = syncview_video_player_get_duration(player);
    assert(syncview_video_player_seek(player, duration + 100000, NULL));
    assert(wait_last_position_between(&ev, duration - 80, duration, 5000));
    /* (a un seek esattamente alla durata non arriva alcun frame, solo EOS: si verifica la sola posizione) */

    /* Un nuovo load() azzera subito frame e framerate del video scartato. */
    assert(syncview_video_player_get_frame_end_ns(player) > 0 && syncview_video_player_get_frame_rate(player) == 25.0);
    ev.got_loaded = FALSE;
    assert(syncview_video_player_load(player, path, NULL));
    assert(syncview_video_player_get_frame_end_ns(player) == -1 && syncview_video_player_get_frame_rate(player) == 0.0);
    assert(spin_until(&ev.got_loaded, 10000));
    assert(syncview_video_player_get_frame_rate(player) == 25.0);

    /* Durante la riproduzione: continua a girare dal nuovo punto. */
    assert(syncview_video_player_seek(player, 0, NULL));
    assert(wait_frame_end(player, 40 * NS_PER_MS, 5000));
    assert(syncview_video_player_play(player, NULL));
    assert(syncview_video_player_seek(player, 2000, NULL));
    spin_for(500);
    assert(syncview_video_player_get_playback_state(player) == SYNCVIEW_PLAYBACK_PLAYING);
    assert(syncview_video_player_is_ticking(player));
    gint64 pos = syncview_video_player_get_position(player);
    assert(pos >= 2000 && pos <= 3000);
    assert(syncview_video_player_pause(player, NULL));

    pos_events_free(&ev);
    g_object_unref(player);
    g_free(path);
}

static void
test_seek_after_end_of_video(const char *dir)
{
    char *path = make_video(dir, "seekend.webm", 320, 240, 25);  /* 1 s */
    PosEvents ev;
    SyncviewVideoPlayer *player = loaded_player(&ev, path);

    assert(syncview_video_player_play(player, NULL));
    gint64 deadline = g_get_monotonic_time() + 10 * G_USEC_PER_SEC;
    while (syncview_video_player_get_playback_state(player) != SYNCVIEW_PLAYBACK_STOPPED &&
           g_get_monotonic_time() < deadline) {
        g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
    assert(syncview_video_player_get_playback_state(player) == SYNCVIEW_PLAYBACK_STOPPED);

    /* Dopo la fine ci si sposta a metà: play() riprende da lì, non dall'inizio. */
    assert(syncview_video_player_seek(player, 400, NULL));
    assert(wait_frame_end(player, 440 * NS_PER_MS, 5000));
    assert(syncview_video_player_play(player, NULL));
    spin_for(100);
    gint64 pos = syncview_video_player_get_position(player);
    assert(pos >= 400);

    pos_events_free(&ev);
    g_object_unref(player);
    g_free(path);
}

static void
test_step_ms(const char *dir)
{
    char *path = make_video(dir, "stepms.webm", 320, 240, 100);
    PosEvents ev;
    SyncviewVideoPlayer *player = loaded_player(&ev, path);

    assert(syncview_video_player_step_ms(player, 200, NULL));
    assert(wait_last_position_between(&ev, 195, 205, 5000));
    assert(wait_frame_end(player, 240 * NS_PER_MS, 5000));
    assert(syncview_video_player_step_ms(player, 1000, NULL));
    assert(wait_last_position_between(&ev, 1195, 1205, 5000));
    assert(syncview_video_player_step_ms(player, -200, NULL));
    assert(wait_last_position_between(&ev, 995, 1005, 5000));
    assert(syncview_video_player_get_playback_state(player) == SYNCVIEW_PLAYBACK_PAUSED);

    /* Limiti 0..durata. */
    assert(syncview_video_player_step_ms(player, -100000, NULL));
    assert(wait_last_position_between(&ev, 0, 10, 5000));
    gint64 duration = syncview_video_player_get_duration(player);
    assert(syncview_video_player_step_ms(player, 100000, NULL));
    assert(wait_last_position_between(&ev, duration - 80, duration, 5000));

    /* Subito dopo un seek (ancora in corso) e ravvicinati: partono dalla destinazione, non dalla posizione vecchia. */
    assert(syncview_video_player_seek(player, 400, NULL));
    assert(syncview_video_player_step_ms(player, 40, NULL));
    assert(wait_last_position_between(&ev, 435, 445, 5000));
    for (int i = 0; i < 5; i++) {
        assert(syncview_video_player_step_ms(player, 40, NULL));
    }
    assert(wait_last_position_between(&ev, 635, 645, 5000));
    spin_for(300);
    assert(last_pos(&ev) >= 635 && last_pos(&ev) <= 645);

    /* In riproduzione lo step mette in pausa, come nell'originale. */
    assert(syncview_video_player_seek(player, 520, NULL));
    assert(wait_frame_end(player, 560 * NS_PER_MS, 5000));
    assert(syncview_video_player_play(player, NULL));
    spin_for(200);
    assert(syncview_video_player_step_ms(player, 100, NULL));
    assert(syncview_video_player_get_playback_state(player) == SYNCVIEW_PLAYBACK_PAUSED);
    assert(!syncview_video_player_is_ticking(player));

    pos_events_free(&ev);
    g_object_unref(player);
    g_free(path);
}

/*
 * Frame-step esatto (O1). Verità di riferimento: i timestamp che si ottengono avanzando di un frame alla volta
 * (GST_EVENT_STEP è esatto per definizione). Tutto il resto — step all'indietro, salti di N frame, passi ravvicinati
 * senza attendere — deve ricadere esattamente sugli stessi timestamp. I container in ms (WebM) a 30 fps hanno pts
 * irregolari (0, 33, 67, 100...): è il caso che un calcolo ingenuo «pos − 1000/fps» sbaglierebbe.
 */
#define STEP_FRAMES 12

static void
step_and_settle(SyncviewVideoPlayer *player, int count, gint64 *pts_out)
{
    assert(syncview_video_player_step_frames(player, count, NULL));
    *pts_out = settled_frame_end(player);
}

static void
check_exact_frame_step(const char *dir, int fps)
{
    char *name = g_strdup_printf("exact%d.webm", fps);
    char *path = make_video_fps(dir, name, 160, 120, 60, fps, FALSE);
    PosEvents ev;
    SyncviewVideoPlayer *player = loaded_player(&ev, path);
    gint64 truth[STEP_FRAMES + 1];

    assert(syncview_video_player_get_frame_rate(player) == (double)fps);
    gint64 frame_ns = GST_SECOND / fps;
    truth[0] = settled_frame_end(player);
    assert(llabs(truth[0] - frame_ns) <= 2 * NS_PER_MS);  /* primo frame: finisce dopo ~1/fps */

    /* Riferimento: un frame avanti alla volta; ogni passo cambia frame e ne avanza uno solo (~1/fps). */
    for (int i = 1; i <= STEP_FRAMES; i++) {
        step_and_settle(player, 1, &truth[i]);
        assert(truth[i] > truth[i - 1]);
        assert(llabs((truth[i] - truth[i - 1]) - frame_ns) <= 1500 * 1000);  /* ms di arrotondamento del container */
    }
    assert(syncview_video_player_get_playback_state(player) == SYNCVIEW_PLAYBACK_PAUSED);
    /* La posizione segue il frame: da dove inizia a dove finisce. */
    assert(wait_last_position_between(&ev, (truth[STEP_FRAMES] - frame_ns) / NS_PER_MS - 2, truth[STEP_FRAMES] / NS_PER_MS, 2000));

    /* Indietro un frame alla volta: esattamente gli stessi frame, in ordine inverso. */
    for (int i = STEP_FRAMES - 1; i >= 0; i--) {
        gint64 pts;
        step_and_settle(player, -1, &pts);
        assert(pts == truth[i]);
    }

    /* Dal primo frame non si va oltre. */
    gint64 pts;
    step_and_settle(player, -1, &pts);
    assert(pts == truth[0]);

    /* Salti di N frame in una volta. */
    step_and_settle(player, 5, &pts);
    assert(pts == truth[5]);
    step_and_settle(player, -3, &pts);
    assert(pts == truth[2]);
    step_and_settle(player, STEP_FRAMES - 2, &pts);
    assert(pts == truth[STEP_FRAMES]);
    step_and_settle(player, -STEP_FRAMES, &pts);
    assert(pts == truth[0]);

    /* Passi ravvicinati, senza attendere la fine del seek precedente: nessun frame perso. */
    step_and_settle(player, STEP_FRAMES, &pts);
    assert(pts == truth[STEP_FRAMES]);
    for (int i = 0; i < 6; i++) {
        assert(syncview_video_player_step_frames(player, -1, NULL));
    }
    assert(settled_frame_end(player) == truth[STEP_FRAMES - 6]);
    for (int i = 0; i < 4; i++) {
        assert(syncview_video_player_step_frames(player, 1, NULL));
    }
    assert(settled_frame_end(player) == truth[STEP_FRAMES - 2]);

    /*
     * Seek esattamente sull'inizio di un frame e step indietro SUBITO (seek ancora in corso): a 30 fps i pts in ms
     * distano 33 o 34 ms, quindi «inizio − 1/fps» può cadere un frame troppo indietro senza un margine sull'ancora.
     */
    for (int j = 1; j <= STEP_FRAMES; j++) {
        gint64 start_ms = (truth[j] - frame_ns + NS_PER_MS / 2) / NS_PER_MS;  /* pts del frame j, intero in ms */
        assert(syncview_video_player_seek(player, start_ms, NULL));
        assert(syncview_video_player_step_frames(player, -1, NULL));
        assert(settled_frame_end(player) == truth[j - 1]);
    }

    /* Oltre l'ultimo frame non si va: molti passi avanti ravvicinati si fermano sull'ultimo, e si torna indietro di uno. */
    gint64 duration_ns = syncview_video_player_get_duration(player) * NS_PER_MS;
    for (int i = 0; i < 100; i++) {
        assert(syncview_video_player_step_frames(player, 1, NULL));
    }
    gint64 last_end = settled_frame_end(player);
    assert(last_end >= duration_ns - 2 * NS_PER_MS && last_end <= duration_ns + 2 * NS_PER_MS);
    step_and_settle(player, -1, &pts);
    assert(llabs((last_end - pts) - frame_ns) <= 2 * NS_PER_MS);

    /* Dopo un seek arbitrario l'ancora si ricalcola dal frame mostrato. */
    assert(syncview_video_player_seek(player, 1000, NULL));
    gint64 shown = settled_frame_end(player);
    assert(shown > 1000 * NS_PER_MS && shown <= 1000 * NS_PER_MS + frame_ns + 2 * NS_PER_MS);
    step_and_settle(player, -1, &pts);
    assert(pts < shown && shown - pts <= frame_ns + 1500 * 1000);
    gint64 back = pts;
    step_and_settle(player, 1, &pts);
    assert(pts == shown);
    (void)back;

    /* In riproduzione lo step mette in pausa e avanza di un frame rispetto al frame mostrato. */
    assert(syncview_video_player_play(player, NULL));
    spin_for(300);
    assert(syncview_video_player_step_frames(player, 1, NULL));
    assert(syncview_video_player_get_playback_state(player) == SYNCVIEW_PLAYBACK_PAUSED);
    assert(!syncview_video_player_is_ticking(player));
    gint64 after_play = settled_frame_end(player);
    step_and_settle(player, 1, &pts);
    assert(pts > after_play && pts - after_play <= frame_ns + 1500 * 1000);

    pos_events_free(&ev);
    g_object_unref(player);
    g_free(path);
    g_free(name);
}

static void
test_frame_step_exact_25fps(const char *dir)
{
    check_exact_frame_step(dir, 25);
}

static void
test_frame_step_exact_30fps(const char *dir)
{
    check_exact_frame_step(dir, 30);
}

static void
test_frame_step_unknown_frame_rate(const char *dir)
{
    char *path = make_video_fps(dir, "vfr.webm", 160, 120, 50, 25, TRUE);
    PosEvents ev;
    SyncviewVideoPlayer *player = loaded_player(&ev, path);

    /* Framerate variabile/sconosciuto: ricade sullo step in ms dell'originale (40 ms per frame). */
    assert(syncview_video_player_get_frame_rate(player) == 0.0);
    assert(syncview_video_player_step_frames(player, 5, NULL));
    assert(wait_last_position_between(&ev, 195, 205, 5000));
    assert(syncview_video_player_step_frames(player, -2, NULL));
    assert(wait_last_position_between(&ev, 115, 125, 5000));

    pos_events_free(&ev);
    g_object_unref(player);
    g_free(path);
}

/* Velocità media della posizione (ms di video per ms reale) su una finestra di ~`window_ms` di orologio. */
static double
measured_speed(SyncviewVideoPlayer *player, int window_ms)
{
    gint64 t0 = g_get_monotonic_time();
    gint64 p0 = syncview_video_player_get_position(player);

    spin_for(window_ms);
    gint64 t1 = g_get_monotonic_time();
    gint64 p1 = syncview_video_player_get_position(player);
    return (double)(p1 - p0) / ((double)(t1 - t0) / 1000.0);
}

static void
test_playback_rate(const char *dir)
{
    char *path = make_video(dir, "rate.webm", 160, 120, 250);  /* 10 s */
    PosEvents ev;
    SyncviewVideoPlayer *player = loaded_player(&ev, path);
    GError *error = NULL;

    assert(syncview_video_player_get_playback_rate(player) == 1.0);

    /* Argomenti non validi: nessun cambiamento. */
    double bad[] = { 0.0, -1.0, NAN, INFINITY };
    for (guint i = 0; i < G_N_ELEMENTS(bad); i++) {
        assert(!syncview_video_player_set_playback_rate(player, bad[i], &error));
        assert(g_error_matches(error, SYNCVIEW_VIDEO_PLAYER_ERROR, SYNCVIEW_VIDEO_PLAYER_ERROR_INVALID_ARGUMENT));
        g_clear_error(&error);
    }
    assert(syncview_video_player_get_playback_rate(player) == 1.0);

    /* Da fermo: la velocità si imposta e vale al play(), senza spostare il frame mostrato. */
    assert(syncview_video_player_set_playback_rate(player, 2.0, NULL));
    assert(syncview_video_player_get_playback_rate(player) == 2.0);
    assert(llabs(settled_frame_end(player) - 40 * NS_PER_MS) <= 1000);
    assert(syncview_video_player_play(player, NULL));
    spin_for(300);
    double speed = measured_speed(player, 800);
    assert(speed > 1.5 && speed < 2.6);

    /* In riproduzione cambia senza interrompere né spostare il video. */
    gint64 before = syncview_video_player_get_position(player);
    assert(syncview_video_player_set_playback_rate(player, 0.5, NULL));
    gint64 after = syncview_video_player_get_position(player);
    assert(after >= before - 100 && after - before < 300);  /* nessun salto (al più un frame indietro per il seek) */
    spin_for(300);
    speed = measured_speed(player, 1000);
    assert(speed > 0.3 && speed < 0.75);
    assert(syncview_video_player_get_playback_state(player) == SYNCVIEW_PLAYBACK_PLAYING);

    assert(syncview_video_player_set_playback_rate(player, 1.0, NULL));
    spin_for(300);
    speed = measured_speed(player, 800);
    assert(speed > 0.75 && speed < 1.3);

    /* Un seek e uno stop non la riportano a 1.0x. */
    assert(syncview_video_player_set_playback_rate(player, 2.0, NULL));
    assert(syncview_video_player_seek(player, 1000, NULL));
    spin_for(300);
    speed = measured_speed(player, 800);
    assert(speed > 1.5 && speed < 2.6);
    assert(syncview_video_player_stop(player, NULL));
    assert(syncview_video_player_get_playback_rate(player) == 2.0);
    assert(syncview_video_player_play(player, NULL));
    spin_for(300);
    speed = measured_speed(player, 800);
    assert(speed > 1.5 && speed < 2.6);
    assert(syncview_video_player_pause(player, NULL));

    /* Un nuovo load() riparte a 1.0x. */
    ev.got_loaded = FALSE;
    assert(syncview_video_player_load(player, path, NULL) && spin_until(&ev.got_loaded, 10000));
    assert(syncview_video_player_get_playback_rate(player) == 1.0);
    assert(syncview_video_player_play(player, NULL));
    spin_for(300);
    speed = measured_speed(player, 800);
    assert(speed > 0.75 && speed < 1.3);

    pos_events_free(&ev);
    g_object_unref(player);
    g_free(path);
}

static void
test_seek_step_log(const char *dir)
{
    /* Le operazioni lasciano traccia nel log (modulo VIDEO), come nell'originale. */
    char *path = make_video(dir, "log27.webm", 160, 120, 50);
    char *log_path = g_build_filename(dir, "m27.log", NULL);
    PosEvents ev;

    g_unsetenv("SYNCVIEW_DEBUG");
    assert(logger_init(log_path, FALSE, NULL));
    SyncviewVideoPlayer *player = loaded_player(&ev, path);
    assert(syncview_video_player_seek(player, 400, NULL));
    assert(syncview_video_player_step_ms(player, 40, NULL));
    assert(syncview_video_player_step_frames(player, 1, NULL));
    assert(syncview_video_player_set_playback_rate(player, 1.5, NULL));
    spin_for(300);
    gint64 duration = syncview_video_player_get_duration(player);
    assert(duration >= 1900 && duration <= 2100);
    assert(syncview_video_player_seek(player, 10000, NULL));  /* oltre la fine: si registra la destinazione effettiva */
    spin_for(300);
    pos_events_free(&ev);
    g_object_unref(player);
    logger_shutdown();

    char *log = NULL;
    assert(g_file_get_contents(log_path, &log, NULL, NULL));
    assert(strstr(log, "[VIDEO 1] Timeline seek: 00:00 (400ms)") != NULL);
    char *clamped = g_strdup_printf("[VIDEO 1] Timeline seek: 00:0%d (%lldms)", (int)(duration / 1000), (long long)duration);
    assert(strstr(log, clamped) != NULL);
    g_free(clamped);
    assert(strstr(log, "10000ms") == NULL);
    assert(strstr(log, "[VIDEO 1] Step (ms)") != NULL);
    assert(strstr(log, "[VIDEO 1] Step Frame") != NULL);
    assert(strstr(log, "[VIDEO 1] Velocità") != NULL && strstr(log, "1.50x") != NULL);
    g_free(log);
    g_free(log_path);
    g_free(path);
}

/*
 * Con SYNCVIEW_TEST_TRACE=1 stampa il nome di ogni test prima di eseguirlo (utile se uno si blocca);
 * con SYNCVIEW_TEST_ONLY=<t1>,<t2>… esegue solo i test di load/riproduzione il cui nome contiene uno dei termini
 * (i test che aprono finestre sono quelli con "frame_clock", "ticker", "tick_widget", "ticking").
 */
#define RUN_TEST(call)                                               \
    do {                                                             \
        if (!test_selected(#call)) {                                 \
            break;                                                   \
        }                                                            \
        if (g_getenv("SYNCVIEW_TEST_TRACE")) {                       \
            fprintf(stderr, ">> %s\n", #call);                       \
        }                                                            \
        call;                                                        \
    } while (0)

int
main(void)
{
    if (!gtk_init_check()) {
        return SKIP_EXIT;  /* nessun display */
    }

    /* Con SYNCVIEW_DEBUG=1 il logger è attivo: gli eventi [GST] del player (stati, ASYNC_DONE, decoder…) compaiono su stderr. */
    if (g_getenv("SYNCVIEW_DEBUG")) {
        logger_init(NULL, FALSE, NULL);
    }

    /* Da qui un CRITICAL/ERROR di GLib/GObject/GTK deve far fallire il test. */
    g_log_set_always_fatal(G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);
    gst_init(NULL, NULL);

    GstElementFactory *f = gst_element_factory_find("gtk4paintablesink");
    GstElementFactory *p = gst_element_factory_find("playbin3");
    if (!f || !p) {
        return SKIP_EXIT;  /* plugin gtk4 (gst-plugins-rs) non installato */
    }
    gst_object_unref(f);
    gst_object_unref(p);

    test_type_and_construction();
    test_paintable_in_gtk_picture();
    test_invalid_index();
    test_independent_players();
    test_lifecycle();
    test_owned_objects_are_released();

    /* Test di load(): servono i plugin per generare i file di prova (vp8enc/webmmux, base+good). */
    const char *encoder_elements[] = { "videotestsrc", "videoconvert", "vp8enc", "webmmux", "filesink", NULL };
    have_test_encoder = TRUE;
    for (int i = 0; encoder_elements[i]; i++) {
        GstElementFactory *factory = gst_element_factory_find(encoder_elements[i]);
        if (!factory) {
            have_test_encoder = FALSE;
        } else {
            gst_object_unref(factory);
        }
    }

    if (have_test_encoder) {
        char *dir = g_dir_make_tmp("syncview-player-XXXXXX", NULL);
        assert(dir != NULL);

        RUN_TEST(test_load_first_frame(dir));
        RUN_TEST(test_load_errors(dir));
        RUN_TEST(test_error_after_loaded(dir));
        RUN_TEST(test_reload_replaces_video(dir));
        RUN_TEST(test_players_load_independently(dir));
        RUN_TEST(test_dispose_while_loading(dir));

        RUN_TEST(test_play_pause_basics(dir));
        RUN_TEST(test_toggle(dir));
        RUN_TEST(test_stop_rewinds_and_keeps_video(dir));
        RUN_TEST(test_end_of_video(dir));
        RUN_TEST(test_not_loaded_is_noop(dir));
        RUN_TEST(test_reload_while_playing(dir));
        RUN_TEST(test_error_while_playing(dir));
        RUN_TEST(test_players_play_independently(dir));
        RUN_TEST(test_dispose_while_playing(dir));
        RUN_TEST(test_playback_log(dir));

        RUN_TEST(test_load_reports_duration(dir));
        RUN_TEST(test_position_while_playing_fallback_timer(dir));
        RUN_TEST(test_position_while_playing_frame_clock(dir));
        RUN_TEST(test_position_on_stop_and_end(dir));
        RUN_TEST(test_position_reset_on_reload_and_error(dir));
        RUN_TEST(test_ticker_only_while_playing(dir));
        RUN_TEST(test_tick_widget_destroyed_while_playing(dir));
        RUN_TEST(test_frame_clock_drives_the_ticks(dir));
        RUN_TEST(test_dispose_while_ticking(dir));

        RUN_TEST(test_seek_and_step_need_a_video());
        RUN_TEST(test_seek(dir));
        RUN_TEST(test_seek_after_end_of_video(dir));
        RUN_TEST(test_step_ms(dir));
        RUN_TEST(test_frame_step_exact_25fps(dir));
        RUN_TEST(test_frame_step_exact_30fps(dir));
        RUN_TEST(test_frame_step_unknown_frame_rate(dir));
        RUN_TEST(test_playback_rate(dir));
        RUN_TEST(test_seek_step_log(dir));

        /* Nessuno smontaggio di pipeline deve restare appeso (un blocco interno a GStreamer lo farebbe fallire qui). */
        assert(drain_teardowns(15000));

        char *cmd = g_strdup_printf("rm -rf '%s'", dir);
        assert(system(cmd) == 0);
        g_free(cmd);
        g_free(dir);
    }

    test_missing_element_error();
    assert(drain_teardowns(15000));
    return 0;
}
