#include "core/frame_index.h"

#include "core/logger.h"

#include <gst/gst.h>
#include <stdlib.h>
#include <string.h>

static int
compare_gint64(const void *a, const void *b)
{
    gint64 x = *(const gint64 *)a, y = *(const gint64 *)b;
    return (x > y) - (x < y);
}

FrameIndex *
frame_index_new(gint64 *pts, guint n)
{
    FrameIndex *index = g_new0(FrameIndex, 1);

    qsort(pts, n, sizeof(gint64), compare_gint64);
    guint kept = 0;
    for (guint i = 0; i < n; i++) {
        if (kept == 0 || pts[i] > pts[kept - 1]) {
            pts[kept++] = pts[i];
        }
    }
    index->pts = pts;
    index->n = kept;
    index->last_end_ns = kept >= 2 ? pts[kept - 1] + (pts[kept - 1] - pts[kept - 2]) : (kept == 1 ? pts[0] : 0);
    return index;
}

void
frame_index_free(FrameIndex *index)
{
    if (index) {
        g_free(index->pts);
        g_free(index);
    }
}

guint
frame_index_find(const FrameIndex *index, gint64 t_ns)
{
    guint lo = 0, hi = index->n;  /* primo indice con pts > t */

    while (lo < hi) {
        guint mid = lo + (hi - lo) / 2;
        if (index->pts[mid] <= t_ns) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo > 0 ? lo - 1 : 0;
}

gint64
frame_index_start(const FrameIndex *index, guint i)
{
    if (index->n == 0) {
        return 0;  /* indice vuoto: niente pts[n - 1] con n == 0 */
    }
    return index->pts[MIN(i, index->n - 1)];
}

gint64
frame_index_end(const FrameIndex *index, guint i)
{
    return i + 1 < index->n ? index->pts[i + 1] : index->last_end_ns;
}

gboolean
frame_index_timestamps_irregular(const gint64 *pts, guint n)
{
    if (n < FRAME_INDEX_MIN_FRAMES) {
        return FALSE;
    }

    gint64 *sorted = g_memdup2(pts, n * sizeof(gint64));
    qsort(sorted, n, sizeof(gint64), compare_gint64);

    guint count = 0;
    gint64 *intervals = g_new(gint64, n);
    for (guint i = 0; i + 1 < n; i++) {
        gint64 d = sorted[i + 1] - sorted[i];
        if (d > 0) {
            intervals[count++] = d;
        }
    }
    g_free(sorted);
    if (count < FRAME_INDEX_MIN_FRAMES - 1) {
        g_free(intervals);
        return FALSE;
    }

    qsort(intervals, count, sizeof(gint64), compare_gint64);
    gint64 median = intervals[count / 2];
    guint outliers = 0;
    for (guint i = 0; i < count; i++) {
        gint64 deviation = llabs(intervals[i] - median);
        if (deviation > median / 5 && deviation > 2 * GST_MSECOND) {
            outliers++;
        }
    }
    g_free(intervals);
    return outliers * 100 > count * 3;
}

/* --- Lettura dei timestamp (stesso schema di discoverer_estimate_fps: parsebin senza decodifica) --- */

typedef struct {
    GMutex lock;
    GArray *pts;  /* gint64: stream time dei buffer video */
    GstSegment segment;
    gboolean have_segment;
    gboolean video_probed;  /* il probe è già su un pad video: gli altri non contano (stesso array) */
    GstElement *pipeline;
} ScanProbe;

static GstPadProbeReturn
scan_pad_probe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{
    (void)pad;
    ScanProbe *probe = user_data;

    g_mutex_lock(&probe->lock);
    if (info->type & GST_PAD_PROBE_TYPE_BUFFER) {
        GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);
        if (buffer && GST_BUFFER_PTS_IS_VALID(buffer)) {
            guint64 t = probe->have_segment
                            ? gst_segment_to_stream_time(&probe->segment, GST_FORMAT_TIME, GST_BUFFER_PTS(buffer))
                            : GST_BUFFER_PTS(buffer);
            if (GST_CLOCK_TIME_IS_VALID(t)) {
                gint64 value = (gint64)t;
                g_array_append_val(probe->pts, value);
            }
        }
    } else if ((info->type & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) && GST_EVENT_TYPE(GST_PAD_PROBE_INFO_EVENT(info)) == GST_EVENT_SEGMENT) {
        const GstSegment *segment;
        gst_event_parse_segment(GST_PAD_PROBE_INFO_EVENT(info), &segment);
        if (segment->format == GST_FORMAT_TIME) {
            gst_segment_copy_into(segment, &probe->segment);
            probe->have_segment = TRUE;
        }
    }
    g_mutex_unlock(&probe->lock);
    return GST_PAD_PROBE_OK;
}

/* Il video va letto all'uscita del demuxer, prima dei parser (alcuni riscrivono i timestamp): vedi discoverer.c. */
static gboolean
scan_autoplug_continue(GstElement *parsebin, GstPad *pad, GstCaps *caps, gpointer user_data)
{
    (void)parsebin;
    (void)pad;
    (void)user_data;

    if (!caps || gst_caps_get_size(caps) == 0) {
        return TRUE;
    }
    GstStructure *st = gst_caps_get_structure(caps, 0);
    gboolean elementary_video = g_str_has_prefix(gst_structure_get_name(st), "video/") && gst_structure_has_field(st, "width");
    return !elementary_video;
}

