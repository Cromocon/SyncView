/*
 * core/frame_index: rilevamento dell'irregolarità dei timestamp, ricerca del frame e lettura dei timestamp di un file
 * (framerate variabile → indice; framerate costante → nessun indice). Nessun display: serve solo GStreamer.
 */
#include "core/frame_index.h"

#include "vfr_clip.h"

#include <glib.h>
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

    g_assert_false(frame_index_timestamps_irregular(a, 200));
    g_assert_false(frame_index_timestamps_irregular(b, 200));  /* l'arrotondamento al ms non è irregolarità */
    g_assert_true(frame_index_timestamps_irregular(c, 200));
    g_assert_false(frame_index_timestamps_irregular(c, FRAME_INDEX_MIN_FRAMES - 1));  /* troppo pochi per decidere */
    g_assert_false(frame_index_timestamps_irregular(NULL, 0));

    /* L'ordine di arrivo (frame B) non conta. */
    gint64 t = c[10];
    c[10] = c[20];
    c[20] = t;
    g_assert_true(frame_index_timestamps_irregular(c, 200));

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

    g_assert_cmpint(index->n, ==, 5);
    g_assert_true(index->pts[0] == 0 && index->pts[1] == 40 && index->pts[2] == 52 && index->pts[3] == 77 && index->pts[4] == 100);
    g_assert_true(frame_index_start(index, 2) == 52 && frame_index_end(index, 2) == 77);
    g_assert_true(frame_index_end(index, 4) == 100 + (100 - 77));  /* l'ultimo frame dura quanto il penultimo intervallo */

    g_assert_cmpint(frame_index_find(index, -5), ==, 0);
    g_assert_cmpint(frame_index_find(index, 0), ==, 0);
    g_assert_cmpint(frame_index_find(index, 39), ==, 0);
    g_assert_cmpint(frame_index_find(index, 40), ==, 1);
    g_assert_cmpint(frame_index_find(index, 76), ==, 2);
    g_assert_cmpint(frame_index_find(index, 77), ==, 3);
    g_assert_cmpint(frame_index_find(index, 5000), ==, 4);
    frame_index_free(index);
    frame_index_free(NULL);
}

static void
test_empty_index(void)
{
    /* Indice vuoto (nessun frame letto): nessuna lettura fuori dall'array. */
    FrameIndex *index = frame_index_new(g_new(gint64, 1), 0);

    g_assert_cmpint(index->n, ==, 0);
    g_assert_cmpint(frame_index_start(index, 0), ==, 0);
    g_assert_cmpint(frame_index_start(index, 5), ==, 0);
    g_assert_cmpint(frame_index_find(index, 100), ==, 0);
    frame_index_free(index);
}

typedef struct {
    guint n;
    gint64 offset_ns;
} OffsetCounter;

static GstPadProbeReturn
offset_rewrite_timestamps(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{
    (void)pad;
    OffsetCounter *c = user_data;
    GstBuffer *buffer = gst_buffer_make_writable(GST_PAD_PROBE_INFO_BUFFER(info));

    GST_BUFFER_PTS(buffer) = (GstClockTime)(vfr_frame_start_ns(c->n) + c->offset_ns);
    GST_BUFFER_DTS(buffer) = GST_BUFFER_PTS(buffer);
    GST_BUFFER_DURATION(buffer) = (GstClockTime)vfr_intervals_ms[c->n % VFR_INTERVALS_COUNT] * GST_MSECOND;
    c->n++;
    GST_PAD_PROBE_INFO_DATA(info) = buffer;
    return GST_PAD_PROBE_OK;
}

/* webm con DUE tracce video irregolari, la seconda sfasata di 7 ms: l'indice deve contare i frame di una sola. */
static char *
make_two_video_streams(const char *dir, int frames)
{
    char *path = g_build_filename(dir, "two-video.webm", NULL);
    char *desc = g_strdup_printf(
        "webmmux name=mux ! filesink name=out "
        "videotestsrc name=a num-buffers=%d ! video/x-raw,width=160,height=120,framerate=25/1 ! videoconvert ! vp8enc keyframe-max-dist=10 ! mux. "
        "videotestsrc name=b num-buffers=%d ! video/x-raw,width=160,height=120,framerate=25/1 ! videoconvert ! vp8enc keyframe-max-dist=10 ! mux.",
        frames, frames);
    GstElement *pipeline = gst_parse_launch(desc, NULL);
    g_assert_true(pipeline);
    GstElement *out = gst_bin_get_by_name(GST_BIN(pipeline), "out");
    g_object_set(out, "location", path, NULL);
    gst_object_unref(out);

    OffsetCounter counters[2] = { { 0, 0 }, { 0, 7 * GST_MSECOND } };
    const char *names[] = { "a", "b" };
    for (int i = 0; i < 2; i++) {
        GstElement *src = gst_bin_get_by_name(GST_BIN(pipeline), names[i]);
        GstPad *pad = gst_element_get_static_pad(src, "src");
        gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, offset_rewrite_timestamps, &counters[i], NULL);
        gst_object_unref(pad);
        gst_object_unref(src);
    }
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    GstBus *bus = gst_element_get_bus(pipeline);
    GstMessage *msg = gst_bus_timed_pop_filtered(bus, 30 * GST_SECOND, GST_MESSAGE_EOS | GST_MESSAGE_ERROR);
    g_assert_true(msg && GST_MESSAGE_TYPE(msg) == GST_MESSAGE_EOS);
    gst_message_unref(msg);
    gst_object_unref(bus);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    g_free(desc);
    return path;
}

