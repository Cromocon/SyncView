/*
 * core/frame_index: rilevamento dell'irregolarità dei timestamp, ricerca del frame e lettura dei timestamp di un file
 * (framerate variabile → indice; framerate costante → nessun indice). Nessun display: serve solo GStreamer.
 */
#include "core/frame_index.h"

#include "vfr_clip.h"

#include <assert.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>

#define SKIP_EXIT 77

static gint64 *
make_pts(guint n, gint64 (*at)(guint))
{
    gint64 *pts = g_new(gint64, n);

    for (guint i = 0; i < n; i++) {
        pts[i] = at(i);
    }
    return pts;
}

static gint64 constant_25(guint i) { return (gint64)i * 40 * GST_MSECOND; }
/* WebM/MKV: timestamp arrotondati al ms, 30 fps → intervalli di 33 e 34 ms. */
static gint64 rounded_30(guint i) { return (gint64)((i * 1000) / 30) * GST_MSECOND; }
static gint64 irregular(guint i) { return vfr_frame_start_ns(i); }

static void
test_irregular_detection(void)
{
    gint64 *a = make_pts(200, constant_25);
    gint64 *b = make_pts(200, rounded_30);
    gint64 *c = make_pts(200, irregular);

    assert(!frame_index_timestamps_irregular(a, 200));
    assert(!frame_index_timestamps_irregular(b, 200));  /* l'arrotondamento al ms non è irregolarità */
    assert(frame_index_timestamps_irregular(c, 200));
    assert(!frame_index_timestamps_irregular(c, FRAME_INDEX_MIN_FRAMES - 1));  /* troppo pochi per decidere */
    assert(!frame_index_timestamps_irregular(NULL, 0));

    /* L'ordine di arrivo (frame B) non conta. */
    gint64 t = c[10];
    c[10] = c[20];
    c[20] = t;
    assert(frame_index_timestamps_irregular(c, 200));

    g_free(a);
    g_free(b);
    g_free(c);
}

static void
test_index_basics(void)
{
    gint64 *pts = g_new(gint64, 6);
    pts[0] = 40; pts[1] = 0; pts[2] = 40; pts[3] = 100; pts[4] = 52; pts[5] = 77;  /* disordinati, con un doppione */
    FrameIndex *index = frame_index_new(pts, 6);

    assert(index->n == 5);
    assert(index->pts[0] == 0 && index->pts[1] == 40 && index->pts[2] == 52 && index->pts[3] == 77 && index->pts[4] == 100);
    assert(frame_index_start(index, 2) == 52 && frame_index_end(index, 2) == 77);
    assert(frame_index_end(index, 4) == 100 + (100 - 77));  /* l'ultimo frame dura quanto il penultimo intervallo */

    assert(frame_index_find(index, -5) == 0);
    assert(frame_index_find(index, 0) == 0);
    assert(frame_index_find(index, 39) == 0);
    assert(frame_index_find(index, 40) == 1);
    assert(frame_index_find(index, 76) == 2);
    assert(frame_index_find(index, 77) == 3);
    assert(frame_index_find(index, 5000) == 4);
    frame_index_free(index);
    frame_index_free(NULL);
}

static void
test_scan(const char *dir)
{
    /* Framerate variabile: l'indice ha tutti i frame, con i timestamp che il file contiene (al ms: lo scrive il muxer). */
    char *vfr = make_vfr_video(dir, "vfr.webm", 120);
    FrameIndex *index = frame_index_scan(vfr, NULL);

    assert(index != NULL);
    assert(index->n == 120);
    for (guint i = 0; i < index->n; i++) {
        assert(llabs(index->pts[i] - vfr_frame_start_ns(i)) <= GST_MSECOND);
    }
    frame_index_free(index);

    /* Annullata subito: niente indice. */
    gint cancelled = 1;
    assert(frame_index_scan(vfr, &cancelled) == NULL);

    /* Framerate costante: nessun indice (la lettura si ferma dopo i primi frame). */
    char *cfr = g_build_filename(dir, "cfr.webm", NULL);
    char *desc = g_strdup_printf("videotestsrc num-buffers=120 ! video/x-raw,width=160,height=120,framerate=25/1 ! videoconvert ! "
                                 "vp8enc ! webmmux ! filesink location=\"%s\"", cfr);
    GstElement *pipeline = gst_parse_launch(desc, NULL);
    assert(pipeline);
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    GstBus *bus = gst_element_get_bus(pipeline);
    GstMessage *msg = gst_bus_timed_pop_filtered(bus, 30 * GST_SECOND, GST_MESSAGE_EOS | GST_MESSAGE_ERROR);
    assert(msg && GST_MESSAGE_TYPE(msg) == GST_MESSAGE_EOS);
    gst_message_unref(msg);
    gst_object_unref(bus);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    assert(frame_index_scan(cfr, NULL) == NULL);

    /* File inesistente: nessun indice, nessun crash. */
    assert(frame_index_scan("/nonexistent/video.webm", NULL) == NULL);

    g_free(desc);
    g_free(cfr);
    g_free(vfr);
}

int
main(void)
{
    g_log_set_always_fatal(G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);
    gst_init(NULL, NULL);

    test_irregular_detection();
    test_index_basics();

    const char *elements[] = { "videotestsrc", "videoconvert", "vp8enc", "webmmux", "capssetter", "filesink", "parsebin", NULL };
    for (int i = 0; elements[i]; i++) {
        GstElementFactory *factory = gst_element_factory_find(elements[i]);
        if (!factory) {
            fprintf(stderr, "elemento %s mancante: lettura dei file non provata\n", elements[i]);
            return SKIP_EXIT;
        }
        gst_object_unref(factory);
    }

    GError *error = NULL;
    char *dir = g_dir_make_tmp("syncview-frame-index-XXXXXX", &error);
    assert(dir != NULL);
    test_scan(dir);

    GDir *d = g_dir_open(dir, 0, NULL);
    const char *name;
    while (d && (name = g_dir_read_name(d))) {
        char *file = g_build_filename(dir, name, NULL);
        g_remove(file);
        g_free(file);
    }
    if (d) {
        g_dir_close(d);
    }
    g_rmdir(dir);
    g_free(dir);
    return 0;
}
