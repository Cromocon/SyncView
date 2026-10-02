#include "ui/deps_dialog.h"

#include "core/deps_state.h"
#include "core/logger.h"
#include "ui/theme.h"

#include <gst/gst.h>
#include <string.h>

#define DIALOG_DATA "syncview-deps-dialog"
#define PULSE_INTERVAL_MS 120
#define OUTPUT_MAX_LINES 2000

/* ------------------------------------------------------------------ */
/* Opzioni (copia posseduta: il chiamante può liberare le sue subito)   */
/* ------------------------------------------------------------------ */

typedef struct {
    SyncviewDepsDialogOptions opts;
    DepRunOptions run;
    gboolean has_run;
    char *state_file;
    char *os_release_path;
    char *elevation_program;
    char *deps_dir;
} OptionsCopy;

static void
options_copy_init(OptionsCopy *copy, const SyncviewDepsDialogOptions *opts)
{
    memset(copy, 0, sizeof *copy);
    if (!opts) {
        return;
    }
    copy->opts = *opts;
    copy->state_file = g_strdup(opts->state_file);
    copy->os_release_path = g_strdup(opts->os_release_path);
    copy->opts.state_file = copy->state_file;
    copy->opts.os_release_path = copy->os_release_path;
    if (opts->run_options) {
        copy->run = *opts->run_options;
        copy->has_run = TRUE;
        copy->elevation_program = g_strdup(opts->run_options->elevation_program);
        copy->deps_dir = g_strdup(opts->run_options->deps_dir);
        copy->run.elevation_program = copy->elevation_program;
        copy->run.deps_dir = copy->deps_dir;
        copy->opts.run_options = &copy->run;
    }
}

static void
options_copy_clear(OptionsCopy *copy)
{
    g_free(copy->state_file);
    g_free(copy->os_release_path);
    g_free(copy->elevation_program);
    g_free(copy->deps_dir);
}

static DepsReport *
run_check(const SyncviewDepsDialogOptions *opts)
{
    return opts->check ? opts->check(opts->check_data) : deps_check_run(NULL, NULL);
}

/* ------------------------------------------------------------------ */
/* Stato del dialogo                                                    */
/* ------------------------------------------------------------------ */

typedef struct {
    gint refs;                 /* il dialogo, i thread di lavoro e i messaggi in volo */
    gboolean alive;            /* finestra non ancora distrutta (solo main thread) */

    GtkWidget *window;
    DepsReport *report;
    OptionsCopy options;

    SyncviewDepsDialogPhase phase;
    SyncviewDepsOutcome outcome;
    gboolean continued_without;
    gboolean outcome_done;

    gboolean include_optional;
    gboolean allow_third_party;
    char *third_party_title;   /* titolo del passo di terze parti disponibile, NULL se non c'è */
    DepPlan *plan;             /* piano mostrato in REVIEW (NULL = nulla da installare da soli) */
    GError *plan_error;

    GHashTable *covered;       /* id dei componenti che l'ultima installazione doveva risolvere */
    gboolean running;
    gboolean cancel_requested;
    GCancellable *cancellable;
    size_t step_index, step_count;
    char *step_title;
    gint64 dl_done, dl_total;
    gboolean verifying;
    guint pulse_id;
    gboolean busy_check;       /* ricontrollo in corso */

    /* Esito dell'ultima installazione. */
    gboolean last_run_ok;
    GError *failure;

    GtkTextBuffer *output;

    /* Widget della schermata corrente (si ricostruiscono a ogni render). */
    GtkWidget *content;
    GtkWidget *footer;
    GHashTable *widgets;       /* nome -> widget (non posseduti) */
    GHashTable *badges;        /* id -> GtkLabel */
    GHashTable *bars;          /* id -> GtkProgressBar */
    GtkWidget *status_label;
    GtkWidget *os_list;
} Dlg;

static Dlg *
dlg_ref(Dlg *dlg)
{
    g_atomic_int_inc(&dlg->refs);
    return dlg;
}

static void
dlg_unref(Dlg *dlg)
{
    if (!g_atomic_int_dec_and_test(&dlg->refs)) {
        return;
    }
    deps_report_free(dlg->report);
    options_copy_clear(&dlg->options);
    dep_plan_free(dlg->plan);
    g_clear_error(&dlg->plan_error);
    g_clear_error(&dlg->failure);
    g_free(dlg->third_party_title);
    g_free(dlg->step_title);
    g_clear_pointer(&dlg->covered, g_hash_table_destroy);
    g_clear_object(&dlg->cancellable);
    g_clear_object(&dlg->output);
    g_hash_table_destroy(dlg->widgets);
    g_hash_table_destroy(dlg->badges);
    g_hash_table_destroy(dlg->bars);
    g_free(dlg);
}

static Dlg *
dlg_of(GtkWidget *dialog)
{
    return dialog ? g_object_get_data(G_OBJECT(dialog), DIALOG_DATA) : NULL;
}

static void render(Dlg *dlg);

/* ------------------------------------------------------------------ */
/* Piano                                                                */
/* ------------------------------------------------------------------ */

/* Componente mancante che SyncView sa installare da solo (stesso criterio di dep_plan_new). */
static gboolean
item_installable(const Dlg *dlg, const DepsItem *item)
{
    return item->status != DEPS_STATUS_OK &&
           (item->resolution == DEPS_RESOLUTION_SYSTEM_PACKAGES || item->resolution == DEPS_RESOLUTION_PLATFORM_INSTALLER) &&
           (item->feature != DEPS_FEATURE_OPTIONAL || dlg->include_optional);
}

static gboolean
has_missing(const DepsReport *report, gboolean required_only)
{
    for (size_t i = 0; i < deps_report_count(report); i++) {
        const DepsItem *item = deps_report_get(report, i);

        if (item->status == DEPS_STATUS_MISSING || (!required_only && item->status == DEPS_STATUS_OPTIONAL_MISSING)) {
            return TRUE;
        }
    }
    return FALSE;
}

static void
rebuild_plan(Dlg *dlg)
{
    DepPlanOptions options = { dlg->include_optional, dlg->allow_third_party, dlg->options.os_release_path };

    dep_plan_free(dlg->plan);
    g_clear_error(&dlg->plan_error);
    dlg->plan = dep_plan_new(dlg->report, &options, &dlg->plan_error);

    /* C'è un passo di terze parti che l'utente potrebbe consentire? Si vede con un piano che lo ammette. */
    g_clear_pointer(&dlg->third_party_title, g_free);

    DepPlanOptions with_third = { dlg->include_optional, TRUE, dlg->options.os_release_path };
    DepPlan *probe = dep_plan_new(dlg->report, &with_third, NULL);

    if (probe) {
        for (size_t i = 0; i < dep_plan_step_count(probe); i++) {
            const DepStep *step = dep_plan_step(probe, i);

            if (step->third_party) {
                dlg->third_party_title = g_strdup(step->title);
                break;
            }
        }
        dep_plan_free(probe);
    }
}

/* ------------------------------------------------------------------ */
/* Costruzione dei widget                                               */
/* ------------------------------------------------------------------ */

static GtkWidget *
reg(Dlg *dlg, const char *name, GtkWidget *widget)
{
    g_hash_table_insert(dlg->widgets, g_strdup(name), widget);
    return widget;
}

