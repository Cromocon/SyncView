/*
 * M2.2: core/discoverer. I file di prova sono generati a runtime con
 * pipeline GStreamer (webm/VP8 e ogg/Theora+Vorbis: plugin base+good); se
 * mancano i plugin il test termina con 77 (skip Meson).
 */
#include "core/discoverer.h"

#include <assert.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gst/gst.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SKIP_EXIT 77

static gboolean
have_elements(const char *const *names)
{
    for (int i = 0; names[i]; i++) {
        GstElementFactory *f = gst_element_factory_find(names[i]);
        if (!f) {
            return FALSE;
        }
        gst_object_unref(f);
    }
    return TRUE;
}

/* Esegue una pipeline fino a EOS; ASSERT se fallisce. */
static void
run_pipeline(const char *description)
{
    GError *error = NULL;
    GstElement *pipeline = gst_parse_launch(description, &error);
    assert(pipeline != NULL && error == NULL);

    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    GstBus *bus = gst_element_get_bus(pipeline);
    GstMessage *msg = gst_bus_timed_pop_filtered(bus, 30 * GST_SECOND, GST_MESSAGE_EOS | GST_MESSAGE_ERROR);
    assert(msg != NULL && GST_MESSAGE_TYPE(msg) == GST_MESSAGE_EOS);

    gst_message_unref(msg);
    gst_object_unref(bus);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
}

static char *
make_video(const char *dir, const char *name, int width, int height, const char *framerate, int frames)
{
    char *path = g_build_filename(dir, name, NULL);
    char *desc = g_strdup_printf(
        "videotestsrc num-buffers=%d ! video/x-raw,width=%d,height=%d,framerate=%s ! videoconvert ! "
        "vp8enc ! webmmux ! filesink location=\"%s\"",
        frames, width, height, framerate, path);
    run_pipeline(desc);
    g_free(desc);
    return path;
}

static void
test_video_info(const char *dir)
{
    /* 50 frame a 25 fps = 2 s, 320x240 */
    char *path = make_video(dir, "v25.webm", 320, 240, "25/1", 50);
    GError *error = NULL;

    VideoInfo *info = discoverer_probe_file(path, 0, &error);
    assert(info != NULL && error == NULL);
    assert(strcmp(info->path, path) == 0);
    assert(info->width == 320 && info->height == 240);
    assert(fabs(info->fps - 25.0) < 1e-9);
    assert(info->duration_ms >= 1900 && info->duration_ms <= 2200);
    assert(strcmp(info->codec, "vp8") == 0);
    video_info_free(info);

    /* Framerate frazionario 30000/1001 -> 29.97, come num/den di ffprobe. */
    char *ntsc = make_video(dir, "v2997.webm", 640, 360, "30000/1001", 30);
    info = discoverer_probe_file(ntsc, 0, &error);
    assert(info != NULL);
    assert(info->width == 640 && info->height == 360);
    assert(fabs(info->fps - 30000.0 / 1001.0) < 1e-9);
    video_info_free(info);

    /* La versione con fallback dà lo stesso risultato sui file buoni. */
    info = discoverer_probe_file_with_fallback(path, 0, &error);
    assert(info != NULL && info->width == 320);
    video_info_free(info);

    g_free(path);
    g_free(ntsc);
}

static void
test_audio_only_gives_defaults(const char *dir)
{
    const char *needed[] = { "audiotestsrc", "vorbisenc", "oggmux", "filesink", NULL };
    if (!have_elements(needed)) {
        return;
    }

    char *path = g_build_filename(dir, "audio.ogg", NULL);
    char *desc = g_strdup_printf("audiotestsrc num-buffers=20 ! audioconvert ! vorbisenc ! oggmux ! filesink location=\"%s\"", path);
    run_pipeline(desc);
    g_free(desc);

    /* Senza stream video non è un errore: valori di default, come ffprobe senza stream. */
    VideoInfo *info = discoverer_probe_file(path, 0, NULL);
    assert(info != NULL);
    assert(info->fps == 0.0 && info->width == 0 && info->height == 0);
    assert(strcmp(info->codec, "unknown") == 0);
    video_info_free(info);

    g_free(path);
}

