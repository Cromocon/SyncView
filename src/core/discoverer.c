#include "core/discoverer.h"

#include "core/logger.h"

#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

G_DEFINE_QUARK(discoverer-error-quark, discoverer_error)

void
video_info_free(VideoInfo *info)
{
    if (!info) {
        return;
    }

    g_free(info->path);
    g_free(info->codec);
    g_free(info);
}

VideoInfo *
video_info_default(const char *path)
{
    VideoInfo *info = g_new0(VideoInfo, 1);

    info->path = g_strdup(path);
    info->fps = 0.0;
    info->duration_ms = 0;
    info->width = 0;
    info->height = 0;
    info->codec = g_strdup("unknown");
    return info;
}

gboolean
discoverer_error_is_blocking(DiscovererError code)
{
    switch (code) {
    case DISCOVERER_ERROR_FILE_NOT_FOUND:
    case DISCOVERER_ERROR_URI_INVALID:
    case DISCOVERER_ERROR_MISSING_PLUGINS:
    case DISCOVERER_ERROR_CORRUPT:
        return TRUE;
    case DISCOVERER_ERROR_TIMEOUT:
    case DISCOVERER_ERROR_BUSY:
    case DISCOVERER_ERROR_INTERNAL:
    default:
        return FALSE;
    }
}

char *
discoverer_format_missing_plugins(const char *const *details)
{
    if (!details || !details[0]) {
        return NULL;
    }

    GPtrArray *names = g_ptr_array_new_with_free_func(g_free);

    for (int i = 0; details[i]; i++) {
        /* Formato: "gstreamer|1.0|<app>|<descrizione>|<tipo>-<caps>": la descrizione è il 4° campo. */
        char **fields = g_strsplit(details[i], "|", 5);
        const char *name = (fields[0] && fields[1] && fields[2] && fields[3] && *fields[3])
                               ? fields[3]
                               : details[i];
        g_ptr_array_add(names, g_strdup(name));
        g_strfreev(fields);
    }
    g_ptr_array_add(names, NULL);

    char *joined = g_strjoinv(", ", (char **)names->pdata);
    g_ptr_array_free(names, TRUE);
    return joined;
}

/* --- Frame rate nominale per i file a framerate variabile --- */

static int
compare_int64(gconstpointer a, gconstpointer b)
{
    int64_t x = *(const int64_t *)a;
    int64_t y = *(const int64_t *)b;
    return (x > y) - (x < y);
}

double
discoverer_nominal_fps_from_intervals(const int64_t *intervals_ns, size_t n)
{
    static const double standard[] = { 24000.0 / 1001.0, 24.0, 25.0, 30000.0 / 1001.0, 30.0,
                                       48.0, 50.0, 60000.0 / 1001.0, 60.0, 120.0 };
    int64_t *valid = g_new(int64_t, n ? n : 1);
    size_t count = 0;

    for (size_t i = 0; intervals_ns && i < n; i++) {
        if (intervals_ns[i] > 0) {
            valid[count++] = intervals_ns[i];
        }
    }

    if (count < SYNCVIEW_FPS_MIN_INTERVALS) {
        g_free(valid);
        return 0.0;
    }

    qsort(valid, count, sizeof(int64_t), compare_int64);
    int64_t median = valid[count / 2];

    /*
     * Media degli intervalli entro ±25% della mediana: scarta pause/buchi
     * (outlier) ma, a differenza della sola mediana, recupera la precisione
     * con timestamp arrotondati (Matroska ha risoluzione 1 ms: a 29.97 fps gli
     * intervalli alternano 33/34 ms, la cui media è 33.37 ms).
     */
    double sum = 0.0;
    size_t kept = 0;
    for (size_t i = 0; i < count; i++) {
        if (valid[i] * 4 >= median * 3 && valid[i] * 4 <= median * 5) {
            sum += (double)valid[i];
            kept++;
        }
    }
    g_free(valid);

    double fps = (double)GST_SECOND / (kept > 0 ? sum / (double)kept : (double)median);

    /* Standard più VICINO entro l'1% (non il primo entro la soglia: 30 e 29.97 distano solo 0.1%). */
    double best = 0.0;
    double best_distance = 0.01;
    for (size_t i = 0; i < G_N_ELEMENTS(standard); i++) {
        double distance = fabs(fps - standard[i]) / standard[i];
        if (distance < best_distance) {
            best_distance = distance;
            best = standard[i];
        }
    }
    return best > 0.0 ? best : fps;
}

#define FPS_SAMPLE_BUFFERS 150
#define FPS_ESTIMATE_TIMEOUT_MS 3000

typedef struct {
    GMutex lock;
    GArray *timestamps;  /* GstClockTime: DTS (o PTS se manca) dei buffer video */
    GstElement *pipeline;
} FpsProbe;

