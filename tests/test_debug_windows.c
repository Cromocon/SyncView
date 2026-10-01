/*
 * M2.9: finestre di debug (Log e Moduli). Richiedono un display GTK: senza, esce con 77 (skip Meson). Non serve
 * GStreamer. Verifica che compaiano solo in modalità debug, che le righe arrivino anche da altri thread senza perdite,
 * i filtri (logger per modulo, vista per modulo e livello), pausa/svuota, il tetto di righe, la chiusura insieme alla
 * finestra principale e la sicurezza dei log emessi dopo la chiusura.
 */
#include "core/logger.h"
#include "ui/debug_log_window.h"
#include "ui/debug_modules_window.h"
#include "ui/debug_windows.h"

#include <assert.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SKIP_EXIT 77

static GtkApplication *app;

static gboolean
test_selected(const char *name)
{
    const char *only = g_getenv("SYNCVIEW_TEST_ONLY");

    return !only || strstr(name, only) != NULL;
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

/* Lascia al main loop il tempo di mostrare tutte le righe in coda (a meno che la finestra sia in pausa). */
static void
settle(GtkWidget *log_window)
{
    gint64 deadline = g_get_monotonic_time() + 10 * G_USEC_PER_SEC;

    do {
        g_main_context_iteration(NULL, FALSE);
        g_usleep(500);
    } while (syncview_debug_log_window_get_pending_count(log_window) > 0 && g_get_monotonic_time() < deadline);
    spin_for(30);
}

static gboolean
line_contains(GtkWidget *log_window, guint index, const char *needle)
{
    const char *message = NULL;

    return syncview_debug_log_window_get_line(log_window, index, NULL, NULL, &message) && strstr(message, needle);
}

static guint
count_lines_containing(GtkWidget *log_window, const char *needle)
{
    guint n = syncview_debug_log_window_get_line_count(log_window), found = 0;

    for (guint i = 0; i < n; i++) {
        found += line_contains(log_window, i, needle) ? 1 : 0;
    }
    return found;
}

static const char *
footer_text(GtkWidget *log_window)
{
    return gtk_label_get_text(GTK_LABEL(syncview_debug_log_window_get_widget(log_window, "footer")));
}

typedef struct {
    GtkWidget *main_window;
    GtkWidget *log;
    GtkWidget *modules;
} Debug;

static Debug
open_debug(void)
{
    Debug d = { 0 };

    logger_init(NULL, TRUE, NULL);
    d.main_window = gtk_application_window_new(app);
    gtk_window_present(GTK_WINDOW(d.main_window));
    assert(syncview_debug_windows_open(app, GTK_WINDOW(d.main_window), &d.log, &d.modules) == 2);
    return d;
}

static void
close_debug(Debug *d)
{
    gtk_window_destroy(GTK_WINDOW(d->main_window));  /* chiude anche Log e Moduli */
    spin_for(50);
    logger_shutdown();
}

/* ---- Test ---- */

static void
test_not_opened_without_debug(void)
{
    GtkWidget *log = (GtkWidget *)0x1, *modules = (GtkWidget *)0x1;
    GtkWidget *main_window = gtk_application_window_new(app);

    logger_init(NULL, FALSE, NULL);
    assert(syncview_debug_windows_open(app, GTK_WINDOW(main_window), &log, &modules) == 0);
    assert(log == NULL && modules == NULL);
    assert(logger_is_debug_mode() == FALSE);
    logger_shutdown();
    gtk_window_destroy(GTK_WINDOW(main_window));
}

static void
test_opens_two_windows_and_shows_lines(void)
{
    Debug d = open_debug();

    assert(d.log != NULL && d.modules != NULL);
    assert(strcmp(gtk_window_get_title(GTK_WINDOW(d.log)), "Debug · Log in tempo reale") == 0);
    assert(strcmp(gtk_window_get_title(GTK_WINDOW(d.modules)), "Debug · Moduli di log") == 0);
    settle(d.log);

    guint before = syncview_debug_log_window_get_line_count(d.log);

    log_user_action("prova-azione", "dettaglio");
    log_ui("prova-ui %d", 42);
    log_sync("prova-sync");
    log_error("prova-errore", NULL);
    settle(d.log);

    assert(syncview_debug_log_window_get_line_count(d.log) == before + 4);
    assert(count_lines_containing(d.log, "prova-azione") == 1);
    assert(count_lines_containing(d.log, "prova-ui 42") == 1);

    /* Livello e modulo di ogni riga sono quelli del logger. */
    guint n = syncview_debug_log_window_get_line_count(d.log);
    LoggerLevel level;
    LoggerModule module;

    for (guint i = before; i < n; i++) {
        assert(syncview_debug_log_window_get_line(d.log, i, &level, &module, NULL));
        if (line_contains(d.log, i, "prova-azione")) {
            assert(level == LOGGER_LEVEL_INFO && module == LOGGER_MODULE_USER);
        } else if (line_contains(d.log, i, "prova-ui")) {
            assert(level == LOGGER_LEVEL_DEBUG && module == LOGGER_MODULE_UI);
        } else if (line_contains(d.log, i, "prova-sync")) {
            assert(level == LOGGER_LEVEL_DEBUG && module == LOGGER_MODULE_SYNC);
        } else if (line_contains(d.log, i, "prova-errore")) {
            assert(level == LOGGER_LEVEL_ERROR && module == LOGGER_MODULE_APP);
        }
    }

    /* L'etichetta del modulo nel testo («[UI] ») non si ripete: c'è già la colonna del modulo. */
    for (guint i = before; i < n; i++) {
        const char *message = NULL;

        syncview_debug_log_window_get_line(d.log, i, NULL, NULL, &message);
        assert(!g_str_has_prefix(message, "[UI] ") && !g_str_has_prefix(message, "[SYNC] "));
    }

    /* Riga di stato: righe e moduli attivi (tutti e 8). */
    assert(strstr(footer_text(d.log), "righe") != NULL);
    assert(strstr(footer_text(d.log), "8 moduli attivi") != NULL);
    assert(strstr(gtk_label_get_text(GTK_LABEL(syncview_debug_log_window_get_widget(d.log, "live"))), "In diretta"));
    assert(strstr(gtk_label_get_text(GTK_LABEL(syncview_debug_log_window_get_widget(d.log, "autoscroll"))), "ON"));
    close_debug(&d);
}

static void
test_module_switches_control_the_logger(void)
{
    Debug d = open_debug();
    GtkWidget *sync_switch = syncview_debug_modules_window_get_widget(d.modules, "switch-SYNC");
    GtkWidget *ui_switch = syncview_debug_modules_window_get_widget(d.modules, "switch-UI");

    assert(sync_switch && ui_switch);
    assert(gtk_switch_get_active(GTK_SWITCH(sync_switch)));  /* all'inizio tutti attivi, come il logger */
    settle(d.log);

    /* Spento il modulo SYNC: le sue righe non arrivano più alla finestra Log (e nemmeno a file/stderr: lo decide il logger). */
    gtk_switch_set_active(GTK_SWITCH(sync_switch), FALSE);
    assert(!logger_is_module_enabled(LOGGER_MODULE_SYNC));
    assert(logger_is_module_enabled(LOGGER_MODULE_UI));
    log_sync("sync-spento");
    log_ui("ui-acceso");
    settle(d.log);
    assert(count_lines_containing(d.log, "sync-spento") == 0);
    assert(count_lines_containing(d.log, "ui-acceso") == 1);

    /* Gli errori non vengono mai filtrati, nemmeno con il modulo spento. */
    gtk_switch_set_active(GTK_SWITCH(ui_switch), FALSE);
    log_ui("ui-spento");
    log_error("errore-sempre", NULL);
    settle(d.log);
    assert(count_lines_containing(d.log, "ui-spento") == 0);
    assert(count_lines_containing(d.log, "errore-sempre") == 1);
    assert(strstr(footer_text(d.log), "6 moduli attivi") != NULL);

    /* Riacceso: le righe tornano. */
    gtk_switch_set_active(GTK_SWITCH(sync_switch), TRUE);
    log_sync("sync-riacceso");
    settle(d.log);
    assert(count_lines_containing(d.log, "sync-riacceso") == 1);

    /* «Tutti» riaccende ogni modulo. */
    g_signal_emit_by_name(syncview_debug_modules_window_get_widget(d.modules, "all"), "clicked");
    assert(logger_is_module_enabled(LOGGER_MODULE_UI) && logger_is_module_enabled(LOGGER_MODULE_SYNC));
    assert(gtk_switch_get_active(GTK_SWITCH(ui_switch)));

    close_debug(&d);
}

static void
test_view_filters_by_level_and_module(void)
{
    Debug d = open_debug();

    settle(d.log);
    g_signal_emit_by_name(syncview_debug_log_window_get_widget(d.log, "clear"), "clicked");
    assert(syncview_debug_log_window_get_line_count(d.log) == 0);

    log_sync("filtro-sync-debug");     /* DEBUG, SYNC */
    log_ui("filtro-ui-debug");         /* DEBUG, UI */
    log_user_action("filtro-info", NULL);  /* INFO, USER */
    log_error("filtro-errore", NULL);  /* ERROR, APP */
    settle(d.log);
    assert(syncview_debug_log_window_get_line_count(d.log) >= 4);
    guint total = syncview_debug_log_window_get_line_count(d.log);

    assert(syncview_debug_log_window_get_visible_count(d.log) == total);

    /* Livello minimo «Info»: spariscono le righe DEBUG dalla vista, non dal modello. */
    gtk_drop_down_set_selected(GTK_DROP_DOWN(syncview_debug_log_window_get_widget(d.log, "level")), 1);
    guint info_visible = syncview_debug_log_window_get_visible_count(d.log);

    assert(info_visible < total);
    assert(syncview_debug_log_window_get_line_count(d.log) == total);

    /* «Errore»: solo gli errori. */
    gtk_drop_down_set_selected(GTK_DROP_DOWN(syncview_debug_log_window_get_widget(d.log, "level")), 3);
    guint err_visible = syncview_debug_log_window_get_visible_count(d.log);

    assert(err_visible >= 1 && err_visible < info_visible);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(syncview_debug_log_window_get_widget(d.log, "level")), 0);
    assert(syncview_debug_log_window_get_visible_count(d.log) == total);

    /* Chip del modulo SYNC spento: le sue righe spariscono dalla vista; il segno di spunta lo mostra a parole. */
    GtkWidget *chip = syncview_debug_log_window_get_widget(d.log, "chip-SYNC");

    assert(strcmp(gtk_button_get_label(GTK_BUTTON(chip)), "✓ SYNC") == 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chip), FALSE);
    assert(strcmp(gtk_button_get_label(GTK_BUTTON(chip)), "SYNC") == 0);
    assert(syncview_debug_log_window_get_visible_count(d.log) == total - 1);
    assert(strstr(footer_text(d.log), "di") != NULL);  /* «N di M righe» */
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chip), TRUE);
    assert(syncview_debug_log_window_get_visible_count(d.log) == total);

    close_debug(&d);
}