static GtkWidget *
make_label(const char *text, const char *css_class, gboolean wrap)
{
    GtkWidget *label = gtk_label_new(text);

    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    if (css_class) {
        gtk_widget_add_css_class(label, css_class);
    }
    if (wrap) {
        gtk_label_set_wrap(GTK_LABEL(label), TRUE);
        gtk_label_set_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
        gtk_label_set_max_width_chars(GTK_LABEL(label), 1);  /* con hexpand: si adatta alla larghezza disponibile */
        gtk_widget_set_hexpand(label, TRUE);
    }
    return label;
}

static GtkWidget *
make_button(Dlg *dlg, const char *name, const char *label, gboolean primary, GCallback callback)
{
    GtkWidget *button = gtk_button_new_with_label(label);

    gtk_widget_add_css_class(button, "sv-btn");
    if (primary) {
        gtk_widget_add_css_class(button, "sv-primary");
    }
    g_signal_connect_swapped(button, "clicked", callback, dlg);
    reg(dlg, name, button);
    gtk_widget_set_sensitive(button, !dlg->busy_check);
    return button;
}

static void
clear_box(GtkWidget *box)
{
    for (GtkWidget *child = gtk_widget_get_first_child(box); child;) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);

        gtk_box_remove(GTK_BOX(box), child);
        child = next;
    }
}

static void
set_badge(GtkWidget *badge, const char *text, const char *css)
{
    gtk_label_set_text(GTK_LABEL(badge), text);
    gtk_widget_remove_css_class(badge, "sv-ok");
    gtk_widget_remove_css_class(badge, "sv-err");
    gtk_widget_remove_css_class(badge, "sv-warn");
    if (css) {
        gtk_widget_add_css_class(badge, css);
    }
}

static gboolean
is_covered(const Dlg *dlg, const DepsItem *item)
{
    return dlg->covered && g_hash_table_contains(dlg->covered, item->id);
}

static void
badge_for(const Dlg *dlg, const DepsItem *item, char **text, const char **css, gboolean *show_bar)
{
    *css = NULL;
    *show_bar = FALSE;

    if (item->status == DEPS_STATUS_OK) {
        *text = g_strdup("✓ Installato");
        *css = "sv-ok";
        return;
    }
    switch (dlg->phase) {
    case SYNCVIEW_DEPS_DIALOG_RUNNING:
        if (is_covered(dlg, item)) {
            *show_bar = TRUE;
            if (dlg->verifying) {
                *text = g_strdup("Verifica…");
            } else if (dlg->dl_total > 0) {
                *text = g_strdup_printf("↓ Download %d%%", (int)(dlg->dl_done * 100 / dlg->dl_total));
            } else {
                *text = g_strdup("In corso");
            }
            return;
        }
        break;
    case SYNCVIEW_DEPS_DIALOG_FAILED:
    case SYNCVIEW_DEPS_DIALOG_SUCCESS:
        /* Negata o annullata: non è stato toccato nulla, quindi nessun «fallita» sulle righe. */
        if (is_covered(dlg, item) &&
            !(dlg->failure && dlg->failure->domain == DEP_INSTALLER_ERROR &&
              (dlg->failure->code == DEP_INSTALLER_ERROR_DENIED || dlg->failure->code == DEP_INSTALLER_ERROR_CANCELLED))) {
            *text = g_strdup("✕ Installazione fallita");
            *css = "sv-err";
            return;
        }
        break;
    default:
        break;
    }

    if (item->resolution == DEPS_RESOLUTION_INSTRUCTIONS) {
        *text = g_strdup("Da installare a mano");
        *css = "sv-warn";
    } else if (item->status == DEPS_STATUS_OPTIONAL_MISSING && !dlg->include_optional) {
        *text = g_strdup("Opzionale, non installato");
    } else if (dlg->phase == SYNCVIEW_DEPS_DIALOG_RUNNING) {
        *text = g_strdup("In attesa");
    } else {
        *text = g_strdup(dlg->phase == SYNCVIEW_DEPS_DIALOG_REVIEW ? "Da installare" : "Non installato");
    }
}

/* Descrizione breve per riga, come nel design: il dettaglio tecnico del controllo resta nel suggerimento (tooltip). */
static const char *
short_description(const DepsItem *item)
{
    static const struct { const char *id; const char *text; } SHORT[] = {
        { "gst-playbin3", "Motore di riproduzione" },
        { "gst-gtk4sink", "Mostra il video nella finestra" },
        { "gst-demuxers", "Formati dei file (mp4, mkv, mov, avi…)" },
        { "gst-decoder-h264", "Codec video principale (H.264)" },
        { "gst-decoder-hevc", "Video H.265, es. da iPhone" },
        { "gst-decoder-vp9", "Video VP9 (WebM)" },
        { "gst-decoder-av1", "Video AV1" },
        { "gst-hw-decoders", "Decodifica con la scheda video (meno carico sulla CPU)" },
        { "ffmpeg", "Esportazione dei video" },
    };

    for (size_t i = 0; i < G_N_ELEMENTS(SHORT); i++) {
        if (strcmp(item->id, SHORT[i].id) == 0) {
            return SHORT[i].text;
        }
    }
    return item->detail;
}

static GtkWidget *
build_rows(Dlg *dlg)
{
    GtkWidget *list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    gtk_widget_add_css_class(list, "sv-deps-list");
    reg(dlg, "list", list);
    g_hash_table_remove_all(dlg->badges);
    g_hash_table_remove_all(dlg->bars);

    for (size_t i = 0; i < deps_report_count(dlg->report); i++) {
        const DepsItem *item = deps_report_get(dlg->report, i);
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        GtkWidget *texts = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        GtkWidget *bar = gtk_progress_bar_new();
        GtkWidget *badge = gtk_label_new(NULL);
        char *badge_text = NULL;
        const char *css = NULL;
        gboolean show_bar = FALSE;

        gtk_widget_add_css_class(row, "sv-dep-row");
        gtk_widget_set_hexpand(texts, TRUE);
        gtk_box_append(GTK_BOX(texts), make_label(item->title, "sv-dep-name", TRUE));
        gtk_box_append(GTK_BOX(texts), make_label(short_description(item), "sv-dep-sub", TRUE));
        gtk_widget_set_tooltip_text(row, item->detail);

        gtk_widget_set_valign(bar, GTK_ALIGN_CENTER);
        gtk_widget_add_css_class(badge, "sv-badge");
        gtk_widget_set_valign(badge, GTK_ALIGN_CENTER);
        badge_for(dlg, item, &badge_text, &css, &show_bar);
        set_badge(badge, badge_text, css);
        g_free(badge_text);
        gtk_widget_set_visible(bar, show_bar);

        gtk_box_append(GTK_BOX(row), texts);
        gtk_box_append(GTK_BOX(row), bar);
        gtk_box_append(GTK_BOX(row), badge);
        gtk_box_append(GTK_BOX(list), row);

        g_hash_table_insert(dlg->badges, g_strdup(item->id), badge);
        g_hash_table_insert(dlg->bars, g_strdup(item->id), bar);
        char *name = g_strdup_printf("badge:%s", item->id);

        reg(dlg, name, badge);
        g_free(name);
    }
    return list;
}

static GtkWidget *
make_banner(Dlg *dlg, const char *text, const char *css)
{
    GtkWidget *banner = make_label(text, "sv-banner", TRUE);

    if (css) {
        gtk_widget_add_css_class(banner, css);
    }
    return reg(dlg, "banner", banner);
}