static void
scan_pad_added(GstElement *parsebin, GstPad *pad, gpointer user_data)
{
    (void)parsebin;
    ScanProbe *probe = user_data;
    GstCaps *caps = gst_pad_get_current_caps(pad);
    if (!caps) {
        caps = gst_pad_query_caps(pad, NULL);
    }
    gboolean is_video = caps && gst_caps_get_size(caps) > 0
                        && g_str_has_prefix(gst_structure_get_name(gst_caps_get_structure(caps, 0)), "video/");
    if (caps) {
        gst_caps_unref(caps);
    }

    GstElement *sink = gst_element_factory_make("fakesink", NULL);
    if (!sink) {
        return;
    }
    g_object_set(sink, "sync", FALSE, "async", FALSE, NULL);
    gst_bin_add(GST_BIN(probe->pipeline), sink);
    gst_element_sync_state_with_parent(sink);

    GstPad *sink_pad = gst_element_get_static_pad(sink, "sink");
    /* Solo il primo stream video (come il discoverer): più probe sullo stesso array mescolerebbero i timestamp. */
    g_mutex_lock(&probe->lock);
    gboolean first_video = is_video && !probe->video_probed;
    if (first_video) {
        probe->video_probed = TRUE;
    }
    g_mutex_unlock(&probe->lock);
    if (first_video) {
        gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, scan_pad_probe, probe, NULL);
    }
    gst_pad_link(pad, sink_pad);
    gst_object_unref(sink_pad);
}

FrameIndex *
frame_index_scan(const char *path, const gint *cancelled)
{
    if (!gst_is_initialized()) {
        gst_init(NULL, NULL);
    }

    GstElement *pipeline = gst_pipeline_new(NULL);
    GstElement *src = gst_element_factory_make("filesrc", NULL);
    GstElement *parsebin = gst_element_factory_make("parsebin", NULL);
    if (!pipeline || !src || !parsebin) {
        g_clear_object(&pipeline);
        g_clear_object(&src);
        g_clear_object(&parsebin);
        return NULL;
    }

    ScanProbe probe = { .pipeline = pipeline };
    g_mutex_init(&probe.lock);
    probe.pts = g_array_new(FALSE, FALSE, sizeof(gint64));
    gst_segment_init(&probe.segment, GST_FORMAT_TIME);

    g_object_set(src, "location", path, NULL);
    gst_bin_add_many(GST_BIN(pipeline), src, parsebin, NULL);
    gst_element_link(src, parsebin);
    g_signal_connect(parsebin, "autoplug-continue", G_CALLBACK(scan_autoplug_continue), NULL);
    g_signal_connect(parsebin, "pad-added", G_CALLBACK(scan_pad_added), &probe);

    GstBus *bus = gst_element_get_bus(pipeline);
    gboolean irregular = FALSE, decided = FALSE, finished = FALSE, got_eos = FALSE, aborted = FALSE;

    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE) {
        gint64 deadline = g_get_monotonic_time() + (gint64)FRAME_INDEX_SCAN_TIMEOUT_S * G_USEC_PER_SEC;

        while (!finished && !aborted && g_get_monotonic_time() < deadline) {
            GstMessage *msg = gst_bus_timed_pop_filtered(bus, 10 * GST_MSECOND, GST_MESSAGE_EOS | GST_MESSAGE_ERROR);
            if (msg) {
                finished = TRUE;
                if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_EOS) {
                    got_eos = TRUE;
                } else {
                    GError *err = NULL;
                    gst_message_parse_error(msg, &err, NULL);
                    log_gst("frame index: errore leggendo %s: %s", path, err ? err->message : "?");
                    g_clear_error(&err);
                }
                gst_message_unref(msg);
            }
            if (cancelled && g_atomic_int_get(cancelled)) {
                aborted = TRUE;
            }
            if (!decided) {
                g_mutex_lock(&probe.lock);
                if (probe.pts->len >= FRAME_INDEX_SAMPLE_FRAMES || finished) {
                    irregular = frame_index_timestamps_irregular((gint64 *)probe.pts->data, probe.pts->len);
                    decided = TRUE;
                }
                g_mutex_unlock(&probe.lock);
                if (decided && !irregular) {
                    break;  /* framerate costante: niente indice */
                }
            }
        }
    }

    gst_element_set_state(pipeline, GST_STATE_NULL);  /* dopo questo nessun probe è più in corso */

    FrameIndex *index = NULL;
    if (irregular && !aborted && got_eos) {
        guint n = probe.pts->len;
        gint64 *copy = g_memdup2(probe.pts->data, n * sizeof(gint64));
        index = frame_index_new(copy, n);
        log_gst("frame index: %u frame letti da %s (framerate variabile)", index->n, path);
    } else if (irregular && !aborted) {
        log_gst("frame index: lettura di %s incompleta (errore o tempo scaduto), nessun indice", path);
    }

    g_array_free(probe.pts, TRUE);
    g_mutex_clear(&probe.lock);
    gst_object_unref(bus);
    gst_object_unref(pipeline);
    return index;
}