static void
test_file_not_found(const char *dir)
{
    char *missing = g_build_filename(dir, "non_esiste.mp4", NULL);
    GError *error = NULL;

    assert(discoverer_probe_file(missing, 0, &error) == NULL);
    assert(error->domain == DISCOVERER_ERROR && error->code == DISCOVERER_ERROR_FILE_NOT_FOUND);
    assert(strcmp(error->message, "File non trovato") == 0);
    g_clear_error(&error);

    /* Bloccante anche con il fallback. */
    assert(discoverer_probe_file_with_fallback(missing, 0, &error) == NULL);
    assert(error->code == DISCOVERER_ERROR_FILE_NOT_FOUND);
    g_clear_error(&error);

    /* Una directory e NULL non sono file regolari. */
    assert(discoverer_probe_file(dir, 0, &error) == NULL && error->code == DISCOVERER_ERROR_FILE_NOT_FOUND);
    g_clear_error(&error);
    assert(discoverer_probe_file(NULL, 0, &error) == NULL && error->code == DISCOVERER_ERROR_FILE_NOT_FOUND);
    g_clear_error(&error);

    g_free(missing);
}

static void
test_corrupt_file(const char *dir)
{
    char *garbage = g_build_filename(dir, "garbage.webm", NULL);
    char data[4096];
    for (size_t i = 0; i < sizeof(data); i++) {
        data[i] = (char)((i * 131 + 17) % 251);
    }
    assert(g_file_set_contents(garbage, data, sizeof(data), NULL));

    GError *error = NULL;
    assert(discoverer_probe_file(garbage, 0, &error) == NULL);
    assert(error->domain == DISCOVERER_ERROR && error->code == DISCOVERER_ERROR_CORRUPT);
    assert(strstr(error->message, "corrotto") != NULL);
    g_clear_error(&error);

    /* Bloccante: niente fallback silenzioso. */
    assert(discoverer_probe_file_with_fallback(garbage, 0, &error) == NULL);
    assert(error->code == DISCOVERER_ERROR_CORRUPT);
    g_clear_error(&error);

    /* File vuoto. */
    char *empty = g_build_filename(dir, "empty.mp4", NULL);
    assert(g_file_set_contents(empty, "", 0, NULL));
    assert(discoverer_probe_file(empty, 0, &error) == NULL);
    assert(discoverer_error_is_blocking((DiscovererError)error->code));
    g_clear_error(&error);

    g_free(garbage);
    g_free(empty);
}

static void
test_truncated_file(const char *dir)
{
    char *path = make_video(dir, "full.webm", 320, 240, "25/1", 50);
    char *content = NULL;
    gsize len = 0;
    assert(g_file_get_contents(path, &content, &len, NULL));

    /* Solo l'intestazione e poco altro: o riconosciuto con meno dati, o errore bloccante/specifico. */
    char *cut = g_build_filename(dir, "cut.webm", NULL);
    assert(g_file_set_contents(cut, content, (gssize)(len / 20), NULL));

    GError *error = NULL;
    VideoInfo *info = discoverer_probe_file(cut, 0, &error);
    if (info) {
        assert(info->width == 320);  /* intestazione intatta: metadati comunque letti */
        video_info_free(info);
    } else {
        assert(error->domain == DISCOVERER_ERROR);
        g_clear_error(&error);
    }

    g_free(content);
    g_free(path);
    g_free(cut);
}