static void
test_pause_resume_and_clear(void)
{
    Debug d = open_debug();
    GtkWidget *pause = syncview_debug_log_window_get_widget(d.log, "pause");

    settle(d.log);
    g_signal_emit_by_name(syncview_debug_log_window_get_widget(d.log, "clear"), "clicked");
    log_ui("prima-della-pausa");
    settle(d.log);
    assert(syncview_debug_log_window_get_line_count(d.log) == 1);

    /* In pausa le righe nuove restano in coda. */
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pause), TRUE);
    assert(strstr(gtk_label_get_text(GTK_LABEL(syncview_debug_log_window_get_widget(d.log, "live"))), "In pausa"));
    assert(strcmp(gtk_button_get_label(GTK_BUTTON(pause)), "Riprendi") == 0);
    for (int i = 0; i < 5; i++) {
        log_ui("durante-la-pausa %d", i);
    }
    spin_for(150);
    assert(syncview_debug_log_window_get_line_count(d.log) == 1);
    assert(syncview_debug_log_window_get_pending_count(d.log) == 5);
    assert(strstr(footer_text(d.log), "5 in attesa") != NULL);
    assert(strstr(gtk_label_get_text(GTK_LABEL(syncview_debug_log_window_get_widget(d.log, "autoscroll"))), "OFF"));

    /* Alla ripresa compaiono, nell'ordine di arrivo. */
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pause), FALSE);
    settle(d.log);
    assert(syncview_debug_log_window_get_line_count(d.log) == 6);
    assert(line_contains(d.log, 1, "durante-la-pausa 0") && line_contains(d.log, 5, "durante-la-pausa 4"));
    assert(strstr(gtk_label_get_text(GTK_LABEL(syncview_debug_log_window_get_widget(d.log, "live"))), "In diretta"));

    /* Svuota: modello e coda a zero. */
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pause), TRUE);
    log_ui("da-scartare");
    spin_for(100);
    g_signal_emit_by_name(syncview_debug_log_window_get_widget(d.log, "clear"), "clicked");
    assert(syncview_debug_log_window_get_line_count(d.log) == 0 && syncview_debug_log_window_get_pending_count(d.log) == 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pause), FALSE);
    settle(d.log);
    assert(syncview_debug_log_window_get_line_count(d.log) == 0);

    close_debug(&d);
}