static void
test_scan(const char *dir)
{
    /* Framerate variabile: l'indice ha tutti i frame, con i timestamp che il file contiene (al ms: lo scrive il muxer). */
    char *vfr = make_vfr_video(dir, "vfr.webm", 120);
    FrameIndex *index = frame_index_scan(vfr, NULL);

    g_assert_nonnull(index);
    g_assert_cmpint(index->n, ==, 120);
    for (guint i = 0; i < index->n; i++) {
        g_assert_true(llabs(index->pts[i] - vfr_frame_start_ns(i)) <= GST_MSECOND);
    }
    frame_index_free(index);

    /* File troncato (errore o EOS prematuro del demuxer): se la lettura non finisce con EOS niente indice; se finisce,
     * l'indice contiene al più i frame presenti e mai timestamp inventati. */
    gchar *content = NULL;
    gsize len = 0;
    g_assert_true(g_file_get_contents(vfr, &content, &len, NULL));
    char *cut = g_build_filename(dir, "cut.webm", NULL);
    g_assert_true(g_file_set_contents(cut, content, (gssize)(len / 2), NULL));
    FrameIndex *partial = frame_index_scan(cut, NULL);
    fprintf(stderr, "file troncato: indice %s (%u frame)\n", partial ? "presente" : "assente", partial ? partial->n : 0);
    if (partial) {
        g_assert_true(partial->n < 120);
        for (guint i = 0; i < partial->n; i++) {
            g_assert_true(llabs(partial->pts[i] - vfr_frame_start_ns(i)) <= GST_MSECOND);
        }
        frame_index_free(partial);
    }
    g_free(cut);
    g_free(content);

    /* Due tracce video: i frame sono quelli della prima, non la somma. */
    char *two = make_two_video_streams(dir, 120);
    FrameIndex *two_index = frame_index_scan(two, NULL);
    g_assert_nonnull(two_index);
    fprintf(stderr, "due tracce video: %u frame\n", two_index->n);
    g_assert_cmpint(two_index->n, ==, 120);
    frame_index_free(two_index);
    g_free(two);

    /* Annullata subito: niente indice. */
    gint cancelled = 1;
    g_assert_null(frame_index_scan(vfr, &cancelled));

    /* Framerate costante: nessun indice (la lettura si ferma dopo i primi frame). */
    char *cfr = g_build_filename(dir, "cfr.webm", NULL);
    char *desc = g_strdup_printf("videotestsrc num-buffers=120 ! video/x-raw,width=160,height=120,framerate=25/1 ! videoconvert ! "
                                 "vp8enc ! webmmux ! filesink location=\"%s\"", cfr);
    GstElement *pipeline = gst_parse_launch(desc, NULL);
    g_assert_true(pipeline);
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    GstBus *bus = gst_element_get_bus(pipeline);
    GstMessage *msg = gst_bus_timed_pop_filtered(bus, 30 * GST_SECOND, GST_MESSAGE_EOS | GST_MESSAGE_ERROR);
    g_assert_true(msg && GST_MESSAGE_TYPE(msg) == GST_MESSAGE_EOS);
    gst_message_unref(msg);
    gst_object_unref(bus);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    g_assert_null(frame_index_scan(cfr, NULL));

    /* File inesistente: nessun indice, nessun crash. */
    g_assert_null(frame_index_scan("/nonexistent/video.webm", NULL));

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
    test_empty_index();

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
    g_assert_nonnull(dir);
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