static GtkWidget *
make_note(Dlg *dlg, const char *name, const char *text)
{
    GtkWidget *note = make_label(text, "sv-note", TRUE);

    return reg(dlg, name, note);
}

/* Cosa farà il sistema operativo per l'autorizzazione (la password non passa mai da SyncView). */
static char *
elevation_text(const Dlg *dlg)
{
    const char *who;

    switch (deps_report_get_platform(dlg->report)) {
    case DEPS_PLATFORM_WINDOWS:
        who = "Windows chiederà il permesso di amministratore (UAC) al momento dell'installazione.";
        break;
    case DEPS_PLATFORM_MACOS:
        who = "macOS chiederà l'autorizzazione di amministratore (password o Touch ID) nell'Installer.";
        break;
    default:
        who = "Il sistema operativo chiederà la password di amministratore al momento dell'installazione (finestra di sistema).";
        break;
    }

    const char *consequence = !deps_report_can_play(dlg->report)
                                  ? " Senza questi componenti SyncView non può riprodurre nessun video."
                              : !deps_report_can_export(dlg->report)
                                  ? " Senza questi componenti SyncView non può esportare i video."
                                  : "";

    return g_strdup_printf("%s SyncView non la vede e non la salva.%s", who, consequence);
}

/* ------------------------------------------------------------------ */
/* Installazione manuale: comando per ogni sistema                      */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *id;
    const char *name;
    const char *manager;      /* NULL = testo fisso */
    const char *static_text;
} OsRow;

static const OsRow OS_ROWS[] = {
    { "apt-get", "Debian / Ubuntu", "apt-get", NULL },
    { "pacman", "Arch", "pacman", NULL },
    { "dnf", "Fedora", "dnf", NULL },
    { "zypper", "openSUSE", "zypper", NULL },
    { "windows", "Windows", NULL,
      "Installa il runtime GStreamer completo dal sito ufficiale (gstreamer.freedesktop.org), poi riapri SyncView." },
    { "macos", "macOS", NULL,
      "Installa il pacchetto runtime di GStreamer dal sito ufficiale (gstreamer.freedesktop.org), poi riapri SyncView." },
};

/* Riga da preselezionare: il sistema in uso. */
static const char *
current_os_id(const Dlg *dlg)
{
    switch (deps_report_get_platform(dlg->report)) {
    case DEPS_PLATFORM_WINDOWS:
        return "windows";
    case DEPS_PLATFORM_MACOS:
        return "macos";
    default:
        break;
    }
    for (size_t i = 0; i < deps_report_count(dlg->report); i++) {
        const DepsItem *item = deps_report_get(dlg->report, i);

        if (item->package_manager) {
            return item->package_manager;
        }
    }
    return OS_ROWS[0].id;
}

static GtkWidget *
build_os_table(Dlg *dlg)
{
    GtkWidget *list = gtk_list_box_new();
    const char *current = current_os_id(dlg);

    gtk_widget_add_css_class(list, "sv-os-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_SINGLE);
    gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(list), TRUE);
    dlg->os_list = list;

    for (size_t i = 0; i < G_N_ELEMENTS(OS_ROWS); i++) {
        const OsRow *os = &OS_ROWS[i];
        char *note = NULL;
        char *command = os->manager ? deps_report_manual_command(dlg->report, os->manager, TRUE, &note) : NULL;
        const char *shown = os->static_text ? os->static_text : command ? command : "Nessun pacchetto da installare";
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        GtkWidget *texts = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        GtkWidget *name = make_label(os->name, "sv-os-name", FALSE);
        GtkWidget *cmd = make_label(shown, "sv-os-cmd", TRUE);

        gtk_widget_set_size_request(name, 130, -1);
        gtk_widget_set_valign(name, GTK_ALIGN_START);
        gtk_label_set_selectable(GTK_LABEL(cmd), TRUE);
        gtk_box_append(GTK_BOX(texts), cmd);
        if (note) {
            gtk_box_append(GTK_BOX(texts), make_label(note, "sv-dep-sub", TRUE));
        }
        gtk_box_append(GTK_BOX(box), name);
        gtk_box_append(GTK_BOX(box), texts);
        gtk_widget_set_hexpand(texts, TRUE);

        GtkWidget *row = gtk_list_box_row_new();

        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
        g_object_set_data_full(G_OBJECT(row), "copy-text", g_strdup(shown), g_free);
        gtk_list_box_append(GTK_LIST_BOX(list), row);
        if (strcmp(os->id, current) == 0) {
            gtk_list_box_select_row(GTK_LIST_BOX(list), GTK_LIST_BOX_ROW(row));
        }

        char *reg_name = g_strdup_printf("os:%s", os->id);

        reg(dlg, reg_name, row);
        g_free(reg_name);
        g_free(command);
        g_free(note);
    }
    return list;
}

/* Istruzioni di questo sistema per i componenti mancanti (anche quelli che l'app non sa installare). */
static char *
instructions_text(const Dlg *dlg, gboolean only_manual)
{
    GString *text = g_string_new(NULL);

    for (size_t i = 0; i < deps_report_count(dlg->report); i++) {
        const DepsItem *item = deps_report_get(dlg->report, i);

        if (item->status == DEPS_STATUS_OK || !*item->instructions ||
            (only_manual && item->resolution != DEPS_RESOLUTION_INSTRUCTIONS)) {
            continue;
        }
        g_string_append_printf(text, "%s• %s: %s", text->len ? "\n" : "", item->title, item->instructions);
    }
    return g_string_free(text, FALSE);
}

/* ------------------------------------------------------------------ */
/* Azioni                                                               */
/* ------------------------------------------------------------------ */

static void
set_phase(Dlg *dlg, SyncviewDepsDialogPhase phase)
{
    dlg->phase = phase;
    render(dlg);
}

static void
on_copy(Dlg *dlg)
{
    GtkListBoxRow *row = dlg->os_list ? gtk_list_box_get_selected_row(GTK_LIST_BOX(dlg->os_list)) : NULL;
    const char *text = row ? g_object_get_data(G_OBJECT(row), "copy-text") : NULL;

    if (text) {
        gdk_clipboard_set_text(gtk_widget_get_clipboard(dlg->window), text);
        log_user_action("Copia comando di installazione", NULL);
        if (dlg->status_label) {
            gtk_label_set_text(GTK_LABEL(dlg->status_label), "Comando copiato negli appunti");
        }
    }
}

static void
on_manual(Dlg *dlg)
{
    log_user_action("Installa a mano", NULL);
    set_phase(dlg, SYNCVIEW_DEPS_DIALOG_MANUAL);
}

static void
on_back(Dlg *dlg)
{
    set_phase(dlg, SYNCVIEW_DEPS_DIALOG_REVIEW);
}

static void
on_close_clicked(Dlg *dlg)
{
    gtk_window_close(GTK_WINDOW(dlg->window));
}

static void
on_cancel_clicked(Dlg *dlg)
{
    log_user_action("Dipendenze: annulla", NULL);
    gtk_window_close(GTK_WINDOW(dlg->window));
}

static void
on_continue_without(Dlg *dlg)
{
    log_user_action("Dipendenze: continua senza", NULL);
    dlg->continued_without = TRUE;
    gtk_window_close(GTK_WINDOW(dlg->window));
}

static void
on_optional_toggled(GtkCheckButton *check, Dlg *dlg)
{
    dlg->include_optional = gtk_check_button_get_active(check);
    rebuild_plan(dlg);
    render(dlg);
}

