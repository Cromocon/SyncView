/*
 * M2.12: finestra «Preparazione di SyncView» (primo avvio / verifica dipendenze). Richiede un display (GTK); non serve
 * GStreamer per i video. L'installatore è un pkexec finto (script), il sistema è finto (sonde iniettate): nessun pacchetto
 * viene davvero installato e non si chiede nessuna password.
 */
#include "core/deps_state.h"
#include "ui/deps_dialog.h"
#include "ui/theme.h"

#include <assert.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SKIP_EXIT 77
#define HAS(text, needle) (strstr((text), (needle)) != NULL)

static char *tmp_dir;
static char *fake_pkexec_path;
static char *fake_log_path;
static char *marker_path;    /* creato dal finto pkexec quando «installa»: da lì in poi il sistema finto è completo */
static char *state_path;
static gint check_calls;

/* ---- Sistema finto ---- */

static gboolean
sys_has_element(const char *name, gpointer user_data)
{
    (void)user_data;
    if (g_file_test(marker_path, G_FILE_TEST_EXISTS)) {
        return TRUE;  /* «installato»: c'è tutto */
    }

    /* Prima: manca il sink GTK4 e il decoder H.265; il resto c'è. */
    const char *present[] = { "playbin3", "qtdemux", "avidemux", "matroskademux", "asfdemux", "flvdemux", "avdec_h264",
                              "vp9dec", "av1dec", "vah264dec", NULL };

    for (int i = 0; present[i]; i++) {
        if (strcmp(name, present[i]) == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

static char *
sys_find_program(const char *name, const char *extra_dir, gpointer user_data)
{
    (void)extra_dir;
    (void)user_data;
    if (strcmp(name, "pacman") == 0) {
        return g_strdup("/usr/bin/pacman");
    }
    if (strcmp(name, "ffmpeg") == 0 && g_file_test(marker_path, G_FILE_TEST_EXISTS)) {
        return g_strdup("/usr/bin/ffmpeg");
    }
    return NULL;
}

static char *
sys_run_program(const char *path, const char *const *args, gpointer user_data)
{
    (void)path;
    (void)user_data;
    if (args[0] && strcmp(args[0], "-version") == 0) {
        return g_strdup("ffmpeg version 7.1.1 Copyright\n");
    }
    return g_strdup(" V....D libx264 libx264 H.264 (codec h264)\n");
}

static DepsReport *
fake_check(gpointer user_data)
{
    DepsProbes probes = { sys_has_element, sys_find_program, sys_run_program, DEPS_PLATFORM_LINUX, user_data };

    g_atomic_int_inc(&check_calls);
    return deps_check_run(&probes, "/deps");
}

/* ---- Strumenti ---- */

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
spin_until_phase(GtkWidget *dialog, SyncviewDepsDialogPhase phase, int timeout_ms)
{
    gint64 end = g_get_monotonic_time() + (gint64)timeout_ms * 1000;

    while (syncview_deps_dialog_get_phase(dialog) != phase && g_get_monotonic_time() < end) {
        g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
    return syncview_deps_dialog_get_phase(dialog) == phase;
}

static GtkWidget *
W(GtkWidget *dialog, const char *name)
{
    GtkWidget *widget = syncview_deps_dialog_get_widget(dialog, name);

    if (!widget) {
        g_printerr("widget «%s» assente (fase %d)\n", name, syncview_deps_dialog_get_phase(dialog));
    }
    assert(widget != NULL);
    return widget;
}

static const char *
label_text(GtkWidget *dialog, const char *name)
{
    return gtk_label_get_text(GTK_LABEL(W(dialog, name)));
}

static void
click(GtkWidget *dialog, const char *name)
{
    GtkWidget *button = W(dialog, name);

    assert(gtk_widget_get_sensitive(button));
    g_signal_emit_by_name(button, "clicked");  /* gtk_widget_activate ritarda il clic di ~250 ms */
}

static char *
text_of_buffer(GtkWidget *view)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    GtkTextIter start, end;

    gtk_text_buffer_get_bounds(buffer, &start, &end);
    return gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
}

static char *
read_file(const char *path)
{
    char *text = NULL;

    return g_file_get_contents(path, &text, NULL, NULL) ? text : NULL;
}

/* ---- Esito ---- */

typedef struct {
    gboolean called;
    SyncviewDepsOutcome outcome;
} Done;

static void
on_done(GtkWidget *dialog, SyncviewDepsOutcome outcome, gpointer user_data)
{
    Done *done = user_data;

    (void)dialog;
    done->called = TRUE;
    done->outcome = outcome;
}

static void
fake_reset(const char *mode)
{
    g_remove(marker_path);
    g_remove(fake_log_path);
    g_remove(state_path);
    g_setenv("FAKE_MODE", mode, TRUE);
}

static GtkWidget *
open_dialog(Done *done, GtkWindow *parent)
{
    DepRunOptions run = { 0 };
    SyncviewDepsDialogOptions options = { 0 };

    run.elevation_program = fake_pkexec_path;
    options.check = fake_check;
    options.run_options = &run;
    options.state_file = state_path;
    options.done = on_done;
    options.done_data = done;
    memset(done, 0, sizeof *done);

    GtkWidget *dialog = syncview_deps_dialog_new(parent, fake_check(NULL), &options);

    gtk_window_present(GTK_WINDOW(dialog));
    return dialog;
}

static void
close_dialog(GtkWidget *dialog)
{
    gtk_window_close(GTK_WINDOW(dialog));
    spin_for(50);
}

/* ---- Test ---- */

static void
test_review_shows_exact_plan_and_does_nothing_without_consent(void)
{
    fake_reset("ok");
    Done done;
    GtkWidget *dialog = open_dialog(&done, NULL);

    assert(syncview_deps_dialog_get_phase(dialog) == SYNCVIEW_DEPS_DIALOG_REVIEW);

    /* Cosa manca, riga per riga. */
    assert(strcmp(label_text(dialog, "badge:gst-playbin3"), "✓ Installato") == 0);
    assert(strcmp(label_text(dialog, "badge:gst-gtk4sink"), "Da installare") == 0);
    assert(strcmp(label_text(dialog, "badge:ffmpeg"), "Da installare") == 0);
    assert(strcmp(label_text(dialog, "badge:gst-decoder-hevc"), "Da installare") == 0);

    /* Elenco ESATTO di ciò che verrà installato e avviso sulla password (gestita dal sistema). */
    const char *plan = label_text(dialog, "plan");

    assert(HAS(plan, "pacman") && HAS(plan, "gst-plugin-gtk4") && HAS(plan, "ffmpeg") && HAS(plan, "gst-libav"));
    const char *note = label_text(dialog, "note");

    assert(HAS(note, "password") && HAS(note, "non la vede e non la salva"));
    assert(HAS(note, "non può riprodurre nessun video"));  /* manca il sink: nessun video senza */

    /* Nessuna installazione senza consenso: aspettare non cambia nulla. */
    spin_for(400);
    assert(syncview_deps_dialog_get_phase(dialog) == SYNCVIEW_DEPS_DIALOG_REVIEW);
    assert(!g_file_test(fake_log_path, G_FILE_TEST_EXISTS) && !g_file_test(marker_path, G_FILE_TEST_EXISTS));

    /* Gli opzionali si possono escludere: il piano cambia di conseguenza. */
    gtk_check_button_set_active(GTK_CHECK_BUTTON(W(dialog, "opt-check")), FALSE);
    plan = label_text(dialog, "plan");
    assert(HAS(plan, "gst-plugin-gtk4") && HAS(plan, "ffmpeg") && !HAS(plan, "gst-libav"));
    assert(strcmp(label_text(dialog, "badge:gst-decoder-hevc"), "Opzionale, non installato") == 0);

    /* Annulla: chiude senza ricordare nulla e senza installare. */
    click(dialog, "cancel");
    spin_for(50);
    assert(done.called && done.outcome == SYNCVIEW_DEPS_OUTCOME_CLOSED);
    DepsState *state = deps_state_load(state_path);

    assert(deps_state_get_declined(state) == NULL);
    deps_state_free(state);
    assert(!g_file_test(marker_path, G_FILE_TEST_EXISTS));
}

static void
test_install_success_flow(void)
{
    fake_reset("ok");
    Done done;
    GtkWidget *dialog = open_dialog(&done, NULL);

    click(dialog, "install");
    assert(syncview_deps_dialog_get_phase(dialog) == SYNCVIEW_DEPS_DIALOG_RUNNING);
    assert(W(dialog, "cancel-install") != NULL && W(dialog, "output") != NULL);

    assert(spin_until_phase(dialog, SYNCVIEW_DEPS_DIALOG_SUCCESS, 20000));
    spin_for(50);

    /* argv esatto passato al finto pkexec: gestore + opzioni non interattive + pacchetti dalla tabella interna. */
    char *log = read_file(fake_log_path);

    assert(log && HAS(log, "pacman\n-S\n--noconfirm\n--needed\n"));
    assert(HAS(log, "gst-plugin-gtk4") && HAS(log, "gst-libav") && HAS(log, "ffmpeg"));
    g_free(log);

    /* Ricontrollo finale: tutto a posto, righe verdi, nessun ritorno alla schermata iniziale. */
    assert(deps_report_is_complete(syncview_deps_dialog_get_report(dialog)));
    assert(strcmp(label_text(dialog, "badge:gst-gtk4sink"), "✓ Installato") == 0);
    assert(HAS(label_text(dialog, "banner"), "Tutto pronto"));

    click(dialog, "close");
    spin_for(50);
    assert(done.called && done.outcome == SYNCVIEW_DEPS_OUTCOME_INSTALLED);
}

static void
test_output_is_streamed_while_running(void)
{
    fake_reset("ok");
    Done done;
    GtkWidget *dialog = open_dialog(&done, NULL);

    click(dialog, "install");

    /* L'output compare nella schermata RUNNING prima della fine (la vista è quella della fase in corso). */
    GtkWidget *view = W(dialog, "output");
    char *text = NULL;
    gint64 end = g_get_monotonic_time() + 15 * G_USEC_PER_SEC;

    while (g_get_monotonic_time() < end) {
        g_free(text);
        text = text_of_buffer(view);
        if (HAS(text, "installazione completata")) {
            break;
        }
        g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
    assert(text && HAS(text, "scarico i pacchetti") && HAS(text, "installazione completata"));
    g_free(text);
    assert(spin_until_phase(dialog, SYNCVIEW_DEPS_DIALOG_SUCCESS, 20000));
    close_dialog(dialog);
}

static void
test_denied_is_not_retried_automatically(void)
{
    fake_reset("denied");
    Done done;
    GtkWidget *dialog = open_dialog(&done, NULL);

    click(dialog, "install");
    assert(spin_until_phase(dialog, SYNCVIEW_DEPS_DIALOG_FAILED, 20000));
    assert(HAS(label_text(dialog, "banner"), "negata") && HAS(label_text(dialog, "banner"), "non è stato modificato nulla"));
    /* Nulla è stato toccato: le righe non dicono «fallita». */
    assert(strcmp(label_text(dialog, "badge:gst-gtk4sink"), "Non installato") == 0);

    /* Nessun nuovo tentativo da solo: un solo invocazione, anche dopo un'attesa. */
    spin_for(600);
    char *log = read_file(fake_log_path);

    assert(log != NULL);
    int runs = 0;

    for (const char *p = log; (p = strstr(p, "--\n")); p += 3) {
        runs++;
    }
    assert(runs == 1);
    g_free(log);
    assert(syncview_deps_dialog_get_phase(dialog) == SYNCVIEW_DEPS_DIALOG_FAILED);

    /* Si riprova solo su richiesta esplicita: torna alla schermata iniziale e l'utente decide di nuovo. */
    click(dialog, "retry");
    assert(syncview_deps_dialog_get_phase(dialog) == SYNCVIEW_DEPS_DIALOG_REVIEW);
    g_setenv("FAKE_MODE", "ok", TRUE);
    click(dialog, "install");
    assert(spin_until_phase(dialog, SYNCVIEW_DEPS_DIALOG_SUCCESS, 20000));
    close_dialog(dialog);
}

static void
test_failure_offers_manual_table(void)
{
    fake_reset("fail");
    Done done;
    GtkWidget *dialog = open_dialog(&done, NULL);

    click(dialog, "install");
    assert(spin_until_phase(dialog, SYNCVIEW_DEPS_DIALOG_FAILED, 20000));
    assert(HAS(label_text(dialog, "banner"), "Installazione fallita") && HAS(label_text(dialog, "banner"), "pacchetto non trovato"));
    assert(strcmp(label_text(dialog, "badge:gst-gtk4sink"), "✕ Installazione fallita") == 0);

    /* Tabella «installazione manuale» con un comando per ogni sistema; il sistema in uso (pacman) è preselezionato. */
    const char *oses[] = { "apt-get", "pacman", "dnf", "zypper", "windows", "macos" };

    for (size_t i = 0; i < G_N_ELEMENTS(oses); i++) {
        char *name = g_strdup_printf("os:%s", oses[i]);

        assert(syncview_deps_dialog_get_widget(dialog, name) != NULL);
        g_free(name);
    }
    GtkWidget *pacman_row = W(dialog, "os:pacman");

    assert(gtk_list_box_row_is_selected(GTK_LIST_BOX_ROW(pacman_row)));
    const char *command = g_object_get_data(G_OBJECT(pacman_row), "copy-text");

    assert(command && HAS(command, "sudo pacman -S") && HAS(command, "gst-plugin-gtk4"));
    const char *apt = g_object_get_data(G_OBJECT(W(dialog, "os:apt-get")), "copy-text");

    assert(apt && HAS(apt, "sudo apt install") && HAS(apt, "gstreamer1.0-gtk4"));
    assert(W(dialog, "copy") != NULL && W(dialog, "retry") != NULL);

    /* «Continua senza» dopo un fallimento: si ricorda la scelta. */
    click(dialog, "continue");
    spin_for(50);
    assert(done.called && done.outcome == SYNCVIEW_DEPS_OUTCOME_CONTINUED_WITHOUT);
}

static void
test_cancel_while_running_and_close_request(void)
{
    fake_reset("hang");
    Done done;
    GtkWidget *dialog = open_dialog(&done, NULL);

    click(dialog, "install");
    spin_for(500);  /* il finto pkexec è partito e dorme */
    assert(syncview_deps_dialog_get_phase(dialog) == SYNCVIEW_DEPS_DIALOG_RUNNING);

    /* Chiudere la finestra durante l'installazione non la interrompe a metà: chiede l'annullamento e resta aperta. */
    gtk_window_close(GTK_WINDOW(dialog));
    assert(gtk_widget_get_visible(dialog) && !done.called);
    assert(HAS(label_text(dialog, "status"), "Annullamento richiesto"));  /* subito: poi il passo termina e cambia schermata */
    assert(!gtk_widget_get_sensitive(W(dialog, "cancel-install")));
    spin_for(100);
    assert(gtk_widget_get_visible(dialog) && !done.called);

    assert(spin_until_phase(dialog, SYNCVIEW_DEPS_DIALOG_FAILED, 20000));
    assert(HAS(label_text(dialog, "banner"), "annullata"));
    assert(strcmp(label_text(dialog, "badge:gst-gtk4sink"), "Non installato") == 0);  /* annullata: nulla toccato */
    close_dialog(dialog);
    assert(done.called && done.outcome == SYNCVIEW_DEPS_OUTCOME_CLOSED);
}

static void
test_manual_path_and_recheck(void)
{
    fake_reset("ok");
    Done done;
    GtkWidget *dialog = open_dialog(&done, NULL);

    click(dialog, "manual");
    assert(syncview_deps_dialog_get_phase(dialog) == SYNCVIEW_DEPS_DIALOG_MANUAL);
    assert(W(dialog, "os:pacman") != NULL && W(dialog, "recheck") != NULL && W(dialog, "back") != NULL);

    /* «Ricontrolla» non cambia nulla finché manca qualcosa: resta nella schermata d'inizio. */
    gint before = g_atomic_int_get(&check_calls);

    click(dialog, "recheck");
    gint64 end = g_get_monotonic_time() + 10 * G_USEC_PER_SEC;

    while (g_atomic_int_get(&check_calls) == before && g_get_monotonic_time() < end) {
        g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
    spin_for(200);
    assert(syncview_deps_dialog_get_phase(dialog) == SYNCVIEW_DEPS_DIALOG_REVIEW);

    /* Installato «a mano» nel frattempo (il marcatore): il ricontrollo trova tutto a posto. */
    click(dialog, "manual");
    assert(g_file_set_contents(marker_path, "x", -1, NULL));
    click(dialog, "recheck");
    assert(spin_until_phase(dialog, SYNCVIEW_DEPS_DIALOG_ALL_OK, 10000));
    assert(HAS(label_text(dialog, "banner"), "a posto"));
    close_dialog(dialog);
}

/* Attende che un controllo asincrono finisca (numero di chiamate cresciuto) e che il main loop abbia agito. */
static void
wait_check_finished(gint before)
{
    gint64 end = g_get_monotonic_time() + 10 * G_USEC_PER_SEC;

    while (g_atomic_int_get(&check_calls) == before && g_get_monotonic_time() < end) {
        g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
    spin_for(400);
}

static void
test_continue_without_is_remembered_across_startups(void)
{
    fake_reset("ok");
    GtkWidget *parent = gtk_window_new();
    DepRunOptions run = { 0 };
    SyncviewDepsDialogOptions options = { 0 };
    Done done;

    memset(&done, 0, sizeof done);
    run.elevation_program = fake_pkexec_path;
    options.check = fake_check;
    options.run_options = &run;
    options.state_file = state_path;
    options.done = on_done;
    options.done_data = &done;

    /* Primo avvio: manca qualcosa, nessuna scelta → il dialogo si apre da solo. */
    gint before = g_atomic_int_get(&check_calls);

    syncview_deps_check_and_show(GTK_WINDOW(parent), SYNCVIEW_DEPS_SHOW_STARTUP, &options);
    wait_check_finished(before);
    GtkWidget *dialog = syncview_deps_dialog_find(GTK_WINDOW(parent));

    assert(dialog != NULL && syncview_deps_dialog_get_phase(dialog) == SYNCVIEW_DEPS_DIALOG_REVIEW);

    /* Un secondo controllo con il dialogo già aperto non ne apre un altro. */
    before = g_atomic_int_get(&check_calls);
    syncview_deps_check_and_show(GTK_WINDOW(parent), SYNCVIEW_DEPS_SHOW_STARTUP, &options);
    spin_for(300);
    assert(syncview_deps_dialog_find(GTK_WINDOW(parent)) == dialog);

    click(dialog, "continue");
    spin_for(100);
    assert(done.called && done.outcome == SYNCVIEW_DEPS_OUTCOME_CONTINUED_WITHOUT);
    assert(syncview_deps_dialog_find(GTK_WINDOW(parent)) == NULL);

    /* Avvio successivo, stessa situazione: scelta rispettata, nessuna finestra. */
    before = g_atomic_int_get(&check_calls);
    syncview_deps_check_and_show(GTK_WINDOW(parent), SYNCVIEW_DEPS_SHOW_STARTUP, &options);
    wait_check_finished(before);
    assert(syncview_deps_dialog_find(GTK_WINDOW(parent)) == NULL);

    /* Ma un errore di plugin mancante nel player riapre comunque la finestra (qualcosa manca davvero). */
    before = g_atomic_int_get(&check_calls);
    syncview_deps_check_and_show(GTK_WINDOW(parent), SYNCVIEW_DEPS_SHOW_AFTER_ERROR, &options);
    wait_check_finished(before);
    dialog = syncview_deps_dialog_find(GTK_WINDOW(parent));
    assert(dialog != NULL);
    close_dialog(dialog);

    /* Se l'insieme dei componenti mancanti cambia, la richiesta si ripresenta: qui si «risolve» tutto → mai più richieste. */
    assert(g_file_set_contents(marker_path, "x", -1, NULL));
    before = g_atomic_int_get(&check_calls);
    syncview_deps_check_and_show(GTK_WINDOW(parent), SYNCVIEW_DEPS_SHOW_STARTUP, &options);
    wait_check_finished(before);
    assert(syncview_deps_dialog_find(GTK_WINDOW(parent)) == NULL);
    DepsState *state = deps_state_load(state_path);

    assert(deps_state_get_declined(state) == NULL);  /* tutto a posto: la vecchia scelta è stata dimenticata */
    deps_state_free(state);

    /* Voce di menu «Verifica dipendenze» con tutto a posto: la finestra c'è comunque (ALL_OK). */
    before = g_atomic_int_get(&check_calls);
    syncview_deps_check_and_show(GTK_WINDOW(parent), SYNCVIEW_DEPS_SHOW_USER_REQUESTED, &options);
    wait_check_finished(before);
    dialog = syncview_deps_dialog_find(GTK_WINDOW(parent));
    assert(dialog != NULL && syncview_deps_dialog_get_phase(dialog) == SYNCVIEW_DEPS_DIALOG_ALL_OK);
    close_dialog(dialog);
    gtk_window_destroy(GTK_WINDOW(parent));
}

static void
test_declined_set_changes_prompt_again(void)
{
    fake_reset("ok");
    GtkWidget *parent = gtk_window_new();
    SyncviewDepsDialogOptions options = { 0 };

    options.check = fake_check;
    options.state_file = state_path;

    /* Scelta ricordata per un insieme diverso da quello attuale (es. prima mancava solo ffmpeg): ora manca altro → si chiede. */
    DepsState *state = deps_state_load(state_path);

    assert(g_file_set_contents(state_path, "{\"version\":1,\"declined_signature\":\"ffmpeg\",\"last_check_unix\":1}", -1, NULL));
    deps_state_free(state);
    gint before = g_atomic_int_get(&check_calls);

    syncview_deps_check_and_show(GTK_WINDOW(parent), SYNCVIEW_DEPS_SHOW_STARTUP, &options);
    wait_check_finished(before);
    GtkWidget *dialog = syncview_deps_dialog_find(GTK_WINDOW(parent));

    assert(dialog != NULL);
    close_dialog(dialog);
    gtk_window_destroy(GTK_WINDOW(parent));
}

/* ---- Immagini per la revisione del design (solo con SYNCVIEW_TEST_SCREENSHOTS=<cartella>): non è un test ---- */

static void
save_png(GtkWidget *dialog, const char *dir, const char *name)
{
    int w = gtk_widget_get_width(dialog), h = gtk_widget_get_height(dialog);

    /* Sul desktop non attivo il frame clock non arriva: il layout dopo un cambio di schermata va forzato a mano. */
    gtk_widget_allocate(dialog, w, h, -1, NULL);
    /* Si ritrae il contenuto, non la finestra: una finestra su un desktop non attivo è «sospesa» e GTK non la disegna più. */
    GtkWidget *content = gtk_window_get_child(GTK_WINDOW(dialog));

    w = gtk_widget_get_width(content);
    h = gtk_widget_get_height(content);
    GtkSnapshot *snapshot = gtk_snapshot_new();

    gtk_widget_snapshot_child(dialog, content, snapshot);
    GskRenderNode *node = gtk_snapshot_free_to_node(snapshot);
    if (!node) {
        g_printerr("immagine %s: snapshot vuoto\n", name);
        return;
    }
    GskRenderer *renderer = gtk_native_get_renderer(GTK_NATIVE(dialog));
    GdkTexture *texture = gsk_renderer_render_texture(renderer, node, NULL);
    char *file = g_strdup_printf("%s/%s.png", dir, name);

    assert(texture != NULL && gdk_texture_save_to_png(texture, file));
    g_printerr("immagine: %s (%dx%d)\n", file, w, h);
    g_free(file);
    g_object_unref(texture);
    gsk_render_node_unref(node);
}

static void
take_screenshots(const char *dir)
{
    for (int dark = 0; dark < 2; dark++) {
        const char *theme = dark ? "dark" : "light";
        char name[64];

        syncview_theme_set_choice(dark ? SYNCVIEW_THEME_DARK : SYNCVIEW_THEME_LIGHT);

        fake_reset("hang");
        Done done;
        GtkWidget *dialog = open_dialog(&done, NULL);

        spin_for(400);
        g_snprintf(name, sizeof name, "deps-1-review-%s", theme);
        save_png(dialog, dir, name);
        click(dialog, "install");
        spin_for(700);
        g_snprintf(name, sizeof name, "deps-2-running-%s", theme);
        save_png(dialog, dir, name);
        click(dialog, "cancel-install");
        assert(spin_until_phase(dialog, SYNCVIEW_DEPS_DIALOG_FAILED, 20000));
        spin_for(300);
        g_snprintf(name, sizeof name, "deps-3-cancelled-%s", theme);
        save_png(dialog, dir, name);
        close_dialog(dialog);

        fake_reset("fail");
        dialog = open_dialog(&done, NULL);
        click(dialog, "install");
        assert(spin_until_phase(dialog, SYNCVIEW_DEPS_DIALOG_FAILED, 20000));
        spin_for(300);
        g_snprintf(name, sizeof name, "deps-4-failed-%s", theme);
        save_png(dialog, dir, name);
        close_dialog(dialog);

        fake_reset("ok");
        dialog = open_dialog(&done, NULL);
        click(dialog, "install");
        assert(spin_until_phase(dialog, SYNCVIEW_DEPS_DIALOG_SUCCESS, 20000));
        spin_for(300);
        g_snprintf(name, sizeof name, "deps-5-success-%s", theme);
        save_png(dialog, dir, name);
        close_dialog(dialog);
    }
}

static void
setup_fake_pkexec(void)
{
    fake_pkexec_path = g_build_filename(tmp_dir, "fake-pkexec.sh", NULL);
    fake_log_path = g_build_filename(tmp_dir, "fake-pkexec.log", NULL);
    marker_path = g_build_filename(tmp_dir, "installed.marker", NULL);
    state_path = g_build_filename(tmp_dir, "deps_state.json", NULL);

    const char *script =
        "#!/bin/sh\n"
        "printf '%s\\n' \"$@\" >> \"$FAKE_LOG\"\n"
        "echo '--' >> \"$FAKE_LOG\"\n"
        "case \"$FAKE_MODE\" in\n"
        "  ok) echo 'scarico i pacchetti'; sleep 0.3; echo 'installazione completata'; touch \"$FAKE_MARKER\"; exit 0;;\n"
        "  fail) echo 'errore: pacchetto non trovato'; exit 1;;\n"
        "  denied) exit 126;;\n"
        "  hang) echo 'avvio'; exec sleep 30;;\n"
        "esac\n"
        "exit 3\n";

    assert(g_file_set_contents(fake_pkexec_path, script, -1, NULL));
    assert(g_chmod(fake_pkexec_path, 0755) == 0);
    g_setenv("FAKE_LOG", fake_log_path, TRUE);
    g_setenv("FAKE_MARKER", marker_path, TRUE);
}

static gboolean
test_selected(const char *name)
{
    const char *only = g_getenv("SYNCVIEW_TEST_ONLY");

    return !only || strstr(name, only) != NULL;
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
#ifdef G_OS_WIN32
    /* L'installatore è un pkexec finto (script sh): su Windows non esiste; l'interfaccia è la stessa e si prova a mano. */
    return SKIP_EXIT;
#endif
    if (!gtk_init_check()) {
        return SKIP_EXIT;  /* nessun display */
    }
    g_log_set_always_fatal(G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);
    g_unsetenv("SYNCVIEW_DEPS_FAKE_MISSING");

    tmp_dir = g_dir_make_tmp("syncview-depsdlg-XXXXXX", NULL);
    assert(tmp_dir != NULL);
    setup_fake_pkexec();

    if (g_getenv("SYNCVIEW_TEST_SCREENSHOTS")) {
        take_screenshots(g_getenv("SYNCVIEW_TEST_SCREENSHOTS"));
        return 0;
    }

    RUN_TEST(test_review_shows_exact_plan_and_does_nothing_without_consent());
    RUN_TEST(test_install_success_flow());
    RUN_TEST(test_output_is_streamed_while_running());
    RUN_TEST(test_denied_is_not_retried_automatically());
    RUN_TEST(test_failure_offers_manual_table());
    RUN_TEST(test_cancel_while_running_and_close_request());
    RUN_TEST(test_manual_path_and_recheck());
    RUN_TEST(test_continue_without_is_remembered_across_startups());
    RUN_TEST(test_declined_set_changes_prompt_again());

    char *cmd = g_strdup_printf("rm -rf '%s'", tmp_dir);
    assert(system(cmd) == 0);
    g_free(cmd);
    return 0;
}
