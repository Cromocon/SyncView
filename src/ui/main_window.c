#include "ui/main_window.h"

#include "core/logger.h"
#include "core/user_paths.h"
#include "ui/theme.h"
#include "util/time_format.h"

#include <stdlib.h>
#include <string.h>

#define MAIN_WINDOW_DATA "syncview-main-window"
#define VIDEO_SLOT 0               /* un solo video in questa versione: slot 1 di SYNCVIEW_MAX_VIDEOS */
#define PULSE_INTERVAL_MS 120

typedef struct {
    GtkWidget *window;
    SyncviewVideoPlayer *player;
    UserPaths *paths;
    GHashTable *widgets;           /* nome -> widget (non posseduti) */

    SyncviewMainWindowState state;
    char *shown_name;              /* nome del file in caricamento/caricato, per i messaggi */
    gint64 duration_ms;
    gboolean updating_scale;       /* l'aggiornamento della barra viene dal player, non dall'utente */
    guint pulse_id;

    GtkWidget *picture;
    GtkWidget *card;
    GtkWidget *card_title;
    GtkWidget *card_detail;
    GtkWidget *card_progress;
    GtkWidget *card_button;
    GtkWidget *chip_fps;
    GtkWidget *chip_time;
    GtkWidget *chip_end;
    GtkWidget *subtitle;
    GtkWidget *btn_play;
    GtkWidget *btn_step[4];        /* -10 -1 +1 +10 */
    GtkWidget *scale;
    GtkWidget *time_label;
    GtkWidget *total_label;

    SyncviewDepsDialogOptions *deps_options;  /* copia posseduta, NULL = predefinite */
} MainWindow;

static const int STEP_VALUES[4] = { -10, -1, 1, 10 };
static const char *STEP_LABELS[4] = { "−10", "−1", "+1", "+10" };
static const char *STEP_NAMES[4] = { "step-m10", "step-m1", "step-p1", "step-p10" };

static MainWindow *
mw_of(GtkWidget *window)
{
    return window ? g_object_get_data(G_OBJECT(window), MAIN_WINDOW_DATA) : NULL;
}

/* "MM:SS.mmm" (o "HH:MM:SS.mmm" oltre l'ora), come nei mockup del design. */
static void
format_time(gint64 ms, char *out, size_t size)
{
    char full[SYNCVIEW_TIME_FORMAT_BUFSIZE];

    syncview_format_time_ms(ms, full, sizeof full);
    g_strlcpy(out, strncmp(full, "00:", 3) == 0 ? full + 3 : full, size);
}

static GtkWidget *
register_widget(MainWindow *mw, const char *name, GtkWidget *widget)
{
    g_hash_table_insert(mw->widgets, g_strdup(name), widget);
    return widget;
}

static GtkWidget *
make_label(const char *text, const char *css_class)
{
    GtkWidget *label = gtk_label_new(text);

    if (css_class) {
        gtk_widget_add_css_class(label, css_class);
    }
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    return label;
}

/* ---- Stato del riquadro video ---- */

static gboolean
on_pulse(gpointer user_data)
{
    MainWindow *mw = user_data;

    gtk_progress_bar_pulse(GTK_PROGRESS_BAR(mw->card_progress));
    return G_SOURCE_CONTINUE;
}

static void
update_controls(MainWindow *mw)
{
    gboolean loaded = mw->state == SYNCVIEW_MAIN_WINDOW_LOADED;

    gtk_widget_set_sensitive(mw->btn_play, loaded);
    for (int i = 0; i < 4; i++) {
        gtk_widget_set_sensitive(mw->btn_step[i], loaded);
    }
    gtk_widget_set_sensitive(mw->scale, loaded);
    gtk_widget_set_visible(mw->chip_time, loaded);
    gtk_widget_set_visible(mw->chip_fps, loaded && mw->player && syncview_video_player_get_frame_rate(mw->player) > 0.0);
}