static GstPadProbeReturn
fps_buffer_probe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{
    (void)pad;
    FpsProbe *probe = user_data;
    GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);

    if (buffer) {
        GstClockTime t = GST_BUFFER_DTS_IS_VALID(buffer) ? GST_BUFFER_DTS(buffer) : GST_BUFFER_PTS(buffer);

        if (GST_CLOCK_TIME_IS_VALID(t)) {
            g_mutex_lock(&probe->lock);
            g_array_append_val(probe->timestamps, t);
            g_mutex_unlock(&probe->lock);
        }
    }
    return GST_PAD_PROBE_OK;
}

/*
 * Il flusso video va letto all'uscita del demuxer, PRIMA dei parser: alcuni
 * (es. av1parse) riscrivono o eliminano timestamp e durate. Un flusso video
 * elementare si riconosce dalle caps con width (i container, es.
 * video/quicktime, no): per quelli si ferma l'autoplug ed il pad viene esposto
 * così com'è; i container e l'audio proseguono normalmente.
 */
static gboolean
fps_autoplug_continue(GstElement *parsebin, GstPad *pad, GstCaps *caps, gpointer user_data)
{
    (void)parsebin;
    (void)pad;
    (void)user_data;

    if (!caps || gst_caps_get_size(caps) == 0) {
        return TRUE;
    }

    GstStructure *st = gst_caps_get_structure(caps, 0);
    gboolean elementary_video = g_str_has_prefix(gst_structure_get_name(st), "video/")
                                && gst_structure_has_field(st, "width");
    return !elementary_video;
}

static void
fps_pad_added(GstElement *parsebin, GstPad *pad, gpointer user_data)
{
    (void)parsebin;
    FpsProbe *probe = user_data;
    GstCaps *caps = gst_pad_get_current_caps(pad);
    if (!caps) {
        caps = gst_pad_query_caps(pad, NULL);
    }
    gboolean is_video = caps && gst_caps_get_size(caps) > 0
                        && g_str_has_prefix(gst_structure_get_name(gst_caps_get_structure(caps, 0)), "video/");
    if (caps) {
        gst_caps_unref(caps);
    }

    /* Ogni pad va collegato a un sink (anche audio, scartato) per non bloccare il demuxer. */
    GstElement *sink = gst_element_factory_make("fakesink", NULL);
    if (!sink) {
        return;
    }
    g_object_set(sink, "sync", FALSE, "async", FALSE, NULL);
    gst_bin_add(GST_BIN(probe->pipeline), sink);
    gst_element_sync_state_with_parent(sink);

    GstPad *sink_pad = gst_element_get_static_pad(sink, "sink");
    if (is_video) {
        gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, fps_buffer_probe, probe, NULL);
    }
    gst_pad_link(pad, sink_pad);
    gst_object_unref(sink_pad);
}

double
discoverer_estimate_fps(const char *path)
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
        return 0.0;
    }

    FpsProbe probe = { .pipeline = pipeline };
    g_mutex_init(&probe.lock);
    probe.timestamps = g_array_new(FALSE, FALSE, sizeof(GstClockTime));

    g_object_set(src, "location", path, NULL);
    gst_bin_add_many(GST_BIN(pipeline), src, parsebin, NULL);
    gst_element_link(src, parsebin);
    g_signal_connect(parsebin, "autoplug-continue", G_CALLBACK(fps_autoplug_continue), NULL);
    g_signal_connect(parsebin, "pad-added", G_CALLBACK(fps_pad_added), &probe);

    double fps = 0.0;
    GstBus *bus = gst_element_get_bus(pipeline);

    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE) {
        gint64 deadline = g_get_monotonic_time() + (gint64)FPS_ESTIMATE_TIMEOUT_MS * 1000;
        gboolean done = FALSE;

        while (!done && g_get_monotonic_time() < deadline) {
            GstMessage *msg = gst_bus_timed_pop_filtered(bus, 20 * GST_MSECOND,
                                                         GST_MESSAGE_EOS | GST_MESSAGE_ERROR);
            if (msg) {
                done = TRUE;
                gst_message_unref(msg);
            }

            g_mutex_lock(&probe.lock);
            if (probe.timestamps->len >= FPS_SAMPLE_BUFFERS) {
                done = TRUE;
            }
            g_mutex_unlock(&probe.lock);
        }
    }

    gst_element_set_state(pipeline, GST_STATE_NULL);  /* dopo questo nessun probe è più in corso */

    guint n = probe.timestamps->len;
    if (n > 1) {
        int64_t *intervals = g_new(int64_t, n - 1);
        for (guint i = 0; i + 1 < n; i++) {
            intervals[i] = (int64_t)g_array_index(probe.timestamps, GstClockTime, i + 1)
                           - (int64_t)g_array_index(probe.timestamps, GstClockTime, i);
        }
        fps = discoverer_nominal_fps_from_intervals(intervals, n - 1);
        g_free(intervals);
    }
    log_gst("discoverer: stima fps da %u timestamp di %s -> %.4f", n, path, fps);

    g_array_free(probe.timestamps, TRUE);
    g_mutex_clear(&probe.lock);
    gst_object_unref(bus);
    gst_object_unref(pipeline);
    return fps;
}