static void
test_error_classification(void)
{
    assert(discoverer_error_is_blocking(DISCOVERER_ERROR_FILE_NOT_FOUND));
    assert(discoverer_error_is_blocking(DISCOVERER_ERROR_URI_INVALID));
    assert(discoverer_error_is_blocking(DISCOVERER_ERROR_MISSING_PLUGINS));
    assert(discoverer_error_is_blocking(DISCOVERER_ERROR_CORRUPT));
    assert(!discoverer_error_is_blocking(DISCOVERER_ERROR_TIMEOUT));
    assert(!discoverer_error_is_blocking(DISCOVERER_ERROR_BUSY));
    assert(!discoverer_error_is_blocking(DISCOVERER_ERROR_INTERNAL));
}

static void
test_missing_plugins_message(void)
{
    assert(discoverer_format_missing_plugins(NULL) == NULL);
    const char *const none[] = { NULL };
    assert(discoverer_format_missing_plugins(none) == NULL);

    /* Formato reale degli installer details: il 4° campo è la descrizione leggibile. */
    const char *const one[] = {
        "gstreamer|1.0|gst-discoverer-1.0|H.265 / HEVC decoder|decoder-video/x-h265, stream-format=(string)byte-stream",
        NULL
    };
    char *msg = discoverer_format_missing_plugins(one);
    assert(msg && strcmp(msg, "H.265 / HEVC decoder") == 0);
    g_free(msg);

    const char *const two[] = {
        "gstreamer|1.0|app|H.265 decoder|decoder-video/x-h265",
        "gstreamer|1.0|app|AAC decoder|decoder-audio/mpeg, mpegversion=(int)4",
        NULL
    };
    msg = discoverer_format_missing_plugins(two);
    assert(msg && strcmp(msg, "H.265 decoder, AAC decoder") == 0);
    g_free(msg);

    /* Stringa non nel formato atteso: la si mostra così com'è, senza crash. */
    const char *const odd[] = { "formato-strano", NULL };
    msg = discoverer_format_missing_plugins(odd);
    assert(msg && strcmp(msg, "formato-strano") == 0);
    g_free(msg);
}

static void
test_timeout_does_not_crash(const char *dir)
{
    char *path = make_video(dir, "t.webm", 320, 240, "25/1", 25);
    GError *error = NULL;

    /* Timeout minuscolo/negativo: limitato a 1 s (minimo di GstDiscoverer) senza CRITICAL di GObject
     * (fatali in questo test). Su un file piccolo il probing riesce comunque. */
    int tiny[] = { 1, -5, 999, 1000, 99999999 };
    for (size_t i = 0; i < G_N_ELEMENTS(tiny); i++) {
        VideoInfo *probed = discoverer_probe_file(path, tiny[i], &error);
        if (probed) {
            video_info_free(probed);
        } else {
            assert(error->domain == DISCOVERER_ERROR);
            g_clear_error(&error);
        }
    }

    /* Con fallback, un timeout (non bloccante) restituisce sempre info utilizzabili. */
    VideoInfo *info = discoverer_probe_file_with_fallback(path, 1, &error);
    assert(info != NULL && error == NULL);
    assert(strcmp(info->path, path) == 0);
    video_info_free(info);

    g_free(path);
}

static void
fill(int64_t *a, size_t n, int64_t value)
{
    for (size_t i = 0; i < n; i++) {
        a[i] = value;
    }
}