static void
set_state(MainWindow *mw, SyncviewMainWindowState state, const char *title, const char *detail)
{
    mw->state = state;

    gboolean show_card = state != SYNCVIEW_MAIN_WINDOW_LOADED;

    gtk_widget_set_visible(mw->card, show_card);
    if (title) {
        gtk_label_set_text(GTK_LABEL(mw->card_title), title);
    }
    if (detail) {
        gtk_label_set_text(GTK_LABEL(mw->card_detail), detail);
    }
    if (state == SYNCVIEW_MAIN_WINDOW_ERROR) {
        gtk_widget_add_css_class(mw->card, "sv-error");
    } else {
        gtk_widget_remove_css_class(mw->card, "sv-error");
    }

    gtk_widget_set_visible(mw->card_progress, state == SYNCVIEW_MAIN_WINDOW_LOADING);
    gtk_widget_set_visible(mw->card_button, state != SYNCVIEW_MAIN_WINDOW_LOADING);
    gtk_button_set_label(GTK_BUTTON(mw->card_button),
                         state == SYNCVIEW_MAIN_WINDOW_ERROR ? "Apri un altro video…" : "Carica video…  Ctrl+O");

    /* L'animazione serve solo durante il caricamento: da fermo nessun risveglio periodico. */
    if (state == SYNCVIEW_MAIN_WINDOW_LOADING && !mw->pulse_id) {
        mw->pulse_id = g_timeout_add(PULSE_INTERVAL_MS, on_pulse, mw);
    } else if (state != SYNCVIEW_MAIN_WINDOW_LOADING && mw->pulse_id) {
        g_source_remove(mw->pulse_id);
        mw->pulse_id = 0;
    }

    if (state != SYNCVIEW_MAIN_WINDOW_LOADED) {
        gtk_widget_set_visible(mw->chip_end, FALSE);
        gtk_label_set_text(GTK_LABEL(mw->subtitle), state == SYNCVIEW_MAIN_WINDOW_EMPTY ? "" : (mw->shown_name ? mw->shown_name : ""));
    }
    update_controls(mw);
}

static void
update_time_labels(MainWindow *mw, gint64 position_ms)
{
    char now[SYNCVIEW_TIME_FORMAT_BUFSIZE], total[SYNCVIEW_TIME_FORMAT_BUFSIZE + 4];

    format_time(position_ms, now, sizeof now);
    gtk_label_set_text(GTK_LABEL(mw->time_label), now);
    gtk_label_set_text(GTK_LABEL(mw->chip_time), now);

    char dur[SYNCVIEW_TIME_FORMAT_BUFSIZE];

    format_time(mw->duration_ms, dur, sizeof dur);
    g_snprintf(total, sizeof total, "/ %s", dur);
    gtk_label_set_text(GTK_LABEL(mw->total_label), total);
}

static void
update_play_button(MainWindow *mw)
{
    SyncviewPlaybackState state = syncview_video_player_get_playback_state(mw->player);

    gtk_button_set_label(GTK_BUTTON(mw->btn_play), state == SYNCVIEW_PLAYBACK_PLAYING ? "Pausa" : "Play");
}

/* ---- Segnali del player ---- */

static void
on_position_changed(SyncviewVideoPlayer *player, gint64 position_ms, gpointer user_data)
{
    MainWindow *mw = user_data;

    (void)player;
    update_time_labels(mw, position_ms);

    mw->updating_scale = TRUE;
    gtk_range_set_value(GTK_RANGE(mw->scale), (double)position_ms);
    mw->updating_scale = FALSE;

    /* «Fine del video»: fermo (STOPPED) ma non all'inizio. */
    gtk_widget_set_visible(mw->chip_end, mw->state == SYNCVIEW_MAIN_WINDOW_LOADED && position_ms > 0 &&
                                             syncview_video_player_get_playback_state(mw->player) ==
                                                 SYNCVIEW_PLAYBACK_STOPPED);
}

static void
on_duration_changed(SyncviewVideoPlayer *player, gint64 duration_ms, gpointer user_data)
{
    MainWindow *mw = user_data;

    (void)player;
    mw->duration_ms = duration_ms;
    mw->updating_scale = TRUE;
    gtk_range_set_range(GTK_RANGE(mw->scale), 0.0, duration_ms > 0 ? (double)duration_ms : 1.0);
    mw->updating_scale = FALSE;
    update_time_labels(mw, syncview_video_player_get_position(mw->player));
}

static void
on_playback_state_changed(SyncviewVideoPlayer *player, guint state, gpointer user_data)
{
    MainWindow *mw = user_data;

    (void)player;
    (void)state;
    update_play_button(mw);
    gtk_widget_set_visible(mw->chip_end, mw->state == SYNCVIEW_MAIN_WINDOW_LOADED &&
                                             syncview_video_player_get_playback_state(mw->player) ==
                                                 SYNCVIEW_PLAYBACK_STOPPED &&
                                             syncview_video_player_get_position(mw->player) > 0);
}