static void
on_third_party_toggled(GtkCheckButton *check, Dlg *dlg)
{
    dlg->allow_third_party = gtk_check_button_get_active(check);
    rebuild_plan(dlg);
    render(dlg);
}

/* ---- Ricontrollo asincrono ---- */

static void
check_thread(GTask *task, gpointer source, gpointer task_data, GCancellable *cancellable)
{
    (void)source;
    (void)cancellable;

    Dlg *dlg = task_data;

    g_task_return_pointer(task, run_check(&dlg->options.opts), (GDestroyNotify)deps_report_free);
}

static void
on_recheck_done(GObject *source, GAsyncResult *result, gpointer user_data)
{
    Dlg *dlg = user_data;
    DepsReport *report = g_task_propagate_pointer(G_TASK(result), NULL);

    (void)source;
    dlg->busy_check = FALSE;
    if (dlg->alive && report) {
        deps_report_free(dlg->report);
        dlg->report = report;
        g_clear_pointer(&dlg->covered, g_hash_table_destroy);
        rebuild_plan(dlg);
        dlg->phase = deps_report_is_complete(report) ? SYNCVIEW_DEPS_DIALOG_ALL_OK : SYNCVIEW_DEPS_DIALOG_REVIEW;
        log_user_action("Dipendenze: ricontrollo", deps_report_is_complete(report) ? "tutto a posto" : "manca qualcosa");
        render(dlg);
    } else {
        deps_report_free(report);
    }
    dlg_unref(dlg);
}

static void
on_recheck(Dlg *dlg)
{
    if (dlg->busy_check || dlg->running) {
        return;
    }
    dlg->busy_check = TRUE;
    render(dlg);  /* i pulsanti si disattivano e compare «Controllo in corso…» */

    GTask *task = g_task_new(NULL, NULL, on_recheck_done, dlg_ref(dlg));

    g_task_set_task_data(task, dlg_ref(dlg), (GDestroyNotify)dlg_unref);
    g_task_run_in_thread(task, check_thread);
    g_object_unref(task);
}

/* ---- Installazione in un thread di lavoro ---- */

typedef struct {
    Dlg *dlg;
    DepPlan *plan;
    DepRunOptions run;
    GCancellable *cancellable;
    gboolean consent_third_party;
} Job;

typedef struct {
    Dlg *dlg;
    DepProgressKind kind;
    size_t step_index, step_count;
    char *title;
    char *line;
    gint64 done, total;
} ProgressMsg;

typedef struct {
    Dlg *dlg;
    gboolean ok;
    GError *error;
    DepsReport *report;
} DoneMsg;

static void
append_output(Dlg *dlg, const char *line)
{
    GtkTextIter end;

    gtk_text_buffer_get_end_iter(dlg->output, &end);
    gtk_text_buffer_insert(dlg->output, &end, line, -1);
    gtk_text_buffer_insert(dlg->output, &end, "\n", 1);

    if (gtk_text_buffer_get_line_count(dlg->output) > OUTPUT_MAX_LINES) {
        GtkTextIter start, cut;

        gtk_text_buffer_get_start_iter(dlg->output, &start);
        gtk_text_buffer_get_iter_at_line(dlg->output, &cut, OUTPUT_MAX_LINES / 4);
        gtk_text_buffer_delete(dlg->output, &start, &cut);
    }
}

/* Aggiorna i widget della fase RUNNING senza ricostruire la schermata. */
static void
update_running(Dlg *dlg)
{
    for (size_t i = 0; i < deps_report_count(dlg->report); i++) {
        const DepsItem *item = deps_report_get(dlg->report, i);
        GtkWidget *badge = g_hash_table_lookup(dlg->badges, item->id);
        GtkWidget *bar = g_hash_table_lookup(dlg->bars, item->id);
        char *text = NULL;
        const char *css = NULL;
        gboolean show_bar = FALSE;

        if (!badge) {
            continue;
        }
        badge_for(dlg, item, &text, &css, &show_bar);
        set_badge(badge, text, css);
        g_free(text);
        if (bar) {
            gtk_widget_set_visible(bar, show_bar);
            if (show_bar && dlg->dl_total > 0) {
                gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(bar), (double)dlg->dl_done / (double)dlg->dl_total);
            }
        }
    }

    if (dlg->status_label) {
        char *text = g_strdup_printf("%sPasso %zu di %zu%s%s", dlg->cancel_requested ? "Annullamento richiesto · " : "",
                                     dlg->step_index + 1, dlg->step_count, dlg->step_title ? " · " : "",
                                     dlg->step_title ? dlg->step_title : "");

        gtk_label_set_text(GTK_LABEL(dlg->status_label), text);
        g_free(text);
    }
}

static gboolean
on_progress_idle(gpointer data)
{
    ProgressMsg *msg = data;
    Dlg *dlg = msg->dlg;

    if (dlg->alive) {
        if (msg->kind == DEP_PROGRESS_OUTPUT && msg->line) {
            append_output(dlg, msg->line);
        } else {
            dlg->step_index = msg->step_index;
            dlg->step_count = msg->step_count;
            g_free(dlg->step_title);
            dlg->step_title = g_strdup(msg->title);
            if (msg->kind == DEP_PROGRESS_STEP_STARTED) {
                dlg->dl_done = dlg->dl_total = 0;
                dlg->verifying = FALSE;
                if (msg->title) {
                    char *line = g_strdup_printf("— %s", msg->title);

                    append_output(dlg, line);
                    g_free(line);
                }
            } else if (msg->kind == DEP_PROGRESS_DOWNLOAD) {
                dlg->dl_done = msg->done;
                dlg->dl_total = msg->total;
            } else if (msg->kind == DEP_PROGRESS_VERIFYING) {
                dlg->verifying = TRUE;
            }
        }
        if (dlg->phase == SYNCVIEW_DEPS_DIALOG_RUNNING) {
            update_running(dlg);
        }
    }
    g_free(msg->title);
    g_free(msg->line);
    dlg_unref(dlg);
    g_free(msg);
    return G_SOURCE_REMOVE;
}

static void
job_progress(const DepProgress *progress, gpointer user_data)
{
    Job *job = user_data;
    ProgressMsg *msg = g_new0(ProgressMsg, 1);

    msg->dlg = dlg_ref(job->dlg);
    msg->kind = progress->kind;
    msg->step_index = progress->step_index;
    msg->step_count = progress->step_count;
    msg->title = g_strdup(progress->title);
    msg->line = g_strdup(progress->line);
    msg->done = progress->bytes_done;
    msg->total = progress->bytes_total;
    g_idle_add(on_progress_idle, msg);
}