static void
test_nominal_fps_from_intervals(void)
{
    int64_t iv[100];

    /* Cadenze standard: agganciate al valore nominale. */
    fill(iv, 100, 40000000);   assert(discoverer_nominal_fps_from_intervals(iv, 100) == 25.0);
    fill(iv, 100, 41708333);   assert(fabs(discoverer_nominal_fps_from_intervals(iv, 100) - 24000.0 / 1001.0) < 1e-9);
    fill(iv, 100, 41666667);   assert(discoverer_nominal_fps_from_intervals(iv, 100) == 24.0);
    fill(iv, 100, 20000000);   assert(discoverer_nominal_fps_from_intervals(iv, 100) == 50.0);

    /* 30 e 29.97 distano solo 0.1%: deve vincere il più vicino, non il primo entro la soglia. */
    fill(iv, 100, 33333333);   assert(discoverer_nominal_fps_from_intervals(iv, 100) == 30.0);
    fill(iv, 100, 33366700);   assert(fabs(discoverer_nominal_fps_from_intervals(iv, 100) - 30000.0 / 1001.0) < 1e-9);
    fill(iv, 100, 16683333);   assert(fabs(discoverer_nominal_fps_from_intervals(iv, 100) - 60000.0 / 1001.0) < 1e-9);

    /* Frame rate non standard: valore grezzo (1/0.0571428 s = 17.5). */
    fill(iv, 100, 57142857);
    double raw = discoverer_nominal_fps_from_intervals(iv, 100);
    assert(fabs(raw - 17.5) < 1e-3);

    /* Mediana: robusta a outlier (pause, buchi) anche numerosi ma minoritari. */
    fill(iv, 100, 33333333);
    for (int i = 0; i < 30; i++) iv[i * 3] = 500000000;
    assert(discoverer_nominal_fps_from_intervals(iv, 100) == 30.0);

    /* Intervalli <= 0 (timestamp duplicati/non crescenti) ignorati. */
    fill(iv, 100, 40000000);
    for (int i = 0; i < 40; i++) iv[i] = (i % 2) ? 0 : -5;
    assert(discoverer_nominal_fps_from_intervals(iv, 100) == 25.0);

    /* Troppo pochi dati validi -> sconosciuto. */
    fill(iv, 100, 40000000);
    assert(discoverer_nominal_fps_from_intervals(iv, SYNCVIEW_FPS_MIN_INTERVALS - 1) == 0.0);
    assert(discoverer_nominal_fps_from_intervals(iv, SYNCVIEW_FPS_MIN_INTERVALS) == 25.0);
    fill(iv, 100, 0);
    assert(discoverer_nominal_fps_from_intervals(iv, 100) == 0.0);
    assert(discoverer_nominal_fps_from_intervals(NULL, 0) == 0.0);
}