static void
on_load_state_changed(SyncviewVideoPlayer *player, gboolean loaded, gpointer user_data)
{
    MainWindow *mw = user_data;

    if (!loaded) {
        return;  /* il motivo arriva col segnale "error" */
    }

    set_state(mw, SYNCVIEW_MAIN_WINDOW_LOADED, NULL, NULL);
    gtk_label_set_text(GTK_LABEL(mw->subtitle), mw->shown_name ? mw->shown_name : "");

    char fps_text[32];
    double fps = syncview_video_player_get_frame_rate(player);

    if (fps > 0.0) {
        char number[G_ASCII_DTOSTR_BUF_SIZE];

        g_ascii_formatd(number, sizeof number, fps == (int)fps ? "%.0f" : "%.2f", fps);
        g_snprintf(fps_text, sizeof fps_text, "%s fps", number);
        gtk_label_set_text(GTK_LABEL(mw->chip_fps), fps_text);
    }
    update_controls(mw);
    update_play_button(mw);

    /*
     * Ottimizzazione O3: il percorso si salva solo ORA, a caricamento riuscito. L'originale lo salvava prima
     * dell'analisi del file, lasciando nel file dei percorsi quelli di video rotti fino alla pulizia successiva.
     */
    const char *path = syncview_video_player_get_path(player);
    GError *error = NULL;

    if (path && !user_paths_set_video_path(mw->paths, VIDEO_SLOT, path, &error)) {
        log_error("Impossibile salvare il percorso del video", error);
        g_clear_error(&error);
    }
    log_ui("Video caricato nella finestra: %s", mw->shown_name ? mw->shown_name : "?");
}

/* Avvia il controllo delle dipendenze con le opzioni della finestra (per i test, sostituibili). */
static void
start_deps_check(MainWindow *mw, SyncviewDepsShowMode mode)
{
    SyncviewDepsDialogOptions options = { 0 };

    if (mw->deps_options) {
        options = *mw->deps_options;
    }
    options.restart_hint = mw->player == NULL;  /* senza player la finestra va riaperta per usare ciò che si installa */
    syncview_deps_check_and_show(GTK_WINDOW(mw->window), mode, &options);
}

static gboolean
on_missing_plugin_idle(gpointer user_data)
{
    GWeakRef *ref = user_data;
    GtkWidget *window = g_weak_ref_get(ref);

    if (window) {
        MainWindow *mw = mw_of(window);

        if (mw) {
            start_deps_check(mw, SYNCVIEW_DEPS_SHOW_AFTER_ERROR);
        }
        g_object_unref(window);
    }
    g_weak_ref_clear(ref);
    g_free(ref);
    return G_SOURCE_REMOVE;
}

static void
on_player_error(SyncviewVideoPlayer *player, const char *message, gpointer user_data)
{
    MainWindow *mw = user_data;
    gboolean missing_plugin = syncview_video_player_last_error_is_missing_plugin(player);
    char *detail = g_strdup_printf("%s\n%s", mw->shown_name ? mw->shown_name : "", message);

    set_state(mw, SYNCVIEW_MAIN_WINDOW_ERROR, missing_plugin ? "Manca un decoder per questo video" : "Impossibile riprodurre il video",
              detail);
    g_free(detail);

    if (missing_plugin) {
        /* Non dentro il segnale del player: il controllo parte appena il main loop è libero. */
        GWeakRef *ref = g_new0(GWeakRef, 1);

        g_weak_ref_init(ref, mw->window);
        g_idle_add(on_missing_plugin_idle, ref);
    }
}

/* ---- Azioni ---- */

static gboolean
is_loaded(MainWindow *mw)
{
    return mw->player && mw->state == SYNCVIEW_MAIN_WINDOW_LOADED;
}

static void
do_toggle_play(MainWindow *mw)
{
    GError *error = NULL;

    if (is_loaded(mw) && !syncview_video_player_toggle_play_pause(mw->player, &error)) {
        log_error("Play/pausa non riuscito", error);
        g_clear_error(&error);
    }
}

static void
do_step(MainWindow *mw, int frames)
{
    GError *error = NULL;

    if (is_loaded(mw) && !syncview_video_player_step_frames(mw->player, frames, &error)) {
        log_error("Passo per frame non riuscito", error);
        g_clear_error(&error);
    }
}

static void
do_seek(MainWindow *mw, gint64 position_ms)
{
    GError *error = NULL;

    if (is_loaded(mw) && !syncview_video_player_seek(mw->player, position_ms, &error)) {
        log_error("Seek non riuscito", error);
        g_clear_error(&error);
    }
}

