#ifndef SYNCVIEW_TESTS_VFR_CLIP_H
#define SYNCVIEW_TESTS_VFR_CLIP_H

#include <assert.h>
#include <gst/gst.h>

/*
 * Genera un webm/VP8 a framerate VARIABILE: i timestamp dei frame di videotestsrc sono riscritti con intervalli
 * irregolari (VFR_INTERVALS_MS, ciclici) che il muxer scrive nel file. Servono encoder e muxer base+good.
 */
static const int vfr_intervals_ms[] = { 40, 12, 55, 25, 33 };
#define VFR_INTERVALS_COUNT 5

/* Inizio (ns) del frame numero `i` secondo gli intervalli sopra. */
static gint64
vfr_frame_start_ns(guint i)
{
    gint64 t = 0;

    for (guint k = 0; k < i; k++) {
        t += (gint64)vfr_intervals_ms[k % VFR_INTERVALS_COUNT] * GST_MSECOND;
    }
    return t;
}

static GstPadProbeReturn
vfr_rewrite_timestamps(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{
    (void)pad;
    guint *n = user_data;
    GstBuffer *buffer = gst_buffer_make_writable(GST_PAD_PROBE_INFO_BUFFER(info));

    GST_BUFFER_PTS(buffer) = (GstClockTime)vfr_frame_start_ns(*n);
    GST_BUFFER_DTS(buffer) = GST_BUFFER_PTS(buffer);
    GST_BUFFER_DURATION(buffer) = (GstClockTime)vfr_intervals_ms[*n % VFR_INTERVALS_COUNT] * GST_MSECOND;
    (*n)++;
    GST_PAD_PROBE_INFO_DATA(info) = buffer;
    return GST_PAD_PROBE_OK;
}

/* Ritorna il percorso (g_free) del file creato in `dir`; asserisce se la pipeline non riesce. */
static char *
make_vfr_video(const char *dir, const char *name, int frames)
{
    char *path = g_build_filename(dir, name, NULL);
    char *desc = g_strdup_printf(
        "videotestsrc name=src num-buffers=%d ! video/x-raw,width=160,height=120,framerate=25/1 ! videoconvert ! "
        "vp8enc keyframe-max-dist=10 ! capssetter caps=\"video/x-vp8,framerate=0/1\" replace=false join=true ! "
        "webmmux ! filesink name=out", frames);
    GError *error = NULL;
    GstElement *pipeline = gst_parse_launch(desc, &error);
    assert(pipeline != NULL && error == NULL);

    GstElement *out = gst_bin_get_by_name(GST_BIN(pipeline), "out");
    g_object_set(out, "location", path, NULL);
    gst_object_unref(out);

    guint counter = 0;
    GstElement *src = gst_bin_get_by_name(GST_BIN(pipeline), "src");
    GstPad *pad = gst_element_get_static_pad(src, "src");
    gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, vfr_rewrite_timestamps, &counter, NULL);
    gst_object_unref(pad);
    gst_object_unref(src);

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

#endif /* SYNCVIEW_TESTS_VFR_CLIP_H */