static void
test_line_cap_keeps_the_newest(void)
{
    Debug d = open_debug();

    settle(d.log);
    g_signal_emit_by_name(syncview_debug_log_window_get_widget(d.log, "clear"), "clicked");

    const int total = SYNCVIEW_LOG_WINDOW_MAX_LINES + 1000;

    for (int i = 1; i <= total; i++) {
        log_ui("riga-%d", i);
    }
    settle(d.log);
    assert(syncview_debug_log_window_get_line_count(d.log) == SYNCVIEW_LOG_WINDOW_MAX_LINES);
    char first[32], last[32];

    g_snprintf(first, sizeof first, "riga-%d", total - SYNCVIEW_LOG_WINDOW_MAX_LINES + 1);
    g_snprintf(last, sizeof last, "riga-%d", total);
    assert(line_contains(d.log, 0, first));
    assert(line_contains(d.log, SYNCVIEW_LOG_WINDOW_MAX_LINES - 1, last));
    close_debug(&d);
}

typedef struct {
    int id;
    int count;
} Burst;

static int finished_bursts;

static gpointer
log_burst(gpointer data)
{
    Burst *burst = data;

    for (int i = 0; i < burst->count; i++) {
        log_ui("thread-%d-%d", burst->id, i);
        if (i % 100 == 0) {
            log_gst("gst-%d-%d", burst->id, i);
        }
    }
    g_atomic_int_inc(&finished_bursts);
    return NULL;
}