/* "video/x-h264" -> "h264", "video/x-vp8" -> "vp8", "video/mpeg" -> "mpeg"; NULL se caps assenti. */
static char *
codec_name_from_caps(GstCaps *caps)
{
    if (!caps || gst_caps_is_empty(caps) || gst_caps_get_size(caps) == 0) {
        return NULL;
    }

    const char *name = gst_structure_get_name(gst_caps_get_structure(caps, 0));
    if (g_str_has_prefix(name, "video/x-")) {
        name += strlen("video/x-");
    } else if (g_str_has_prefix(name, "video/")) {
        name += strlen("video/");
    }
    return g_strdup(name);
}

static VideoInfo *
info_from_discoverer(const char *path, GstDiscovererInfo *dinfo)
{
    VideoInfo *info = video_info_default(path);

    GstClockTime duration = gst_discoverer_info_get_duration(dinfo);
    if (duration != GST_CLOCK_TIME_NONE) {
        info->duration_ms = (int64_t)GST_TIME_AS_MSECONDS(duration);
    }

    /* Solo il primo stream video, come `-select_streams v:0`. */
    GList *streams = gst_discoverer_info_get_video_streams(dinfo);
    if (streams) {
        GstDiscovererVideoInfo *video = GST_DISCOVERER_VIDEO_INFO(streams->data);
        guint num = gst_discoverer_video_info_get_framerate_num(video);
        guint den = gst_discoverer_video_info_get_framerate_denom(video);

        if (den > 0 && num > 0) {  /* come l'originale: fps resta 0.0 se den <= 0 */
            info->fps = (double)num / (double)den;
        } else {
            /* Framerate non dichiarato (0/1, file a frame rate variabile): ffprobe avrebbe dato r_frame_rate. */
            info->fps = discoverer_estimate_fps(path);
        }
        info->width = (int)gst_discoverer_video_info_get_width(video);
        info->height = (int)gst_discoverer_video_info_get_height(video);

        GstCaps *caps = gst_discoverer_stream_info_get_caps(GST_DISCOVERER_STREAM_INFO(video));
        char *codec = codec_name_from_caps(caps);
        if (codec) {
            g_free(info->codec);
            info->codec = codec;
        }
        if (caps) {
            gst_caps_unref(caps);
        }

        log_gst("discoverer: stream video 0 di %s: %ux%u, framerate %u/%u, codec %s", path,
                gst_discoverer_video_info_get_width(video),
                gst_discoverer_video_info_get_height(video), num, den, info->codec);
    } else {
        log_gst("discoverer: nessuno stream video in %s, info di default", path);
    }

    gst_discoverer_stream_info_list_free(streams);
    return info;
}

