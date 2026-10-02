/*
 * Prova su file reali (M2.7, punto aperto): ogni clip in $SYNCVIEW_TEST_CLIPS (cartella) viene caricata e
 * si verifica che il passo ±1 frame sia esatto (1/fps), che il seek accurato cada sul frame giusto e che il
 * cambio di velocità non faccia saltare la posizione. Senza la variabile (o senza display/plugin) esce con 77.
 */
#include "video/video_player.h"

#include <glib.h>
#include <gtk/gtk.h>
#include <gst/gst.h>
#include <stdio.h>
#include <stdlib.h>

#define SKIP_EXIT 77
#define NS_PER_MS 1000000LL

static int failures;

#define CHECK(cond, ...) \
    G_STMT_START { \
        if (!(cond)) { \
            failures++; \
            g_printerr("  FALLITO (%s:%d) %s: ", __FILE__, __LINE__, #cond); \
            g_printerr(__VA_ARGS__); \
            g_printerr("\n"); \
        } \
    } G_STMT_END

static void
spin_ms(int ms)
{
    gint64 end = g_get_monotonic_time() + (gint64)ms * 1000;
    while (g_get_monotonic_time() < end) {
        g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
}

static gboolean
wait_loaded(SyncviewVideoPlayer *p)
{
    gint64 end = g_get_monotonic_time() + 15 * G_USEC_PER_SEC;
    while (g_get_monotonic_time() < end) {
        g_main_context_iteration(NULL, FALSE);
        if (syncview_video_player_is_loaded(p) && syncview_video_player_get_frame_end_ns(p) > 0) {
            return TRUE;
        }
        g_usleep(1000);
    }
    return FALSE;
}

/* Il frame mostrato non cambia da 250 ms (dopo aver atteso che cambi rispetto a `before`, al più 3 s). */
static gint64
settled_after(SyncviewVideoPlayer *p, gint64 before)
{
    gint64 end = g_get_monotonic_time() + 3 * G_USEC_PER_SEC;
    while (syncview_video_player_get_frame_end_ns(p) == before && g_get_monotonic_time() < end) {
        g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
    gint64 last = -2, since = g_get_monotonic_time();
    end = since + 10 * G_USEC_PER_SEC;
    while (g_get_monotonic_time() < end) {
        gint64 now = syncview_video_player_get_frame_end_ns(p);
        if (now != last) {
            last = now;
            since = g_get_monotonic_time();
        } else if (g_get_monotonic_time() - since > 250000) {
            break;
        }
        g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
    return last;
}

static void
check_clip(const char *path)
{
    g_print("== %s\n", path);
    SyncviewVideoPlayer *p = syncview_video_player_new(0, NULL);
    GError *error = NULL;

    if (!syncview_video_player_load(p, path, &error) || !wait_loaded(p)) {
        CHECK(FALSE, "caricamento non riuscito (%s)", error ? error->message : "timeout");
        g_clear_error(&error);
        g_object_unref(p);
        return;
    }
    double fps = syncview_video_player_get_frame_rate(p);
    gint64 frame_ns = (gint64)(1e9 / fps + 0.5);
    gint64 duration = syncview_video_player_get_duration(p);
    char *dec = syncview_video_player_get_decoder_description(p);
    g_print("  fps=%.3f durata=%" G_GINT64_FORMAT " ms primo frame fine=%" G_GINT64_FORMAT " ns decoder=%s\n", fps, duration,
            syncview_video_player_get_frame_end_ns(p), dec ? dec : "?");
    g_free(dec);
    CHECK(fps > 20 && fps < 70, "fps %.3f", fps);
    CHECK(duration >= 9900 && duration <= 10100, "durata %" G_GINT64_FORMAT, duration);
    CHECK(llabs(syncview_video_player_get_frame_end_ns(p) - frame_ns) <= 1000000, "primo frame");

    /* Avanti di un frame, 8 volte: ogni volta esattamente +1/fps. */
    gint64 prev = syncview_video_player_get_frame_end_ns(p);
    for (int i = 0; i < 8; i++) {
        CHECK(syncview_video_player_step_frames(p, 1, NULL), "step avanti");
        gint64 now = settled_after(p, prev);
        CHECK(llabs((now - prev) - frame_ns) <= 1000000, "avanti %d: delta %" G_GINT64_FORMAT " ns (atteso %" G_GINT64_FORMAT ")", i, now - prev, frame_ns);
        prev = now;
    }
    /* Indietro di un frame, 8 volte. */
    for (int i = 0; i < 8; i++) {
        CHECK(syncview_video_player_step_frames(p, -1, NULL), "step indietro");
        gint64 now = settled_after(p, prev);
        CHECK(llabs((prev - now) - frame_ns) <= 1000000, "indietro %d: delta %" G_GINT64_FORMAT " ns", i, prev - now);
        prev = now;
    }
    /* Dopo 8 avanti e 8 indietro si è di nuovo al primo frame. */
    CHECK(llabs(prev - frame_ns) <= 1000000, "ritorno al primo frame: %" G_GINT64_FORMAT, prev);

    /* Seek accurato a metà frame: mostra il frame che contiene il punto. */
    gint64 targets[] = { 1000, 4321, 7777, 9500 };
    for (guint i = 0; i < G_N_ELEMENTS(targets); i++) {
        CHECK(syncview_video_player_seek(p, targets[i], NULL), "seek");
        gint64 now = settled_after(p, prev);
        gint64 start = now - frame_ns;
        gint64 t_ns = targets[i] * NS_PER_MS;
        CHECK(t_ns >= start - 1000000 && t_ns < now + 1000000, "seek %" G_GINT64_FORMAT " ms -> frame %" G_GINT64_FORMAT "-%" G_GINT64_FORMAT " ns", targets[i], start, now);
        prev = now;
    }
    /* Passo indietro dopo un seek in mezzo al video (caso tipico). */
    CHECK(syncview_video_player_step_frames(p, -1, NULL), "step indietro dopo seek");
    gint64 now = settled_after(p, prev);
    CHECK(llabs((prev - now) - frame_ns) <= 1000000, "indietro dopo seek: delta %" G_GINT64_FORMAT, prev - now);
    prev = now;
    /* Passi multipli ±10. */
    CHECK(syncview_video_player_step_frames(p, 10, NULL), "step +10");
    now = settled_after(p, prev);
    CHECK(llabs((now - prev) - 10 * frame_ns) <= 2000000, "+10: delta %" G_GINT64_FORMAT, now - prev);
    prev = now;
    CHECK(syncview_video_player_step_frames(p, -10, NULL), "step -10");
    now = settled_after(p, prev);
    CHECK(llabs((prev - now) - 10 * frame_ns) <= 2000000, "-10: delta %" G_GINT64_FORMAT, prev - now);

    /* Riproduzione e velocità: il tempo avanza (senza frame clock si misura la pipeline). */
    CHECK(syncview_video_player_seek(p, 2000, NULL), "seek 2000");
    prev = settled_after(p, now);
    CHECK(syncview_video_player_play(p, NULL), "play");
    spin_ms(700);
    gint64 pos = syncview_video_player_get_position(p);
    CHECK(pos >= 2300 && pos <= 3500, "dopo ~700 ms di play: %" G_GINT64_FORMAT " ms", pos);
    CHECK(syncview_video_player_set_playback_rate(p, 2.0, NULL), "rate 2x");
    spin_ms(500);  /* il cambio di velocità è un seek: col decoder hardware la ripartenza richiede qualche centinaio di ms */
    gint64 before = syncview_video_player_get_position(p);
    gint64 t0 = g_get_monotonic_time();
    spin_ms(1000);
    gint64 after = syncview_video_player_get_position(p);
    gint64 real_ms = (g_get_monotonic_time() - t0) / 1000;
    CHECK(after - before >= real_ms * 3 / 2 && after - before <= real_ms * 5 / 2, "2x: %" G_GINT64_FORMAT " ms in %" G_GINT64_FORMAT " ms reali", after - before, real_ms);
    CHECK(syncview_video_player_set_playback_rate(p, 1.0, NULL), "rate 1x");
    CHECK(syncview_video_player_pause(p, NULL), "pausa");
    spin_ms(300);

    g_object_unref(p);
}

/* Raccoglie ricorsivamente i file (i clip possono stare in sottocartelle per formato). */
static void
collect_files(const char *dir, GPtrArray *out)
{
    GDir *d = g_dir_open(dir, 0, NULL);

    for (const char *n; d && (n = g_dir_read_name(d));) {
        char *path = g_build_filename(dir, n, NULL);
        if (g_file_test(path, G_FILE_TEST_IS_DIR)) {
            collect_files(path, out);
            g_free(path);
        } else {
            g_ptr_array_add(out, path);
        }
    }
    if (d) {
        g_dir_close(d);
    }
}

int
main(int argc, char **argv)
{
    const char *dir = g_getenv("SYNCVIEW_TEST_CLIPS");

    if (!dir || !*dir) {
        g_print("SYNCVIEW_TEST_CLIPS non impostata: salto\n");
        return SKIP_EXIT;
    }
    if (!gtk_init_check()) {
        g_print("nessun display: salto\n");
        return SKIP_EXIT;
    }
    gst_init(&argc, &argv);
    if (!gst_element_factory_find("gtk4paintablesink")) {
        g_print("gtk4paintablesink assente: salto\n");
        return SKIP_EXIT;
    }

    GPtrArray *names = g_ptr_array_new_with_free_func(g_free);
    collect_files(dir, names);
    if (names->len == 0) {
        g_printerr("nessun file in %s\n", dir);
        return 1;
    }
    g_ptr_array_sort_values(names, (GCompareFunc)g_strcmp0);

    for (guint i = 0; i < names->len; i++) {
        check_clip(g_ptr_array_index(names, i));
    }
    g_ptr_array_free(names, TRUE);
    g_print("%s (%d verifiche fallite)\n", failures ? "ESITO: FALLITO" : "ESITO: OK", failures);
    return failures ? 1 : 0;
}