static void
test_logging_from_other_threads(void)
{
    Debug d = open_debug();
    GThread *threads[4];
    Burst bursts[4];

    settle(d.log);
    g_signal_emit_by_name(syncview_debug_log_window_get_widget(d.log, "clear"), "clicked");

    g_atomic_int_set(&finished_bursts, 0);
    for (int t = 0; t < 4; t++) {
        bursts[t] = (Burst){ t, 800 };
        threads[t] = g_thread_new("log-burst", log_burst, &bursts[t]);
    }
    /* Il main loop gira mentre i thread loggano: nessuna riga persa né fuori posto per thread. */
    while (g_atomic_int_get(&finished_bursts) < 4) {
        g_main_context_iteration(NULL, FALSE);
        g_usleep(200);
    }
    for (int t = 0; t < 4; t++) {
        g_thread_join(threads[t]);
    }
    settle(d.log);

    assert(syncview_debug_log_window_get_dropped_count(d.log) == 0);
    for (int t = 0; t < 4; t++) {
        int next = 0;
        guint n = syncview_debug_log_window_get_line_count(d.log);
        char prefix[32];

        g_snprintf(prefix, sizeof prefix, "thread-%d-", t);
        for (guint i = 0; i < n; i++) {
            const char *message = NULL;

            syncview_debug_log_window_get_line(d.log, i, NULL, NULL, &message);
            const char *at = strstr(message, prefix);

            if (at) {
                assert(atoi(at + strlen(prefix)) == next);  /* ordine di emissione del thread rispettato */
                next++;
            }
        }
        if (next != 800) {
            g_printerr("thread %d: trovate %d righe su 800 (modello=%u in coda=%u scartate=%u)\n", t, next,
                       syncview_debug_log_window_get_line_count(d.log), syncview_debug_log_window_get_pending_count(d.log),
                       syncview_debug_log_window_get_dropped_count(d.log));
        }
        assert(next == 800);
    }
    close_debug(&d);
}

