/*
 * M2.3: scheletro di SyncviewVideoPlayer. Richiede un display (GTK) e il
 * plugin GStreamer gtk4paintablesink: se mancano, termina con 77 (skip Meson).
 */
#include "core/logger.h"
#include "core/settings.h"
#include "video/video_player.h"

#include <assert.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <gst/gst.h>
#include <string.h>

#define SKIP_EXIT 77

static GstState
pipeline_state(GstElement *pipeline)
{
    GstState state = GST_STATE_VOID_PENDING;
    gst_element_get_state(pipeline, &state, NULL, 0);
    return state;
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

        /* La pipeline era solo nostra: dopo il dispose resta l'ultimo riferimento (il nostro), in stato NULL. */
        assert(GST_OBJECT_REFCOUNT(pipeline) == 1);
        assert(pipeline_state(pipeline) == GST_STATE_NULL);
        gst_object_unref(pipeline);
    }
}

typedef struct {
    gboolean pipeline_finalized;
    gboolean sink_finalized;
    gboolean paintable_finalized;
} Finalized;

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

        Finalized fin = { FALSE, FALSE, FALSE };
        GstElement *pipeline = syncview_video_player_get_pipeline(player);
        GstElement *sink = NULL;
        g_object_get(pipeline, "video-sink", &sink, NULL);  /* riferimento forte, lo rilasciamo subito */
        g_object_weak_ref(G_OBJECT(pipeline), on_pipeline_gone, &fin);
        g_object_weak_ref(G_OBJECT(sink), on_sink_gone, &fin);
        g_object_weak_ref(G_OBJECT(syncview_video_player_get_paintable(player)), on_paintable_gone, &fin);
        gst_object_unref(sink);

        g_object_unref(player);

        /* Il paintable può essere rilasciato dal sink anche un attimo dopo: si lascia girare il main context. */
        for (int i = 0; i < 20 && !(fin.paintable_finalized && fin.sink_finalized); i++) {
            g_main_context_iteration(NULL, FALSE);
        }

        assert(fin.pipeline_finalized);
        assert(fin.sink_finalized);
        assert(fin.paintable_finalized);
    }
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

int
main(void)
{
    if (!gtk_init_check()) {
        return SKIP_EXIT;  /* nessun display */
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
    test_missing_element_error();
    return 0;
}