static gboolean
on_job_done_idle(gpointer data)
{
    DoneMsg *msg = data;
    Dlg *dlg = msg->dlg;

    if (dlg->alive) {
        dlg->running = FALSE;
        dlg->verifying = FALSE;
        g_clear_pointer(&dlg->failure, g_error_free);
        dlg->failure = msg->error;
        msg->error = NULL;
        dlg->last_run_ok = msg->ok;
        if (msg->report) {
            deps_report_free(dlg->report);
            dlg->report = msg->report;
            msg->report = NULL;
        }
        rebuild_plan(dlg);

        /* Riuscito davvero solo se tutto ciò che il piano doveva risolvere ora c'è (altrimenti: fallito, anche con exit 0). */
        gboolean resolved = msg->ok;

        for (size_t i = 0; resolved && i < deps_report_count(dlg->report); i++) {
            const DepsItem *item = deps_report_get(dlg->report, i);

            resolved = item->status == DEPS_STATUS_OK || !is_covered(dlg, item);
        }
        log_user_action("Dipendenze: installazione terminata", msg->ok ? (resolved ? "riuscita" : "da riavviare/incompleta") : "fallita");
        dlg->phase = resolved ? SYNCVIEW_DEPS_DIALOG_SUCCESS : SYNCVIEW_DEPS_DIALOG_FAILED;
        if (dlg->pulse_id) {
            g_source_remove(dlg->pulse_id);
            dlg->pulse_id = 0;
        }
        render(dlg);
    }
    if (msg->error) {
        g_error_free(msg->error);
    }
    deps_report_free(msg->report);
    dlg_unref(dlg);
    g_free(msg);
    return G_SOURCE_REMOVE;
}

static gpointer
job_thread(gpointer data)
{
    Job *job = data;
    DoneMsg *done = g_new0(DoneMsg, 1);

    job->run.progress = job_progress;
    job->run.user_data = job;
    job->run.cancellable = job->cancellable;
    job->run.consent_third_party = job->consent_third_party;
    done->dlg = dlg_ref(job->dlg);
    done->ok = dep_installer_run(job->plan, &job->run, &done->error);

    if (done->ok && gst_is_initialized()) {
        gst_update_registry();  /* il processo corrente vede subito i plugin appena installati */
    }
    /* Ricontrollo sempre: anche dopo un fallimento parziale alcuni componenti possono essere stati installati. */
    done->report = run_check(&job->dlg->options.opts);

    g_idle_add(on_job_done_idle, done);
    g_object_unref(job->cancellable);
    dep_plan_free(job->plan);
    dlg_unref(job->dlg);
    g_free(job);
    return NULL;
}

static gboolean
on_pulse(gpointer data)
{
    Dlg *dlg = data;
    GHashTableIter it;
    gpointer id, bar;

    g_hash_table_iter_init(&it, dlg->bars);
    while (g_hash_table_iter_next(&it, &id, &bar)) {
        if (gtk_widget_get_visible(GTK_WIDGET(bar)) && dlg->dl_total <= 0) {
            gtk_progress_bar_pulse(GTK_PROGRESS_BAR(bar));
        }
    }
    return G_SOURCE_CONTINUE;
}

static void
on_install(Dlg *dlg)
{
    if (!dlg->plan || dlg->running) {
        return;
    }

    /* Componenti che questa installazione deve risolvere: per riconoscere un fallimento anche quando il processo esce con 0. */
    g_clear_pointer(&dlg->covered, g_hash_table_destroy);
    dlg->covered = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    for (size_t i = 0; i < deps_report_count(dlg->report); i++) {
        const DepsItem *item = deps_report_get(dlg->report, i);

        /* Il passo di terze parti (Fedora, RPM Fusion) installa il decoder H.264. */
        if (item_installable(dlg, item) ||
            (dlg->allow_third_party && dep_plan_has_third_party(dlg->plan) && item->status != DEPS_STATUS_OK &&
             strcmp(item->id, "gst-decoder-h264") == 0)) {
            g_hash_table_add(dlg->covered, g_strdup(item->id));
        }
    }

    Job *job = g_new0(Job, 1);

    job->dlg = dlg_ref(dlg);
    job->plan = dlg->plan;
    dlg->plan = NULL;                    /* il thread ne è il proprietario; si ricostruisce a fine lavoro */
    if (dlg->options.has_run) {
        job->run = dlg->options.run;
    }
    g_clear_object(&dlg->cancellable);
    dlg->cancellable = g_cancellable_new();
    job->cancellable = g_object_ref(dlg->cancellable);
    job->consent_third_party = dlg->allow_third_party;

    dlg->running = TRUE;
    dlg->cancel_requested = FALSE;
    dlg->step_index = 0;
    dlg->step_count = dep_plan_step_count(job->plan);
    dlg->dl_done = dlg->dl_total = 0;
    g_clear_pointer(&dlg->step_title, g_free);
    gtk_text_buffer_set_text(dlg->output, "", -1);
    g_clear_error(&dlg->failure);

    {
        char *summary = dep_plan_describe(job->plan);

        log_user_action("Dipendenze: installazione avviata (consenso dato)", summary);
        g_free(summary);
    }
    dlg->phase = SYNCVIEW_DEPS_DIALOG_RUNNING;
    render(dlg);
    if (!dlg->pulse_id) {
        dlg->pulse_id = g_timeout_add(PULSE_INTERVAL_MS, on_pulse, dlg);
    }

    GThread *thread = g_thread_new("syncview-deps-install", job_thread, job);

    g_thread_unref(thread);
}