static void
on_play_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    do_toggle_play(user_data);
}

static void
on_step_clicked(GtkButton *button, gpointer user_data)
{
    MainWindow *mw = user_data;

    for (int i = 0; i < 4; i++) {
        if (GTK_WIDGET(button) == mw->btn_step[i]) {
            do_step(mw, STEP_VALUES[i]);
            return;
        }
    }
}

static gboolean
on_scale_change_value(GtkRange *range, GtkScrollType scroll, double value, gpointer user_data)
{
    MainWindow *mw = user_data;

    (void)range;
    (void)scroll;
    /* Solo l'utente scatena questo segnale (gli aggiornamenti dal player usano set_value): seek alla posizione scelta. */
    if (!mw->updating_scale) {
        do_seek(mw, (gint64)value);
    }
    return FALSE;
}

/* ---- Apri file ---- */

gboolean
syncview_main_window_open_file(GtkWidget *window, const char *path)
{
    MainWindow *mw = mw_of(window);
    GError *error = NULL;

    g_return_val_if_fail(mw != NULL && path != NULL, FALSE);
    if (!mw->player) {
        return FALSE;
    }

    g_free(mw->shown_name);
    mw->shown_name = g_path_get_basename(path);
    log_user_action("Apri video", mw->shown_name);
    set_state(mw, SYNCVIEW_MAIN_WINDOW_LOADING, "Analisi del file…", mw->shown_name);

    if (!syncview_video_player_load(mw->player, path, &error)) {
        gboolean missing = g_error_matches(error, SYNCVIEW_VIDEO_PLAYER_ERROR, SYNCVIEW_VIDEO_PLAYER_ERROR_FILE_NOT_FOUND);

        set_state(mw, SYNCVIEW_MAIN_WINDOW_ERROR, missing ? "File spostato o non trovato" : "Impossibile aprire il video",
                  missing ? path : error->message);
        g_clear_error(&error);
        return FALSE;
    }
    return TRUE;
}

static void
on_file_chosen(GObject *source, GAsyncResult *result, gpointer user_data)
{
    GWeakRef *ref = user_data;
    GtkWidget *window = g_weak_ref_get(ref);
    GFile *file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), result, NULL);  /* NULL se annullato */

    if (window && file) {
        char *path = g_file_get_path(file);

        if (path) {
            syncview_main_window_open_file(window, path);
        }
        g_free(path);
    }
    g_clear_object(&file);
    g_clear_object(&window);
    g_weak_ref_clear(ref);
    g_free(ref);
}

static void
do_open_dialog(MainWindow *mw)
{
    GtkFileDialog *dialog = gtk_file_dialog_new();
    GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    GtkFileFilter *videos = gtk_file_filter_new();
    const char *suffixes[] = { "mp4", "mkv", "mov", "avi", "webm", "m4v", "ts", NULL };

    /* Suffissi e non tipi MIME: su Windows i MIME farebbero ripiegare GTK sul dialogo non nativo. */
    gtk_file_filter_set_name(videos, "Video (mp4, mkv, mov, avi, webm…)");
    for (int i = 0; suffixes[i]; i++) {
        gtk_file_filter_add_suffix(videos, suffixes[i]);
    }
    g_list_store_append(filters, videos);
    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
    gtk_file_dialog_set_title(dialog, "Apri video");

    const char *saved = user_paths_get_video_path(mw->paths, VIDEO_SLOT);

    if (saved) {
        char *dir = g_path_get_dirname(saved);
        GFile *folder = g_file_new_for_path(dir);

        gtk_file_dialog_set_initial_folder(dialog, folder);
        g_object_unref(folder);
        g_free(dir);
    }

    GWeakRef *ref = g_new0(GWeakRef, 1);

    g_weak_ref_init(ref, mw->window);
    gtk_file_dialog_open(dialog, GTK_WINDOW(mw->window), NULL, on_file_chosen, ref);
    g_object_unref(filters);
    g_object_unref(videos);
    g_object_unref(dialog);
}

static void
on_open_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    do_open_dialog(user_data);
}

/* ---- Scorciatoie ---- */

static gboolean
sc_toggle_play(GtkWidget *widget, GVariant *args, gpointer user_data)
{
    (void)widget;
    (void)args;
    do_toggle_play(user_data);
    return TRUE;
}

static gboolean
sc_open(GtkWidget *widget, GVariant *args, gpointer user_data)
{
    (void)widget;
    (void)args;
    do_open_dialog(user_data);
    return TRUE;
}

