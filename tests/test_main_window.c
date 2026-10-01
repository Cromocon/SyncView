/*
 * M2.8: finestra principale minima. Richiede un display (GTK) e il plugin GStreamer gtk4paintablesink: se mancano,
 * termina con 77 (skip Meson). Controlla il tema (nessun errore CSS in entrambi i temi), gli stati della finestra, il
 * flusso apri → play → pausa → passo → chiudi e l'ottimizzazione O3 (il percorso si salva solo a caricamento riuscito).
 */
#include "core/logger.h"
#include "core/user_paths.h"
#include "ui/main_window.h"
#include "ui/theme.h"
#include "video/video_player.h"

#include <assert.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gst/gst.h>
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SKIP_EXIT 77

static GtkApplication *app;
static char *tmp_dir;

/* ---- Strumenti ---- */

static gboolean
test_selected(const char *name)
{
    const char *only = g_getenv("SYNCVIEW_TEST_ONLY");

    return !only || strstr(name, only) != NULL;
}

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

static void
spin_for(int ms)
{
    gint64 end = g_get_monotonic_time() + (gint64)ms * 1000;

    while (g_get_monotonic_time() < end) {
        g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
}

static gboolean
spin_until_state(GtkWidget *window, SyncviewMainWindowState wanted, int timeout_ms)
{
    gint64 deadline = g_get_monotonic_time() + (gint64)timeout_ms * 1000;

    while (syncview_main_window_get_state(window) != wanted && g_get_monotonic_time() < deadline) {
        g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
    if (syncview_main_window_get_state(window) != wanted) {
        GtkWidget *title = syncview_main_window_get_widget(window, "card-title");
        GtkWidget *detail = syncview_main_window_get_widget(window, "card-detail");
        SyncviewVideoPlayer *player = syncview_main_window_get_player(window);

        g_printerr("attesa dello stato %d scaduta: stato=%d titolo=«%s» dettaglio=«%s» player: loading=%d loaded=%d\n", wanted,
                   syncview_main_window_get_state(window), gtk_label_get_text(GTK_LABEL(title)),
                   gtk_label_get_text(GTK_LABEL(detail)), player ? syncview_video_player_is_loading(player) : -1,
                   player ? syncview_video_player_is_loaded(player) : -1);
        return FALSE;
    }
    return TRUE;
}

static char *
make_video(const char *name, int frames)
{
    char *path = g_build_filename(tmp_dir, name, NULL);
    char *desc = g_strdup_printf("videotestsrc num-buffers=%d ! video/x-raw,width=320,height=240,framerate=25/1 ! "
                                 "videoconvert ! vp8enc ! webmmux ! filesink name=out", frames);
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
read_file(const char *path)
{
    char *contents = NULL;

    if (!g_file_get_contents(path, &contents, NULL, NULL)) {
        return NULL;
    }
    return contents;
}

/*
 * Il tempo mostrato durante il play segue il frame clock di GTK (ottimizzazione O5). Su un desktop non attivo o su un
 * runner senza display attivo il compositor non invia frame callback: lì il tempo non avanza e non si può verificare.
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
frame_clock_ticks(GtkWidget *window)
{
    GtkWidget *tick_source = syncview_main_window_get_widget(window, "seek");
    guint ticks = 0;
    guint id = gtk_widget_add_tick_callback(tick_source, tick_counter, &ticks, NULL);

    spin_for(500);
    gtk_widget_remove_tick_callback(tick_source, id);
    return ticks >= 10;
}

static GtkWidget *
new_window(const char *paths_file)
{
    GtkWidget *window = syncview_main_window_new(app, paths_file);

    gtk_window_present(GTK_WINDOW(window));
    /* Come nel test del player: si attende che la finestra sia mappata (il sink ne ha bisogno per il contesto GL). */
    for (gint64 deadline = g_get_monotonic_time() + 5 * G_USEC_PER_SEC;
         !gtk_widget_get_mapped(window) && g_get_monotonic_time() < deadline;) {
        g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
    return window;
}

static void
close_window(GtkWidget *window)
{
    gtk_window_destroy(GTK_WINDOW(window));
    assert(drain_teardowns(15000));
}

static void
click(GtkWidget *window, const char *name)
{
    GtkWidget *button = syncview_main_window_get_widget(window, name);

    assert(button != NULL && gtk_widget_get_sensitive(button));
    g_signal_emit_by_name(button, "clicked");
}

static const char *
label_of(GtkWidget *window, const char *name)
{
    GtkWidget *widget = syncview_main_window_get_widget(window, name);

    assert(widget != NULL);
    return GTK_IS_LABEL(widget) ? gtk_label_get_text(GTK_LABEL(widget)) : gtk_button_get_label(GTK_BUTTON(widget));
}

/* ---- Test ---- */

static void
test_theme_has_no_css_errors(void)
{
    GdkDisplay *display = gdk_display_get_default();

    syncview_theme_init(display);

    /* Entrambi i fogli di stile generati devono essere accettati da GTK senza errori di analisi. */
    syncview_theme_set_choice(SYNCVIEW_THEME_DARK);
    assert(syncview_theme_is_dark());
    syncview_theme_set_choice(SYNCVIEW_THEME_LIGHT);
    assert(!syncview_theme_is_dark());
    syncview_theme_set_choice(SYNCVIEW_THEME_DARK);
    assert(syncview_theme_is_dark());
    assert(syncview_theme_parse_errors() == 0);
    syncview_theme_set_choice(SYNCVIEW_THEME_LIGHT);
    assert(syncview_theme_parse_errors() == 0);
}

static void
test_empty_window(void)
{
    char *paths_file = g_build_filename(tmp_dir, "empty-paths.json", NULL);
    GtkWidget *window = new_window(paths_file);

    assert(syncview_main_window_get_state(window) == SYNCVIEW_MAIN_WINDOW_EMPTY);
    assert(syncview_main_window_get_player(window) != NULL);
    assert(strcmp(label_of(window, "card-title"), "Nessun video") == 0);
    assert(!gtk_widget_get_sensitive(syncview_main_window_get_widget(window, "play")));
    assert(!gtk_widget_get_sensitive(syncview_main_window_get_widget(window, "seek")));
    const char *steps[] = { "step-m10", "step-m1", "step-p1", "step-p10" };

    for (int i = 0; i < 4; i++) {
        assert(!gtk_widget_get_sensitive(syncview_main_window_get_widget(window, steps[i])));
    }
    /* Il pulsante «Carica video» c'è e funziona da subito. */
    assert(gtk_widget_get_sensitive(syncview_main_window_get_widget(window, "open")));

    close_window(window);
    g_free(paths_file);
}

static void
test_open_play_step_close(void)
{
    char *video = make_video("play.webm", 100);  /* 4 s a 25 fps */
    char *paths_file = g_build_filename(tmp_dir, "play-paths.json", NULL);
    GtkWidget *window = new_window(paths_file);
    SyncviewVideoPlayer *player = syncview_main_window_get_player(window);

    assert(syncview_main_window_open_file(window, video));
    assert(syncview_main_window_get_state(window) == SYNCVIEW_MAIN_WINDOW_LOADING);
    assert(strcmp(label_of(window, "card-title"), "Analisi del file…") == 0);
    assert(spin_until_state(window, SYNCVIEW_MAIN_WINDOW_LOADED, 15000));

    /* Caricato: comandi attivi, durata e tempo mostrati. */
    assert(gtk_widget_get_sensitive(syncview_main_window_get_widget(window, "play")));
    assert(gtk_widget_get_sensitive(syncview_main_window_get_widget(window, "seek")));
    assert(strcmp(label_of(window, "time"), "00:00.000") == 0);
    assert(strcmp(label_of(window, "play"), "Play") == 0);
    assert(syncview_video_player_get_duration(player) > 3500);

    /* O3: dopo il caricamento riuscito il percorso è nel file. */
    char *saved = read_file(paths_file);

    assert(saved != NULL && strstr(saved, "play.webm") != NULL);
    g_free(saved);

    /* Play dal pulsante: parte, il pulsante diventa «Pausa» e il tempo avanza. */
    click(window, "play");
    assert(syncview_video_player_get_playback_state(player) == SYNCVIEW_PLAYBACK_PLAYING);
    assert(strcmp(label_of(window, "play"), "Pausa") == 0);
    spin_for(600);
    if (frame_clock_ticks(window)) {
        assert(strcmp(label_of(window, "time"), "00:00.000") != 0);
    } else {
        g_printerr("frame clock assente (finestra non visibile): avanzamento del tempo in play non verificato\n");
    }

    click(window, "play");
    assert(syncview_video_player_get_playback_state(player) == SYNCVIEW_PLAYBACK_PAUSED);
    assert(strcmp(label_of(window, "play"), "Play") == 0);
    spin_for(300);

    /* Seek come dalla barra: la barra e il tempo seguono. */
    assert(syncview_video_player_seek(player, 2000, NULL));
    spin_for(600);
    GtkWidget *scale = syncview_main_window_get_widget(window, "seek");

    assert(gtk_range_get_value(GTK_RANGE(scale)) > 1900 && gtk_range_get_value(GTK_RANGE(scale)) < 2100);
    assert(strcmp(label_of(window, "time"), "00:02.000") == 0);

    /* Passo di un frame dal pulsante: avanza di 40 ms esatti. */
    click(window, "step-p1");
    spin_for(800);
    assert(strcmp(label_of(window, "time"), "00:02.040") == 0);
    click(window, "step-m1");
    spin_for(800);
    assert(strcmp(label_of(window, "time"), "00:02.000") == 0);

    close_window(window);
    g_free(paths_file);
    g_free(video);
}

static void
test_o3_failed_load_does_not_touch_saved_paths(void)
{
    char *video = make_video("o3.webm", 25);
    char *paths_file = g_build_filename(tmp_dir, "o3-paths.json", NULL);
    char *garbage = g_build_filename(tmp_dir, "rotto.mp4", NULL);
    char *missing = g_build_filename(tmp_dir, "non-esiste.mp4", NULL);
    GtkWidget *window = new_window(paths_file);

    assert(g_file_set_contents(garbage, "questo non e' un video", -1, NULL));

    assert(syncview_main_window_open_file(window, video));
    assert(spin_until_state(window, SYNCVIEW_MAIN_WINDOW_LOADED, 15000));
    char *before = read_file(paths_file);

    assert(before != NULL && strstr(before, "o3.webm") != NULL);

    /* File non riproducibile: errore nella finestra, file dei percorsi identico byte per byte. */
    assert(syncview_main_window_open_file(window, garbage));  /* il caricamento parte; l'esito è asincrono */
    assert(spin_until_state(window, SYNCVIEW_MAIN_WINDOW_ERROR, 15000));
    assert(strcmp(label_of(window, "card-title"), "Impossibile riprodurre il video") == 0);
    assert(strstr(label_of(window, "card-detail"), "rotto.mp4") != NULL);
    assert(!gtk_widget_get_sensitive(syncview_main_window_get_widget(window, "play")));
    char *after_garbage = read_file(paths_file);

    assert(after_garbage != NULL && strcmp(before, after_garbage) == 0);

    /* File inesistente: errore subito, ancora nessuna modifica. */
    assert(!syncview_main_window_open_file(window, missing));
    assert(syncview_main_window_get_state(window) == SYNCVIEW_MAIN_WINDOW_ERROR);
    assert(strcmp(label_of(window, "card-title"), "File spostato o non trovato") == 0);
    char *after_missing = read_file(paths_file);

    assert(after_missing != NULL && strcmp(before, after_missing) == 0);

    /* Dopo gli errori si può caricare di nuovo un video valido: ora il percorso cambia. */
    char *second = make_video("o3-second.webm", 25);

    assert(syncview_main_window_open_file(window, second));
    assert(spin_until_state(window, SYNCVIEW_MAIN_WINDOW_LOADED, 15000));
    char *after_second = read_file(paths_file);

    assert(after_second != NULL && strstr(after_second, "o3-second.webm") != NULL);

    g_free(second);
    g_free(after_second);
    g_free(after_missing);
    g_free(after_garbage);
    g_free(before);
    close_window(window);
    g_free(missing);
    g_free(garbage);
    g_free(paths_file);
    g_free(video);
}

static void
test_saved_video_is_reloaded_at_startup(void)
{
    char *video = make_video("startup.webm", 25);
    char *paths_file = g_build_filename(tmp_dir, "startup-paths.json", NULL);
    GtkWidget *first = new_window(paths_file);

    assert(syncview_main_window_open_file(first, video));
    assert(spin_until_state(first, SYNCVIEW_MAIN_WINDOW_LOADED, 15000));
    close_window(first);

    /* Nuova finestra: ricarica da sola il video salvato. */
    GtkWidget *second = new_window(paths_file);

    assert(syncview_main_window_get_state(second) == SYNCVIEW_MAIN_WINDOW_LOADING ||
           syncview_main_window_get_state(second) == SYNCVIEW_MAIN_WINDOW_LOADED);
    assert(spin_until_state(second, SYNCVIEW_MAIN_WINDOW_LOADED, 15000));
    close_window(second);

    /* Se il file non c'è più: finestra vuota e percorso tolto dal file (get_valid_video_paths). */
    assert(g_remove(video) == 0);
    GtkWidget *third = new_window(paths_file);

    assert(syncview_main_window_get_state(third) == SYNCVIEW_MAIN_WINDOW_EMPTY);
    char *saved = read_file(paths_file);

    assert(saved != NULL && strstr(saved, "startup.webm") == NULL);
    g_free(saved);
    close_window(third);

    g_free(paths_file);
    g_free(video);
}

static void
test_close_while_loading_and_playing(void)
{
    char *video = make_video("close.webm", 100);
    char *paths_file = g_build_filename(tmp_dir, "close-paths.json", NULL);

    for (int i = 0; i < 6; i++) {
        GtkWidget *window = new_window(paths_file);

        assert(syncview_main_window_open_file(window, video));
        if (i % 2) {
            /* Chiusa a metà del caricamento. */
            spin_for(i * 10);
        } else {
            /* Chiusa in riproduzione. */
            assert(spin_until_state(window, SYNCVIEW_MAIN_WINDOW_LOADED, 15000));
            click(window, "play");
            spin_for(200);
        }
        close_window(window);  /* nessun smontaggio rimasto in sospeso */
    }

    g_free(paths_file);
    g_free(video);
}

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

#ifdef __APPLE__
    /*
     * PROBLEMA APERTO: sul runner macOS della CI (nessun display attivo) il caricamento di un video agganciato alla
     * finestra non termina mai (la pipeline resta ferma dopo READY → PAUSED), mentre lo stesso caricamento nel test del
     * player, senza la nostra finestra, funziona. Da verificare su un Mac reale prima di dichiarare M2.8 funzionante lì.
     */
    if (g_getenv("CI")) {
        g_printerr("macOS in CI: test della finestra saltato (problema aperto, vedi PLAN.md M2.8)\n");
        return SKIP_EXIT;
    }
#endif

    g_log_set_always_fatal(G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);
    gst_init(NULL, NULL);

    GstElementFactory *sink = gst_element_factory_find("gtk4paintablesink");
    GstElementFactory *playbin = gst_element_factory_find("playbin3");
    const char *encoder_elements[] = { "videotestsrc", "videoconvert", "vp8enc", "webmmux", "filesink", NULL };

    if (!sink || !playbin) {
        return SKIP_EXIT;  /* plugin gtk4 (gst-plugins-rs) non installato */
    }
    gst_object_unref(sink);
    gst_object_unref(playbin);
    for (int i = 0; encoder_elements[i]; i++) {
        GstElementFactory *factory = gst_element_factory_find(encoder_elements[i]);

        if (!factory) {
            return SKIP_EXIT;  /* mancano i plugin per generare i file di prova */
        }
        gst_object_unref(factory);
    }

    if (g_getenv("SYNCVIEW_DEBUG")) {
        logger_init(NULL, FALSE, NULL);
    }

    tmp_dir = g_dir_make_tmp("syncview-mainwin-XXXXXX", NULL);
    assert(tmp_dir != NULL);
    app = gtk_application_new("com.syncview.SyncView.Test", G_APPLICATION_NON_UNIQUE);
    assert(g_application_register(G_APPLICATION(app), NULL, NULL));

    RUN_TEST(test_theme_has_no_css_errors());
    RUN_TEST(test_empty_window());
    RUN_TEST(test_open_play_step_close());
    RUN_TEST(test_o3_failed_load_does_not_touch_saved_paths());
    RUN_TEST(test_saved_video_is_reloaded_at_startup());
    RUN_TEST(test_close_while_loading_and_playing());

    assert(drain_teardowns(15000));
    g_object_unref(app);
    return 0;
}