static void
on_cancel_install(Dlg *dlg)
{
    if (dlg->running && !dlg->cancel_requested) {
        dlg->cancel_requested = TRUE;
        g_cancellable_cancel(dlg->cancellable);
        log_user_action("Dipendenze: annullamento richiesto", NULL);
        update_running(dlg);
        GtkWidget *button = g_hash_table_lookup(dlg->widgets, "cancel-install");

        if (button) {
            gtk_widget_set_sensitive(button, FALSE);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Schermate                                                            */
/* ------------------------------------------------------------------ */

static GtkWidget *
make_check(Dlg *dlg, const char *name, const char *label, gboolean active, GCallback callback)
{
    GtkWidget *check = gtk_check_button_new_with_label(label);

    gtk_widget_add_css_class(check, "sv-check");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(check), active);
    g_signal_connect(check, "toggled", callback, dlg);
    gtk_widget_set_sensitive(check, !dlg->busy_check);
    return reg(dlg, name, check);
}

static void
append_footer_spacer(GtkWidget *footer)
{
    GtkWidget *spacer = gtk_label_new(NULL);

    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(footer), spacer);
}

static void
build_review(Dlg *dlg)
{
    gboolean can_play = deps_report_can_play(dlg->report);
    const char *heading = !can_play ? "Manca un componente per leggere i video"
                          : !deps_report_can_export(dlg->report) ? "Manca un componente per esportare i video"
                                                                 : "Alcuni componenti opzionali non sono installati";
    size_t ok = 0, total = deps_report_count(dlg->report);

    for (size_t i = 0; i < total; i++) {
        ok += deps_report_get(dlg->report, i)->status == DEPS_STATUS_OK;
    }

    char *lead = g_strdup_printf("SyncView usa GStreamer per decodificare i video. %zu componenti su %zu sono già presenti%s.", ok,
                                 total, dlg->plan ? "; gli altri si possono installare ora" : "");

    gtk_box_append(GTK_BOX(dlg->content), make_label(heading, "sv-dlg-h", TRUE));
    gtk_box_append(GTK_BOX(dlg->content), make_label(lead, "sv-dlg-lead", TRUE));
    g_free(lead);
    gtk_box_append(GTK_BOX(dlg->content), build_rows(dlg));

    gboolean any_optional = FALSE;

    for (size_t i = 0; i < total; i++) {
        const DepsItem *item = deps_report_get(dlg->report, i);

        any_optional |= item->status == DEPS_STATUS_OPTIONAL_MISSING && item->resolution != DEPS_RESOLUTION_INSTRUCTIONS;
    }
    if (any_optional) {
        gtk_box_append(GTK_BOX(dlg->content),
                       make_check(dlg, "opt-check", "Installa anche i componenti opzionali (altri codec, decodifica hardware)",
                                  dlg->include_optional, G_CALLBACK(on_optional_toggled)));
    }
    if (dlg->third_party_title) {
        char *label = g_strdup_printf("Consento il passo di terze parti «%s». SyncView non ne garantisce il corretto "
                                      "funzionamento.", dlg->third_party_title);

        gtk_box_append(GTK_BOX(dlg->content),
                       make_check(dlg, "third-check", label, dlg->allow_third_party, G_CALLBACK(on_third_party_toggled)));
        g_free(label);
    }

    if (dlg->plan) {
        char *describe = dep_plan_describe(dlg->plan);
        GtkWidget *plan = make_label(describe, "sv-plan", TRUE);

        gtk_label_set_selectable(GTK_LABEL(plan), TRUE);
        gtk_box_append(GTK_BOX(dlg->content), make_label("Cosa verrà installato", "sv-section-title", FALSE));
        gtk_box_append(GTK_BOX(dlg->content), reg(dlg, "plan", plan));
        g_free(describe);

        if (dep_plan_needs_elevation(dlg->plan)) {
            char *text = elevation_text(dlg);

            gtk_box_append(GTK_BOX(dlg->content), make_note(dlg, "note", text));
            g_free(text);
        }
    }

    char *manual = instructions_text(dlg, TRUE);

    if (*manual) {
        char *text = g_strdup_printf("Da installare a mano su questo sistema:\n%s", manual);

        gtk_box_append(GTK_BOX(dlg->content), make_note(dlg, "manual-note", text));
        g_free(text);
    }
    g_free(manual);

    /* Piè di pagina: Installa a mano | Annulla, Continua senza, Installa */
    gtk_box_append(GTK_BOX(dlg->footer), make_button(dlg, "manual", "Installa a mano", FALSE, G_CALLBACK(on_manual)));
    append_footer_spacer(dlg->footer);
    gtk_box_append(GTK_BOX(dlg->footer), make_button(dlg, "cancel", "Annulla", FALSE, G_CALLBACK(on_cancel_clicked)));
    gtk_box_append(GTK_BOX(dlg->footer), make_button(dlg, "continue", "Continua senza", FALSE, G_CALLBACK(on_continue_without)));
    GtkWidget *install = make_button(dlg, "install", "Installa", TRUE, G_CALLBACK(on_install));

    gtk_widget_set_sensitive(install, dlg->plan != NULL && !dlg->busy_check);
    gtk_box_append(GTK_BOX(dlg->footer), install);
}

static void
build_running(Dlg *dlg)
{
    gtk_box_append(GTK_BOX(dlg->content), make_label("Installazione in corso", "sv-dlg-h", TRUE));
    gtk_box_append(GTK_BOX(dlg->content),
                   make_label("Non chiudere SyncView: i componenti vengono installati e poi ricontrollati.", "sv-dlg-lead", TRUE));
    gtk_box_append(GTK_BOX(dlg->content), build_rows(dlg));

    char *text = elevation_text(dlg);

    gtk_box_append(GTK_BOX(dlg->content), make_note(dlg, "note", text));
    g_free(text);

    GtkWidget *view = gtk_text_view_new_with_buffer(dlg->output);
    GtkWidget *scroller = gtk_scrolled_window_new();
    GtkWidget *expander = gtk_expander_new("Output dell'installazione");

    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
    gtk_widget_add_css_class(view, "sv-output");
    gtk_widget_add_css_class(scroller, "sv-output-box");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), view);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroller), 140);
    gtk_expander_set_child(GTK_EXPANDER(expander), scroller);
    gtk_expander_set_expanded(GTK_EXPANDER(expander), TRUE);
    reg(dlg, "output", view);
    gtk_box_append(GTK_BOX(dlg->content), expander);

    dlg->status_label = reg(dlg, "status", make_label("", "sv-dep-sub", FALSE));
    gtk_box_append(GTK_BOX(dlg->footer), dlg->status_label);
    append_footer_spacer(dlg->footer);
    GtkWidget *cancel = make_button(dlg, "cancel-install", "Annulla installazione", FALSE, G_CALLBACK(on_cancel_install));

    gtk_widget_set_sensitive(cancel, !dlg->cancel_requested);
    gtk_box_append(GTK_BOX(dlg->footer), cancel);
    update_running(dlg);
}

static void
build_failed_or_manual(Dlg *dlg)
{
    gboolean manual = dlg->phase == SYNCVIEW_DEPS_DIALOG_MANUAL;

    if (!manual) {
        GError *e = dlg->failure;
        char *text = NULL;
        const char *css = "sv-err";

        if (e && e->domain == DEP_INSTALLER_ERROR && e->code == DEP_INSTALLER_ERROR_DENIED) {
            text = g_strdup("Autorizzazione negata o finestra chiusa · non è stato modificato nulla");
            css = "sv-warn";
        } else if (e && e->domain == DEP_INSTALLER_ERROR && e->code == DEP_INSTALLER_ERROR_CANCELLED) {
            text = g_strdup("Installazione annullata");
            css = "sv-warn";
        } else if (e && e->domain == DEP_INSTALLER_ERROR && e->code == DEP_INSTALLER_ERROR_NO_ELEVATION) {
            text = g_strdup("✕ Non è possibile chiedere i permessi di amministratore da SyncView · installa a mano");
        } else if (e) {
            text = g_strdup_printf("✕ Installazione fallita · %s", e->message);
        } else {
            GString *missing = g_string_new(NULL);

            for (size_t i = 0; i < deps_report_count(dlg->report); i++) {
                const DepsItem *item = deps_report_get(dlg->report, i);

                if (item->status != DEPS_STATUS_OK && is_covered(dlg, item)) {
                    g_string_append_printf(missing, "%s%s", missing->len ? ", " : "", item->title);
                }
            }
            text = g_strdup_printf("✕ L'installazione è terminata ma mancano ancora: %s · può servire riavviare SyncView",
                                   missing->str);
            g_string_free(missing, TRUE);
        }
        gtk_box_append(GTK_BOX(dlg->content), make_banner(dlg, text, css));
        g_free(text);
    } else {
        gtk_box_append(GTK_BOX(dlg->content), make_label("Installazione manuale", "sv-dlg-h", TRUE));
    }

    gtk_box_append(GTK_BOX(dlg->content), build_rows(dlg));
    gtk_box_append(GTK_BOX(dlg->content),
                   make_label("Installazione manuale · scegli il tuo sistema", "sv-section-title", FALSE));
    gtk_box_append(GTK_BOX(dlg->content), build_os_table(dlg));

    char *instructions = instructions_text(dlg, FALSE);

    if (*instructions) {
        char *text = g_strdup_printf("Su questo sistema:\n%s", instructions);

        gtk_box_append(GTK_BOX(dlg->content), make_note(dlg, "manual-note", text));
        g_free(text);
    }
    g_free(instructions);
    gtk_box_append(GTK_BOX(dlg->content),
                   make_label(!deps_report_can_play(dlg->report)
                                  ? "Senza questi componenti SyncView non può riprodurre nessun video. Dopo l'installazione manuale, ricontrolla da qui."
                                  : "Dopo l'installazione manuale, ricontrolla da qui.",
                              "sv-dlg-lead", TRUE));

    dlg->status_label = reg(dlg, "status", make_label(dlg->busy_check ? "Controllo in corso…" : "", "sv-dep-sub", FALSE));
    gtk_box_append(GTK_BOX(dlg->footer), make_button(dlg, "copy", "Copia comando", FALSE, G_CALLBACK(on_copy)));
    gtk_box_append(GTK_BOX(dlg->footer), dlg->status_label);
    append_footer_spacer(dlg->footer);
    if (manual) {
        if (dlg->plan) {
            gtk_box_append(GTK_BOX(dlg->footer), make_button(dlg, "back", "Indietro", FALSE, G_CALLBACK(on_back)));
        }
        gtk_box_append(GTK_BOX(dlg->footer), make_button(dlg, "close", "Chiudi", FALSE, G_CALLBACK(on_close_clicked)));
        gtk_box_append(GTK_BOX(dlg->footer), make_button(dlg, "recheck", "Ricontrolla", TRUE, G_CALLBACK(on_recheck)));
    } else {
        gtk_box_append(GTK_BOX(dlg->footer), make_button(dlg, "continue", "Continua senza", FALSE, G_CALLBACK(on_continue_without)));
        GtkWidget *retry = make_button(dlg, "retry", "Riprova installazione", TRUE, G_CALLBACK(on_back));

        gtk_widget_set_sensitive(retry, dlg->plan != NULL && !dlg->busy_check);
        gtk_box_append(GTK_BOX(dlg->footer), retry);
    }
}