static gboolean
sc_frame_back(GtkWidget *widget, GVariant *args, gpointer user_data)
{
    (void)widget;
    (void)args;
    do_step(user_data, -1);
    return TRUE;
}

static gboolean
sc_frame_forward(GtkWidget *widget, GVariant *args, gpointer user_data)
{
    (void)widget;
    (void)args;
    do_step(user_data, 1);
    return TRUE;
}

static gboolean
sc_frame_back10(GtkWidget *widget, GVariant *args, gpointer user_data)
{
    (void)widget;
    (void)args;
    do_step(user_data, -10);
    return TRUE;
}

static gboolean
sc_frame_forward10(GtkWidget *widget, GVariant *args, gpointer user_data)
{
    (void)widget;
    (void)args;
    do_step(user_data, 10);
    return TRUE;
}

static gboolean
sc_to_start(GtkWidget *widget, GVariant *args, gpointer user_data)
{
    (void)widget;
    (void)args;
    do_seek(user_data, 0);
    return TRUE;
}

static gboolean
sc_to_end(GtkWidget *widget, GVariant *args, gpointer user_data)
{
    MainWindow *mw = user_data;

    (void)widget;
    (void)args;
    do_seek(mw, mw->duration_ms);
    return TRUE;
}

static void
add_shortcut(GtkShortcutController *controller, guint keyval, GdkModifierType modifiers, GtkShortcutFunc func,
             gpointer data)
{
    gtk_shortcut_controller_add_shortcut(
        controller, gtk_shortcut_new(gtk_keyval_trigger_new(keyval, modifiers), gtk_callback_action_new(func, data, NULL)));
}

static void
install_shortcuts(MainWindow *mw)
{
    GtkEventController *controller = gtk_shortcut_controller_new();

    /* In fase di cattura: Spazio e frecce valgono anche con il focus su un pulsante o sulla barra. */
    gtk_event_controller_set_propagation_phase(controller, GTK_PHASE_CAPTURE);
    GtkShortcutController *sc = GTK_SHORTCUT_CONTROLLER(controller);

    add_shortcut(sc, GDK_KEY_space, 0, sc_toggle_play, mw);
    add_shortcut(sc, GDK_KEY_o, GDK_CONTROL_MASK, sc_open, mw);
    add_shortcut(sc, GDK_KEY_Left, 0, sc_frame_back, mw);
    add_shortcut(sc, GDK_KEY_Right, 0, sc_frame_forward, mw);
    add_shortcut(sc, GDK_KEY_Left, GDK_SHIFT_MASK, sc_frame_back10, mw);
    add_shortcut(sc, GDK_KEY_Right, GDK_SHIFT_MASK, sc_frame_forward10, mw);
    add_shortcut(sc, GDK_KEY_Home, 0, sc_to_start, mw);
    add_shortcut(sc, GDK_KEY_End, 0, sc_to_end, mw);
    gtk_widget_add_controller(mw->window, controller);
}

/* ---- Costruzione ---- */

static GtkWidget *
make_shortcuts_bar(void)
{
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    const char *items[][2] = {
        { "Spazio", "play" }, { "←/→", "frame" },  { "Shift+←/→", "±10 frame" },
        { "Home/Fine", "inizio/fine" }, { "Ctrl+O", "apri" },
    };

    gtk_widget_add_css_class(bar, "sv-shortcuts");
    for (guint i = 0; i < G_N_ELEMENTS(items); i++) {
        GtkWidget *item = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

        gtk_box_append(GTK_BOX(item), make_label(items[i][0], "sv-key"));
        gtk_box_append(GTK_BOX(item), make_label(items[i][1], NULL));
        gtk_box_append(GTK_BOX(bar), item);
    }
    return bar;
}

static GtkWidget *
make_chip(const char *text, const char *extra_class, GtkAlign halign, GtkAlign valign)
{
    GtkWidget *chip = gtk_label_new(text);

    gtk_widget_add_css_class(chip, "sv-chip");
    if (extra_class) {
        gtk_widget_add_css_class(chip, extra_class);
    }
    gtk_widget_set_halign(chip, halign);
    gtk_widget_set_valign(chip, valign);
    gtk_widget_set_margin_start(chip, 10);
    gtk_widget_set_margin_end(chip, 10);
    gtk_widget_set_margin_top(chip, 10);
    gtk_widget_set_margin_bottom(chip, 10);
    return chip;
}