VideoInfo *
discoverer_probe_file(const char *path, int timeout_ms, GError **error)
{
    if (!path || !g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
        g_set_error_literal(error, DISCOVERER_ERROR, DISCOVERER_ERROR_FILE_NOT_FOUND,
                            "File non trovato");
        return NULL;
    }

    if (!gst_is_initialized()) {
        gst_init(NULL, NULL);
    }

    GFile *file = g_file_new_for_path(path);
    char *uri = g_file_get_uri(file);
    g_object_unref(file);
    if (!uri) {
        g_set_error(error, DISCOVERER_ERROR, DISCOVERER_ERROR_URI_INVALID,
                    "Percorso non convertibile in URI: %s", path);
        return NULL;
    }

    /* <= 0 = default; GstDiscoverer accetta solo timeout tra 1 s e 3600 s (fuori range: CRITICAL di GObject). */
    if (timeout_ms <= 0) {
        timeout_ms = SYNCVIEW_DISCOVERER_TIMEOUT_MS;
    }
    timeout_ms = CLAMP(timeout_ms, SYNCVIEW_DISCOVERER_MIN_TIMEOUT_MS, SYNCVIEW_DISCOVERER_MAX_TIMEOUT_MS);

    GError *gst_error = NULL;
    GstDiscoverer *discoverer = gst_discoverer_new((GstClockTime)timeout_ms * GST_MSECOND, &gst_error);
    if (!discoverer) {
        g_set_error(error, DISCOVERER_ERROR, DISCOVERER_ERROR_INTERNAL,
                    "Impossibile creare il discoverer: %s", gst_error ? gst_error->message : "errore sconosciuto");
        g_clear_error(&gst_error);
        g_free(uri);
        return NULL;
    }

    log_gst("discoverer: probing di %s (timeout %dms)", uri, timeout_ms);
    GstDiscovererInfo *dinfo = gst_discoverer_discover_uri(discoverer, uri, &gst_error);
    VideoInfo *info = NULL;

    if (!dinfo) {
        g_set_error(error, DISCOVERER_ERROR, DISCOVERER_ERROR_INTERNAL, "Probing fallito: %s",
                    gst_error ? gst_error->message : "errore sconosciuto");
    } else {
        GstDiscovererResult result = gst_discoverer_info_get_result(dinfo);
        log_gst("discoverer: risultato %d per %s", (int)result, uri);

        switch (result) {
        case GST_DISCOVERER_OK:
            info = info_from_discoverer(path, dinfo);
            break;
        case GST_DISCOVERER_MISSING_PLUGINS: {
            char *missing = discoverer_format_missing_plugins(
                (const char *const *)gst_discoverer_info_get_missing_elements_installer_details(dinfo));
            g_set_error(error, DISCOVERER_ERROR, DISCOVERER_ERROR_MISSING_PLUGINS,
                        "Plugin GStreamer mancanti: %s", missing ? missing : "(non specificati)");
            g_free(missing);
            break;
        }
        case GST_DISCOVERER_TIMEOUT:
            g_set_error(error, DISCOVERER_ERROR, DISCOVERER_ERROR_TIMEOUT,
                        "Timeout durante l'analisi del file (%d ms)", timeout_ms);
            break;
        case GST_DISCOVERER_BUSY:
            g_set_error_literal(error, DISCOVERER_ERROR, DISCOVERER_ERROR_BUSY, "Discoverer occupato");
            break;
        case GST_DISCOVERER_URI_INVALID:
            g_set_error(error, DISCOVERER_ERROR, DISCOVERER_ERROR_URI_INVALID, "URI non valido: %s", uri);
            break;
        case GST_DISCOVERER_ERROR:
        default:
            g_set_error(error, DISCOVERER_ERROR, DISCOVERER_ERROR_CORRUPT,
                        "File corrotto o formato non riconosciuto: %s",
                        gst_error ? gst_error->message : "errore sconosciuto");
            break;
        }
        g_object_unref(dinfo);
    }

    g_clear_error(&gst_error);
    g_object_unref(discoverer);
    g_free(uri);
    return info;
}

VideoInfo *
discoverer_probe_file_with_fallback(const char *path, int timeout_ms, GError **error)
{
    GError *local = NULL;
    VideoInfo *info = discoverer_probe_file(path, timeout_ms, &local);

    if (info) {
        return info;
    }

    if (discoverer_error_is_blocking(local->code)) {
        g_propagate_error(error, local);
        return NULL;
    }

    /* Come l'originale: errore di probing non bloccante -> log_error e info di default. */
    char *name = g_path_get_basename(path);
    char *message = g_strdup_printf("Errore probing %s", name);
    log_error(message, local);
    g_free(message);
    g_free(name);
    g_error_free(local);

    return video_info_default(path);
}

typedef struct {
    char *path;
    int timeout_ms;
} ProbeTaskData;

static void
probe_task_data_free(ProbeTaskData *data)
{
    g_free(data->path);
    g_free(data);
}

static void
probe_thread(GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable)
{
    (void)source_object;
    (void)cancellable;
    ProbeTaskData *data = task_data;
    GError *error = NULL;

    VideoInfo *info = discoverer_probe_file_with_fallback(data->path, data->timeout_ms, &error);
    if (info) {
        g_task_return_pointer(task, info, (GDestroyNotify)video_info_free);
    } else {
        g_task_return_error(task, error);
    }
}

void
discoverer_probe_async(const char *path, int timeout_ms, GCancellable *cancellable,
                       GAsyncReadyCallback callback, gpointer user_data)
{
    GTask *task = g_task_new(NULL, cancellable, callback, user_data);
    ProbeTaskData *data = g_new0(ProbeTaskData, 1);

    data->path = g_strdup(path);
    data->timeout_ms = timeout_ms;
    g_task_set_task_data(task, data, (GDestroyNotify)probe_task_data_free);
    g_task_set_return_on_cancel(task, TRUE);  /* annullando, la callback parte subito con CANCELLED */

    log_gst("discoverer: probing asincrono avviato per %s", path);
    g_task_run_in_thread(task, probe_thread);
    g_object_unref(task);
}

VideoInfo *
discoverer_probe_finish(GAsyncResult *result, GError **error)
{
    return g_task_propagate_pointer(G_TASK(result), error);
}