static void
build_done(Dlg *dlg)
{
    gboolean success = dlg->phase == SYNCVIEW_DEPS_DIALOG_SUCCESS;
    char *text = success ? g_strdup_printf("✓ Tutto pronto: i componenti sono stati installati e ricontrollati%s",
                                           dlg->options.opts.restart_hint ? " · riavvia SyncView per usarli" : "")
                         : g_strdup("✓ Tutte le dipendenze sono a posto");

    gtk_box_append(GTK_BOX(dlg->content), make_banner(dlg, text, "sv-ok"));
    g_free(text);
    gtk_box_append(GTK_BOX(dlg->content), build_rows(dlg));

    /* Rimangono eventuali componenti opzionali o non installabili da SyncView: lo si dice. */
    char *manual = instructions_text(dlg, TRUE);

    if (*manual) {
        char *note = g_strdup_printf("Da installare a mano su questo sistema:\n%s", manual);

        gtk_box_append(GTK_BOX(dlg->content), make_note(dlg, "manual-note", note));
        g_free(note);
    }
    g_free(manual);

    dlg->status_label = reg(dlg, "status", make_label(dlg->busy_check ? "Controllo in corso…" : "", "sv-dep-sub", FALSE));
    gtk_box_append(GTK_BOX(dlg->footer), dlg->status_label);
    append_footer_spacer(dlg->footer);
    gtk_box_append(GTK_BOX(dlg->footer), make_button(dlg, "recheck", "Ricontrolla", FALSE, G_CALLBACK(on_recheck)));
    gtk_box_append(GTK_BOX(dlg->footer), make_button(dlg, "close", "Chiudi", TRUE, G_CALLBACK(on_close_clicked)));
}

static void
render(Dlg *dlg)
{
    clear_box(dlg->content);
    clear_box(dlg->footer);
    g_hash_table_remove_all(dlg->widgets);
    dlg->status_label = NULL;
    dlg->os_list = NULL;

    switch (dlg->phase) {
    case SYNCVIEW_DEPS_DIALOG_REVIEW:
        build_review(dlg);
        break;
    case SYNCVIEW_DEPS_DIALOG_RUNNING:
        build_running(dlg);
        break;
    case SYNCVIEW_DEPS_DIALOG_FAILED:
    case SYNCVIEW_DEPS_DIALOG_MANUAL:
        build_failed_or_manual(dlg);
        break;
    case SYNCVIEW_DEPS_DIALOG_SUCCESS:
    case SYNCVIEW_DEPS_DIALOG_ALL_OK:
        build_done(dlg);
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Finestra                                                             */
/* ------------------------------------------------------------------ */

/* Salva lo stato e comunica l'esito, una sola volta, alla chiusura. */
static void
finish_outcome(Dlg *dlg)
{
    if (dlg->outcome_done) {
        return;
    }
    dlg->outcome_done = TRUE;

    if (dlg->continued_without) {
        dlg->outcome = SYNCVIEW_DEPS_OUTCOME_CONTINUED_WITHOUT;
    } else if (dlg->phase == SYNCVIEW_DEPS_DIALOG_SUCCESS) {
        dlg->outcome = SYNCVIEW_DEPS_OUTCOME_INSTALLED;
    } else {
        dlg->outcome = SYNCVIEW_DEPS_OUTCOME_CLOSED;
    }

    DepsState *state = deps_state_load(dlg->options.state_file);
    GError *error = NULL;

    deps_state_mark_checked(state);
    if (dlg->continued_without) {
        deps_state_set_declined(state, dlg->report);
    } else if (!has_missing(dlg->report, FALSE)) {
        deps_state_clear_declined(state);  /* tutto a posto: la scelta di prima non vale più */
    }
    if (!deps_state_save(state, &error)) {
        log_error("Impossibile salvare lo stato delle dipendenze", error);
        g_clear_error(&error);
    }
    deps_state_free(state);

    if (dlg->options.opts.done) {
        dlg->options.opts.done(dlg->window, dlg->outcome, dlg->options.opts.done_data);
    }
}

static gboolean
on_close_request(GtkWindow *window, gpointer user_data)
{
    Dlg *dlg = user_data;

    (void)window;
    if (dlg->running) {
        /* Un'installazione non si interrompe a metà chiudendo la finestra: si chiede l'annullamento e si resta qui. */
        on_cancel_install(dlg);
        return TRUE;
    }
    return FALSE;
}

static void
on_destroy(GtkWidget *window, gpointer user_data)
{
    Dlg *dlg = user_data;

    (void)window;
    finish_outcome(dlg);
    dlg->alive = FALSE;
    if (dlg->pulse_id) {
        g_source_remove(dlg->pulse_id);
        dlg->pulse_id = 0;
    }
    if (dlg->running && dlg->cancellable) {
        g_cancellable_cancel(dlg->cancellable);  /* chiusura dell'applicazione: il thread finisce da solo */
    }
    log_ui("Finestra dipendenze chiusa");
    dlg_unref(dlg);
}

GtkWidget *
syncview_deps_dialog_new(GtkWindow *parent, DepsReport *report, const SyncviewDepsDialogOptions *options)
{
    g_return_val_if_fail(report != NULL, NULL);

    Dlg *dlg = g_new0(Dlg, 1);

    dlg->refs = 1;
    dlg->alive = TRUE;
    dlg->report = report;
    options_copy_init(&dlg->options, options);
    dlg->include_optional = TRUE;
    dlg->output = gtk_text_buffer_new(NULL);
    dlg->widgets = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    dlg->badges = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    dlg->bars = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    syncview_theme_init(gdk_display_get_default());

    dlg->window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dlg->window), "Preparazione di SyncView");
    gtk_window_set_default_size(GTK_WINDOW(dlg->window), 720, 680);
    gtk_widget_add_css_class(dlg->window, "syncview");
    if (parent) {
        gtk_window_set_transient_for(GTK_WINDOW(dlg->window), parent);
        gtk_window_set_modal(GTK_WINDOW(dlg->window), TRUE);
    }
    g_object_set_data(G_OBJECT(dlg->window), DIALOG_DATA, dlg);

    GtkWidget *header = gtk_header_bar_new();

    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header), make_label("Preparazione di SyncView", "sv-title", FALSE));
    gtk_window_set_titlebar(GTK_WINDOW(dlg->window), header);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *scroller = gtk_scrolled_window_new();

    dlg->content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(dlg->content, 20);
    gtk_widget_set_margin_bottom(dlg->content, 12);
    gtk_widget_set_margin_start(dlg->content, 24);
    gtk_widget_set_margin_end(dlg->content, 24);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), dlg->content);
    gtk_widget_set_vexpand(scroller, TRUE);

    dlg->footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top(dlg->footer, 8);
    gtk_widget_set_margin_bottom(dlg->footer, 16);
    gtk_widget_set_margin_start(dlg->footer, 24);
    gtk_widget_set_margin_end(dlg->footer, 24);

    gtk_box_append(GTK_BOX(root), scroller);
    gtk_box_append(GTK_BOX(root), dlg->footer);
    gtk_window_set_child(GTK_WINDOW(dlg->window), root);

    g_signal_connect(dlg->window, "close-request", G_CALLBACK(on_close_request), dlg);
    g_signal_connect(dlg->window, "destroy", G_CALLBACK(on_destroy), dlg);

    rebuild_plan(dlg);
    dlg->phase = deps_report_is_complete(dlg->report) ? SYNCVIEW_DEPS_DIALOG_ALL_OK : SYNCVIEW_DEPS_DIALOG_REVIEW;
    render(dlg);
    log_ui("Finestra dipendenze creata");
    return dlg->window;
}