static GtkWidget *
build_tile(MainWindow *mw)
{
    GtkWidget *overlay = gtk_overlay_new();

    gtk_widget_add_css_class(overlay, "sv-tile");
    gtk_widget_set_hexpand(overlay, TRUE);
    gtk_widget_set_vexpand(overlay, TRUE);
    gtk_widget_set_size_request(overlay, 560, 315);
    gtk_widget_set_overflow(overlay, GTK_OVERFLOW_HIDDEN);

    mw->picture = mw->player ? gtk_picture_new_for_paintable(syncview_video_player_get_paintable(mw->player))
                             : gtk_picture_new();
    gtk_widget_add_css_class(mw->picture, "sv-video");
    gtk_picture_set_content_fit(GTK_PICTURE(mw->picture), GTK_CONTENT_FIT_CONTAIN);
    gtk_widget_set_hexpand(mw->picture, TRUE);
    gtk_widget_set_vexpand(mw->picture, TRUE);
    gtk_overlay_set_child(GTK_OVERLAY(overlay), mw->picture);

    /* Identità del canale: colore, forma e lettera (mai il solo colore). */
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay),
                            make_chip("● A · FEED-1", "sv-chip-ch-a", GTK_ALIGN_START, GTK_ALIGN_START));
    mw->chip_fps = make_chip("", NULL, GTK_ALIGN_END, GTK_ALIGN_START);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), mw->chip_fps);
    mw->chip_time = make_chip("00:00.000", "sv-chip-time", GTK_ALIGN_START, GTK_ALIGN_END);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), mw->chip_time);
    mw->chip_end = make_chip("Fine del video", "sv-chip-end", GTK_ALIGN_END, GTK_ALIGN_END);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), mw->chip_end);

    /* Scheda di stato al centro: nessun video / caricamento / errore. */
    mw->card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(mw->card, "sv-card");
    gtk_widget_set_halign(mw->card, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(mw->card, GTK_ALIGN_CENTER);
    mw->card_title = register_widget(mw, "card-title", make_label("Nessun video", "sv-card-title"));
    mw->card_detail = register_widget(mw, "card-detail", make_label("Carica un video per iniziare", "sv-card-detail"));
    gtk_label_set_wrap(GTK_LABEL(mw->card_detail), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(mw->card_detail), 48);
    mw->card_progress = gtk_progress_bar_new();
    mw->card_button = register_widget(mw, "open", gtk_button_new_with_label("Carica video…  Ctrl+O"));
    gtk_widget_add_css_class(mw->card_button, "sv-btn");
    gtk_widget_set_halign(mw->card_button, GTK_ALIGN_START);
    g_signal_connect(mw->card_button, "clicked", G_CALLBACK(on_open_clicked), mw);
    gtk_box_append(GTK_BOX(mw->card), mw->card_title);
    gtk_box_append(GTK_BOX(mw->card), mw->card_detail);
    gtk_box_append(GTK_BOX(mw->card), mw->card_progress);
    gtk_box_append(GTK_BOX(mw->card), mw->card_button);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), mw->card);
    return overlay;
}

static GtkWidget *
build_controls(MainWindow *mw)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *times = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

    gtk_widget_set_margin_top(box, 12);
    gtk_widget_set_margin_bottom(box, 12);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);

    mw->time_label = register_widget(mw, "time", make_label("00:00.000", "sv-time"));
    mw->total_label = make_label("/ 00:00.000", "sv-time-total");
    gtk_widget_set_valign(mw->total_label, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(times), mw->time_label);
    gtk_box_append(GTK_BOX(times), mw->total_label);

    mw->scale = register_widget(mw, "seek", gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 1.0, 1.0));
    gtk_scale_set_draw_value(GTK_SCALE(mw->scale), FALSE);
    gtk_widget_add_css_class(mw->scale, "sv-seek");
    gtk_widget_set_hexpand(mw->scale, TRUE);
    g_signal_connect(mw->scale, "change-value", G_CALLBACK(on_scale_change_value), mw);

    gtk_widget_set_halign(buttons, GTK_ALIGN_CENTER);
    for (int i = 0; i < 4; i++) {
        mw->btn_step[i] = register_widget(mw, STEP_NAMES[i], gtk_button_new_with_label(STEP_LABELS[i]));
        gtk_widget_add_css_class(mw->btn_step[i], "sv-step");
        {
            char *tip = g_strdup_printf("%d frame %s", abs(STEP_VALUES[i]), STEP_VALUES[i] < 0 ? "indietro" : "avanti");

            gtk_widget_set_tooltip_text(mw->btn_step[i], tip);
            g_free(tip);
        }
        g_signal_connect(mw->btn_step[i], "clicked", G_CALLBACK(on_step_clicked), mw);
    }
    mw->btn_play = register_widget(mw, "play", gtk_button_new_with_label("Play"));
    gtk_widget_add_css_class(mw->btn_play, "sv-btn");
    gtk_widget_add_css_class(mw->btn_play, "sv-primary");
    g_signal_connect(mw->btn_play, "clicked", G_CALLBACK(on_play_clicked), mw);

    gtk_box_append(GTK_BOX(buttons), mw->btn_step[0]);
    gtk_box_append(GTK_BOX(buttons), mw->btn_step[1]);
    gtk_box_append(GTK_BOX(buttons), mw->btn_play);
    gtk_box_append(GTK_BOX(buttons), mw->btn_step[2]);
    gtk_box_append(GTK_BOX(buttons), mw->btn_step[3]);

    gtk_box_append(GTK_BOX(box), times);
    gtk_box_append(GTK_BOX(box), mw->scale);
    gtk_box_append(GTK_BOX(box), buttons);
    return box;
}