static void
test_estimate_fps_from_file(const char *dir)
{
    /* Stima dai timestamp reali del demuxer, su file a frame rate costante noto. */
    char *p25 = make_video(dir, "e25.webm", 320, 240, "25/1", 100);
    assert(discoverer_estimate_fps(p25) == 25.0);

    char *p30 = make_video(dir, "e30.webm", 320, 240, "30000/1001", 100);
    assert(fabs(discoverer_estimate_fps(p30) - 30000.0 / 1001.0) < 1e-9);

    /* File inesistente / non video: 0.0, senza crash né blocchi. */
    assert(discoverer_estimate_fps("/non/esiste.webm") == 0.0);
    char *garbage = g_build_filename(dir, "estimate_garbage.bin", NULL);
    assert(g_file_set_contents(garbage, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", -1, NULL));
    assert(discoverer_estimate_fps(garbage) == 0.0);

    g_free(p25);
    g_free(p30);
    g_free(garbage);
}

static void
test_default_info(void)
{
    VideoInfo *info = video_info_default("/x/y.mp4");
    assert(strcmp(info->path, "/x/y.mp4") == 0);
    assert(info->fps == 0.0 && info->duration_ms == 0 && info->width == 0 && info->height == 0);
    assert(strcmp(info->codec, "unknown") == 0);
    video_info_free(info);
    video_info_free(NULL);
}

/* --- asincrono --- */
typedef struct {
    GMainLoop *loop;
    VideoInfo *info;
    GError *error;
    int calls;
} AsyncResult;

static void
on_probe_done(GObject *source, GAsyncResult *result, gpointer user_data)
{
    (void)source;
    AsyncResult *r = user_data;

    r->info = discoverer_probe_finish(result, &r->error);
    r->calls++;
    g_main_loop_quit(r->loop);
}

static gboolean
loop_timeout(gpointer loop)
{
    g_main_loop_quit(loop);
    return G_SOURCE_REMOVE;
}

static void
run_async(const char *path, GCancellable *cancellable, AsyncResult *r, gboolean cancel_immediately)
{
    r->loop = g_main_loop_new(NULL, FALSE);
    r->info = NULL;
    r->error = NULL;
    r->calls = 0;

    discoverer_probe_async(path, 0, cancellable, on_probe_done, r);
    if (cancel_immediately) {
        g_cancellable_cancel(cancellable);
    }
    guint timeout = g_timeout_add_seconds(30, loop_timeout, r->loop);
    g_main_loop_run(r->loop);
    g_source_remove(timeout);
    g_main_loop_unref(r->loop);
}

static void
test_async(const char *dir)
{
    char *path = make_video(dir, "async.webm", 320, 240, "25/1", 25);
    AsyncResult r;

    /* Successo: callback nel main context, info corrette. */
    run_async(path, NULL, &r, FALSE);
    assert(r.calls == 1 && r.info != NULL && r.error == NULL);
    assert(r.info->width == 320 && fabs(r.info->fps - 25.0) < 1e-9);
    video_info_free(r.info);

    /* File assente: errore bloccante propagato dal thread. */
    char *missing = g_build_filename(dir, "manca.webm", NULL);
    run_async(missing, NULL, &r, FALSE);
    assert(r.calls == 1 && r.info == NULL);
    assert(r.error->domain == DISCOVERER_ERROR && r.error->code == DISCOVERER_ERROR_FILE_NOT_FOUND);
    g_clear_error(&r.error);

    /* Annullamento: la callback riceve CANCELLED (una sola volta) e nessuna info. */
    GCancellable *cancellable = g_cancellable_new();
    run_async(path, cancellable, &r, TRUE);
    assert(r.calls == 1 && r.info == NULL);
    assert(g_error_matches(r.error, G_IO_ERROR, G_IO_ERROR_CANCELLED));
    g_clear_error(&r.error);
    g_object_unref(cancellable);

    /* Più probing concorrenti (uno per slot) restano indipendenti. */
    char *other = make_video(dir, "async2.webm", 640, 360, "30/1", 25);
    AsyncResult a = {0}, b = {0};
    a.loop = b.loop = g_main_loop_new(NULL, FALSE);
    a.info = b.info = NULL;
    discoverer_probe_async(path, 0, NULL, on_probe_done, &a);
    discoverer_probe_async(other, 0, NULL, on_probe_done, &b);
    guint timeout = g_timeout_add_seconds(30, loop_timeout, a.loop);
    while (a.calls + b.calls < 2) {
        g_main_context_iteration(NULL, TRUE);
        if (!g_main_context_find_source_by_id(NULL, timeout)) {
            break;
        }
    }
    g_source_remove(timeout);
    assert(a.info && b.info && a.info->width == 320 && b.info->width == 640);
    video_info_free(a.info);
    video_info_free(b.info);
    g_main_loop_unref(a.loop);

    g_free(path);
    g_free(missing);
    g_free(other);
}

int
main(void)
{
    /* Un CRITICAL di GLib/GObject (es. property fuori range) deve far fallire il test. */
    g_log_set_always_fatal(G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);
    gst_init(NULL, NULL);

    const char *needed[] = { "videotestsrc", "videoconvert", "vp8enc", "webmmux", "filesink", NULL };
    if (!have_elements(needed)) {
        return SKIP_EXIT;
    }

    char *dir = g_dir_make_tmp("syncview-discoverer-XXXXXX", NULL);
    assert(dir != NULL);

    test_default_info();
    test_error_classification();
    test_missing_plugins_message();
    test_nominal_fps_from_intervals();
    test_video_info(dir);
    test_estimate_fps_from_file(dir);
    test_audio_only_gives_defaults(dir);
    test_file_not_found(dir);
    test_corrupt_file(dir);
    test_truncated_file(dir);
    test_timeout_does_not_crash(dir);
    test_async(dir);

    char *cmd = g_strdup_printf("rm -rf '%s'", dir);
    assert(system(cmd) == 0);
    g_free(cmd);
    g_free(dir);
    return 0;
}