SyncviewDepsDialogPhase
syncview_deps_dialog_get_phase(GtkWidget *dialog)
{
    Dlg *dlg = dlg_of(dialog);

    g_return_val_if_fail(dlg != NULL, SYNCVIEW_DEPS_DIALOG_REVIEW);
    return dlg->phase;
}

SyncviewDepsOutcome
syncview_deps_dialog_get_outcome(GtkWidget *dialog)
{
    Dlg *dlg = dlg_of(dialog);

    g_return_val_if_fail(dlg != NULL, SYNCVIEW_DEPS_OUTCOME_CLOSED);
    return dlg->outcome;
}

const DepsReport *
syncview_deps_dialog_get_report(GtkWidget *dialog)
{
    Dlg *dlg = dlg_of(dialog);

    g_return_val_if_fail(dlg != NULL, NULL);
    return dlg->report;
}

GtkWidget *
syncview_deps_dialog_get_widget(GtkWidget *dialog, const char *name)
{
    Dlg *dlg = dlg_of(dialog);

    g_return_val_if_fail(dlg != NULL && name != NULL, NULL);
    return g_hash_table_lookup(dlg->widgets, name);
}

GtkWidget *
syncview_deps_dialog_find(GtkWindow *parent)
{
    GList *toplevels = gtk_window_list_toplevels();  /* lista nuova: va liberata (le finestre no) */
    GtkWidget *found = NULL;

    for (GList *l = toplevels; l && !found; l = l->next) {
        GtkWindow *window = l->data;

        if (g_object_get_data(G_OBJECT(window), DIALOG_DATA) && gtk_window_get_transient_for(window) == parent) {
            found = GTK_WIDGET(window);
        }
    }
    g_list_free(toplevels);
    return found;
}

/* ------------------------------------------------------------------ */
/* Controllo + apertura                                                 */
/* ------------------------------------------------------------------ */

typedef struct {
    OptionsCopy options;
    SyncviewDepsShowMode mode;
    GWeakRef parent;
} CheckJob;

static void
check_job_free(CheckJob *job)
{
    options_copy_clear(&job->options);
    g_weak_ref_clear(&job->parent);
    g_free(job);
}

static void
show_check_thread(GTask *task, gpointer source, gpointer task_data, GCancellable *cancellable)
{
    CheckJob *job = task_data;

    (void)source;
    (void)cancellable;
    g_task_return_pointer(task, run_check(&job->options.opts), (GDestroyNotify)deps_report_free);
}

static void
on_show_check_done(GObject *source, GAsyncResult *result, gpointer user_data)
{
    CheckJob *job = user_data;
    DepsReport *report = g_task_propagate_pointer(G_TASK(result), NULL);
    GtkWindow *parent = g_weak_ref_get(&job->parent);

    (void)source;
    if (report) {
        DepsState *state = deps_state_load(job->options.opts.state_file);
        gboolean missing = has_missing(report, FALSE);
        gboolean show = job->mode == SYNCVIEW_DEPS_SHOW_USER_REQUESTED ||
                        (job->mode == SYNCVIEW_DEPS_SHOW_AFTER_ERROR && missing) ||
                        (job->mode == SYNCVIEW_DEPS_SHOW_STARTUP && deps_state_should_prompt(state, report));

        deps_report_log(report);
        if (show) {
            GtkWidget *dialog = syncview_deps_dialog_new(parent, report, &job->options.opts);

            report = NULL;  /* posseduto dal dialogo */
            log_user_action(job->mode == SYNCVIEW_DEPS_SHOW_USER_REQUESTED  ? "Verifica dipendenze (richiesta dall'utente)"
                            : job->mode == SYNCVIEW_DEPS_SHOW_AFTER_ERROR ? "Verifica dipendenze (plugin mancante nel player)"
                                                                          : "Verifica dipendenze all'avvio",
                            NULL);
            gtk_window_present(GTK_WINDOW(dialog));
        } else {
            /* Tutto a posto o già scelto «Continua senza»: nessuna finestra, si registra solo il controllo. */
            GError *error = NULL;

            deps_state_mark_checked(state);
            if (!has_missing(report, FALSE)) {
                deps_state_clear_declined(state);
            }
            if (!deps_state_save(state, &error)) {
                log_error("Impossibile salvare lo stato delle dipendenze", error);
                g_clear_error(&error);
            }
        }
        deps_state_free(state);
        deps_report_free(report);
    }
    g_clear_object(&parent);
    check_job_free(job);
}

void
syncview_deps_check_and_show(GtkWindow *parent, SyncviewDepsShowMode mode, const SyncviewDepsDialogOptions *options)
{
    GtkWidget *existing = syncview_deps_dialog_find(parent);

    if (existing) {
        gtk_window_present(GTK_WINDOW(existing));
        return;
    }

    CheckJob *job = g_new0(CheckJob, 1);

    options_copy_init(&job->options, options);
    job->mode = mode;
    g_weak_ref_init(&job->parent, parent);

    GTask *task = g_task_new(NULL, NULL, on_show_check_done, job);

    g_task_set_task_data(task, job, NULL);
    g_task_run_in_thread(task, show_check_thread);
    g_object_unref(task);
}

SyncviewDepsDialogOptions *
syncview_deps_dialog_options_dup(const SyncviewDepsDialogOptions *options)
{
    if (!options) {
        return NULL;
    }

    OptionsCopy *copy = g_new0(OptionsCopy, 1);

    options_copy_init(copy, options);
    return &copy->opts;  /* opts è il primo membro */
}

void
syncview_deps_dialog_options_free(SyncviewDepsDialogOptions *options)
{
    if (options) {
        OptionsCopy *copy = (OptionsCopy *)options;

        options_copy_clear(copy);
        g_free(copy);
    }
}