static void
on_menu_deps_clicked(GtkButton *button, gpointer user_data)
{
    MainWindow *mw = user_data;
    GtkWidget *popover = gtk_widget_get_ancestor(GTK_WIDGET(button), GTK_TYPE_POPOVER);

    if (popover) {
        gtk_popover_popdown(GTK_POPOVER(popover));
    }
    log_user_action("Menu: verifica dipendenze", NULL);
    start_deps_check(mw, SYNCVIEW_DEPS_SHOW_USER_REQUESTED);
}

static GtkWidget *
build_menu(MainWindow *mw)
{
    GtkWidget *menu = gtk_menu_button_new();
    GtkWidget *popover = gtk_popover_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *deps = gtk_button_new_with_label("Verifica dipendenze…");

    gtk_menu_button_set_label(GTK_MENU_BUTTON(menu), "Menu");
    gtk_widget_add_css_class(menu, "sv-btn");
    gtk_widget_add_css_class(deps, "sv-btn");
    gtk_widget_add_css_class(popover, "syncview");
    g_signal_connect(deps, "clicked", G_CALLBACK(on_menu_deps_clicked), mw);
    gtk_box_append(GTK_BOX(box), deps);
    gtk_popover_set_child(GTK_POPOVER(popover), box);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(menu), popover);
    register_widget(mw, "menu", menu);
    register_widget(mw, "menu-deps", deps);
    return menu;
}

static GtkWidget *
build_header(MainWindow *mw)
{
    GtkWidget *header = gtk_header_bar_new();
    GtkWidget *title_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *open = gtk_button_new_with_label("Apri video…");

    gtk_widget_add_css_class(open, "sv-btn");
    g_signal_connect(open, "clicked", G_CALLBACK(on_open_clicked), mw);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), open);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), build_menu(mw));

    gtk_box_append(GTK_BOX(title_box), make_label("SyncView", "sv-title"));
    mw->subtitle = make_label("", "sv-subtitle");
    gtk_box_append(GTK_BOX(title_box), mw->subtitle);
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header), title_box);
    return header;
}

static void
on_window_destroy(GtkWidget *window, gpointer user_data)
{
    MainWindow *mw = user_data;

    (void)window;
    if (mw->pulse_id) {
        g_source_remove(mw->pulse_id);
    }
    if (mw->player) {
        g_signal_handlers_disconnect_by_data(mw->player, mw);
        g_object_unref(mw->player);  /* se la pipeline sta ancora salendo, lo smontaggio è rimandato (vedi video_player.h) */
    }
    user_paths_free(mw->paths);
    syncview_deps_dialog_options_free(mw->deps_options);
    g_hash_table_destroy(mw->widgets);
    g_free(mw->shown_name);
    g_free(mw);
    log_ui("Finestra principale chiusa");
}