static void
test_closing_main_window_closes_debug_windows(void)
{
    Debug d = open_debug();
    GWeakRef log_ref, modules_ref;

    g_weak_ref_init(&log_ref, d.log);
    g_weak_ref_init(&modules_ref, d.modules);

    /* Una chiusa prima, a mano: la chiusura della principale non deve toccare una finestra già sparita. */
    gtk_window_destroy(GTK_WINDOW(d.modules));
    spin_for(50);
    assert(g_weak_ref_get(&modules_ref) == NULL);

    gtk_window_destroy(GTK_WINDOW(d.main_window));
    spin_for(100);
    GtkWidget *alive = g_weak_ref_get(&log_ref);

    assert(alive == NULL);
    g_weak_ref_clear(&log_ref);
    g_weak_ref_clear(&modules_ref);
    logger_shutdown();
}

static void
test_logging_after_close_is_safe(void)
{
    Debug d = open_debug();
    GThread *threads[2];
    Burst bursts[2];

    close_debug(&d);  /* il logger è spento qui, lo riaccendiamo senza finestre */
    logger_init(NULL, TRUE, NULL);

    /* Finestra chiusa mentre altri thread continuano a loggare: nessun accesso a memoria liberata (ASan). */
    d = open_debug();
    for (int t = 0; t < 2; t++) {
        bursts[t] = (Burst){ t, 3000 };
        threads[t] = g_thread_new("log-burst", log_burst, &bursts[t]);
    }
    spin_for(20);
    gtk_window_destroy(GTK_WINDOW(d.main_window));
    for (int t = 0; t < 2; t++) {
        g_thread_join(threads[t]);
    }
    spin_for(1500);  /* oltre il ritardo con cui la finestra rilascia la coda del sink */
    log_ui("dopo-la-chiusura");
    spin_for(50);
    logger_shutdown();
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

    g_log_set_always_fatal(G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);
    g_unsetenv("SYNCVIEW_DEBUG");

    app = gtk_application_new("com.syncview.SyncView.DebugTest", G_APPLICATION_NON_UNIQUE);
    assert(g_application_register(G_APPLICATION(app), NULL, NULL));

    RUN_TEST(test_not_opened_without_debug());
    RUN_TEST(test_opens_two_windows_and_shows_lines());
    RUN_TEST(test_module_switches_control_the_logger());
    RUN_TEST(test_view_filters_by_level_and_module());
    RUN_TEST(test_pause_resume_and_clear());
    RUN_TEST(test_line_cap_keeps_the_newest());
    RUN_TEST(test_logging_from_other_threads());
    RUN_TEST(test_closing_main_window_closes_debug_windows());
    RUN_TEST(test_logging_after_close_is_safe());

    g_object_unref(app);
    return 0;
}