GtkWidget *
syncview_main_window_new(GtkApplication *app, const char *user_paths_file)
{
    MainWindow *mw = g_new0(MainWindow, 1);
    GError *error = NULL;

    syncview_theme_init(gdk_display_get_default());

    mw->widgets = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    mw->window = gtk_application_window_new(app);
    mw->state = SYNCVIEW_MAIN_WINDOW_EMPTY;
    gtk_window_set_title(GTK_WINDOW(mw->window), "SyncView");
    gtk_window_set_default_size(GTK_WINDOW(mw->window), 1000, 680);
    gtk_widget_add_css_class(mw->window, "syncview");
    g_object_set_data(G_OBJECT(mw->window), MAIN_WINDOW_DATA, mw);

    char *default_file = user_paths_file ? NULL : user_paths_default_file();

    mw->paths = user_paths_new(user_paths_file ? user_paths_file : default_file);
    g_free(default_file);

    mw->player = syncview_video_player_new(VIDEO_SLOT, &error);

    gtk_window_set_titlebar(GTK_WINDOW(mw->window), build_header(mw));

    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *tile = build_tile(mw);

    gtk_widget_set_margin_top(tile, 16);
    gtk_widget_set_margin_start(tile, 16);
    gtk_widget_set_margin_end(tile, 16);
    gtk_box_append(GTK_BOX(content), tile);
    gtk_box_append(GTK_BOX(content), build_controls(mw));
    gtk_box_append(GTK_BOX(content), make_shortcuts_bar());
    gtk_window_set_child(GTK_WINDOW(mw->window), content);

    install_shortcuts(mw);
    g_signal_connect(mw->window, "destroy", G_CALLBACK(on_window_destroy), mw);

    if (mw->player) {
        g_signal_connect(mw->player, "position-changed", G_CALLBACK(on_position_changed), mw);
        g_signal_connect(mw->player, "duration-changed", G_CALLBACK(on_duration_changed), mw);
        g_signal_connect(mw->player, "playback-state-changed", G_CALLBACK(on_playback_state_changed), mw);
        g_signal_connect(mw->player, "load-state-changed", G_CALLBACK(on_load_state_changed), mw);
        g_signal_connect(mw->player, "error", G_CALLBACK(on_player_error), mw);
        syncview_video_player_set_tick_widget(mw->player, mw->picture);
        set_state(mw, SYNCVIEW_MAIN_WINDOW_EMPTY, "Nessun video", "Carica un video per iniziare");
    } else {
        /* Manca un componente (tipicamente gtk4paintablesink): niente riproduzione; il controllo d'avvio propone l'installazione (M2.12). */
        set_state(mw, SYNCVIEW_MAIN_WINDOW_ERROR, "Manca un componente per leggere i video", error ? error->message : "");
        gtk_widget_set_sensitive(mw->card_button, FALSE);
        log_error("Player non disponibile", error);
    }
    g_clear_error(&error);

    log_ui("Finestra principale creata");

    /* Come l'originale: all'avvio si ricarica il video salvato, se esiste ancora. */
    const char *valid[SYNCVIEW_MAX_VIDEOS];
    GError *save_error = NULL;

    user_paths_get_valid_video_paths(mw->paths, valid, &save_error);
    if (save_error) {
        log_error("Impossibile aggiornare il file dei percorsi", save_error);
        g_clear_error(&save_error);
    }
    if (mw->player && valid[VIDEO_SLOT]) {
        log_user_action("Ricarico il video salvato", valid[VIDEO_SLOT]);
        syncview_main_window_open_file(mw->window, valid[VIDEO_SLOT]);
    }
    return mw->window;
}

void
syncview_main_window_check_dependencies(GtkWidget *window, gboolean user_requested)
{
    MainWindow *mw = mw_of(window);

    g_return_if_fail(mw != NULL);
    start_deps_check(mw, user_requested ? SYNCVIEW_DEPS_SHOW_USER_REQUESTED : SYNCVIEW_DEPS_SHOW_STARTUP);
}

void
syncview_main_window_set_deps_options(GtkWidget *window, const SyncviewDepsDialogOptions *options)
{
    MainWindow *mw = mw_of(window);

    g_return_if_fail(mw != NULL);
    syncview_deps_dialog_options_free(mw->deps_options);
    mw->deps_options = syncview_deps_dialog_options_dup(options);
}

SyncviewMainWindowState
syncview_main_window_get_state(GtkWidget *window)
{
    MainWindow *mw = mw_of(window);

    g_return_val_if_fail(mw != NULL, SYNCVIEW_MAIN_WINDOW_EMPTY);
    return mw->state;
}

SyncviewVideoPlayer *
syncview_main_window_get_player(GtkWidget *window)
{
    MainWindow *mw = mw_of(window);

    g_return_val_if_fail(mw != NULL, NULL);
    return mw->player;
}

GtkWidget *
syncview_main_window_get_widget(GtkWidget *window, const char *name)
{
    MainWindow *mw = mw_of(window);

    g_return_val_if_fail(mw != NULL && name != NULL, NULL);
    return g_hash_table_lookup(mw->widgets, name);
}
