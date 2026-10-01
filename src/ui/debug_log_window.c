#include "ui/debug_log_window.h"

#include "ui/theme.h"

#include <string.h>

#define LOG_WINDOW_DATA "syncview-debug-log-window"
#define QUEUE_MAX_LINES 20000   /* tetto della coda tra sink e vista: oltre, le righe più vecchie si scartano (contate) */
#define DRAIN_BATCH 300         /* righe mostrate per iterazione del main loop: l'interfaccia resta reattiva */
#define QUEUE_RELEASE_DELAY_S 1 /* vedi on_window_destroy *//*
 * Priorità dello svuotamento della coda: sopra il ridisegno di GTK (G_PRIORITY_HIGH_IDLE + 20). Con la priorità degli
 * idle normali (200) il main loop di GTK, sempre occupato (riproduzione, frame clock), non lo eseguirebbe mai e le righe
 * comparirebbero con grande ritardo: lo si è visto sul runner macOS della CI. I blocchi sono piccoli (DRAIN_BATCH).
 */
#define DRAIN_PRIORITY G_PRIORITY_HIGH_IDLE

/* ---- Riga di log come oggetto del modello ---- */

#define SV_TYPE_LOG_LINE (sv_log_line_get_type())
G_DECLARE_FINAL_TYPE(SvLogLine, sv_log_line, SV, LOG_LINE, GObject)

struct _SvLogLine {
    GObject parent_instance;
    char *time;           /* "HH:MM:SS.mmm" */
    LoggerLevel level;
    LoggerModule module;
    char *message;
};

G_DEFINE_FINAL_TYPE(SvLogLine, sv_log_line, G_TYPE_OBJECT)

static void
sv_log_line_finalize(GObject *object)
{
    SvLogLine *line = SV_LOG_LINE(object);

    g_free(line->time);
    g_free(line->message);
    G_OBJECT_CLASS(sv_log_line_parent_class)->finalize(object);
}

static void
sv_log_line_class_init(SvLogLineClass *klass)
{
    G_OBJECT_CLASS(klass)->finalize = sv_log_line_finalize;
}

static void
sv_log_line_init(SvLogLine *line)
{
    (void)line;
}

/* ---- Coda condivisa tra il sink (thread qualsiasi) e il main thread ---- */

typedef struct {
    LoggerLevel level;
    LoggerModule module;
    char *time;
    char *message;
} PendingLine;

typedef struct {
    gatomicrefcount refs;
    GMutex lock;
    GQueue lines;            /* PendingLine*, la più vecchia in testa */
    guint dropped;
    gboolean closed;         /* la finestra non c'è più: il sink non accoda */
    gboolean drain_scheduled;
    GWeakRef window;         /* la finestra (per il main thread) */
} LogQueue;

static void
pending_line_free(PendingLine *line)
{
    g_free(line->time);
    g_free(line->message);
    g_free(line);
}

/* Svuota la coda (con il lock già preso, o quando nessun altro la usa più). */
static void
log_queue_discard_lines(LogQueue *q)
{
    PendingLine *line;

    while ((line = g_queue_pop_head(&q->lines)) != NULL) {
        pending_line_free(line);
    }
}

static LogQueue *
log_queue_new(GtkWidget *window)
{
    LogQueue *q = g_new0(LogQueue, 1);

    g_atomic_ref_count_init(&q->refs);
    g_mutex_init(&q->lock);
    g_queue_init(&q->lines);
    g_weak_ref_init(&q->window, window);
    return q;
}

static LogQueue *
log_queue_ref(LogQueue *q)
{
    g_atomic_ref_count_inc(&q->refs);
    return q;
}

static void
log_queue_unref(LogQueue *q)
{
    if (!g_atomic_ref_count_dec(&q->refs)) {
        return;
    }
    log_queue_discard_lines(q);
    g_weak_ref_clear(&q->window);
    g_mutex_clear(&q->lock);
    g_free(q);
}

/* ---- Finestra ---- */

typedef struct {
    GtkWidget *window;
    LogQueue *queue;
    guint sink_id;

    GListStore *store;               /* SvLogLine, la più vecchia in testa */
    GtkFilterListModel *filtered;
    GtkFilter *filter;
    GtkWidget *list_view;
    GtkWidget *scrolled;
    GtkWidget *live_chip;
    GtkWidget *pause_button;
    GtkWidget *level_dropdown;
    GtkWidget *footer_left;
    GtkWidget *footer_right;
    GtkWidget *module_chips[LOGGER_MODULE_COUNT];
    GHashTable *widgets;

    LoggerLevel min_level;           /* filtro di vista */
    gboolean module_visible[LOGGER_MODULE_COUNT];
    gboolean paused;
    gboolean autoscroll;
    gboolean scrolling_by_us;
} LogWindow;

static const char *LEVEL_LABELS[] = { "DEBUG", "INFO", "! WARN", "✕ ERR" };
static const char *LEVEL_CLASSES[] = { "sv-lvl-debug", "sv-lvl-info", "sv-lvl-warn", "sv-lvl-err" };

static LogWindow *
lw_of(GtkWidget *window)
{
    return window ? g_object_get_data(G_OBJECT(window), LOG_WINDOW_DATA) : NULL;
}

static guint
active_module_count(void)
{
    guint n = 0;

    for (int m = 0; m < LOGGER_MODULE_COUNT; m++) {
        n += logger_is_module_enabled((LoggerModule)m) ? 1 : 0;
    }
    return n;
}

static void
update_footer(LogWindow *lw)
{
    guint total = g_list_model_get_n_items(G_LIST_MODEL(lw->store));
    guint visible = g_list_model_get_n_items(G_LIST_MODEL(lw->filtered));
    guint pending, dropped;

    g_mutex_lock(&lw->queue->lock);
    pending = g_queue_get_length(&lw->queue->lines);
    dropped = lw->queue->dropped;
    g_mutex_unlock(&lw->queue->lock);

    GString *left = g_string_new(NULL);

    if (visible == total) {
        g_string_append_printf(left, "%u righe", total);
    } else {
        g_string_append_printf(left, "%u di %u righe", visible, total);
    }
    g_string_append_printf(left, " · %u moduli attivi", active_module_count());
    if (pending > 0) {
        g_string_append_printf(left, " · %u in attesa", pending);
    }
    if (dropped > 0) {
        g_string_append_printf(left, " · %u scartate", dropped);
    }
    gtk_label_set_text(GTK_LABEL(lw->footer_left), left->str);
    g_string_free(left, TRUE);

    gtk_label_set_text(GTK_LABEL(lw->footer_right),
                       (lw->autoscroll && !lw->paused) ? "Scorrimento automatico: ON" : "Scorrimento automatico: OFF");
}

static void
scroll_to_bottom(LogWindow *lw)
{
    guint n = g_list_model_get_n_items(G_LIST_MODEL(lw->filtered));

    if (n == 0) {
        return;
    }
    lw->scrolling_by_us = TRUE;
    gtk_list_view_scroll_to(GTK_LIST_VIEW(lw->list_view), n - 1, GTK_LIST_SCROLL_NONE, NULL);
    lw->scrolling_by_us = FALSE;
}

/* Sposta in vista le righe in coda (al più `limit`); ritorna quante ne restano. */
static guint
drain_queue(LogWindow *lw, guint limit)
{
    GPtrArray *items = g_ptr_array_new_with_free_func(g_object_unref);
    guint remaining;

    g_mutex_lock(&lw->queue->lock);
    while (items->len < limit && !g_queue_is_empty(&lw->queue->lines)) {
        PendingLine *p = g_queue_pop_head(&lw->queue->lines);
        SvLogLine *line = g_object_new(SV_TYPE_LOG_LINE, NULL);
        const char *space = strchr(p->time, ' ');

        line->time = g_strdup(space ? space + 1 : p->time);  /* solo l'ora: la data non serve in una sessione */
        line->level = p->level;
        line->module = p->module;
        line->message = g_steal_pointer(&p->message);

        /* L'etichetta «[MARKER] » nel testo è già nella colonna del modulo: si toglie solo se coincide (resta «[VIDEO 1]»). */
        char *tag = g_strdup_printf("[%s] ", logger_module_name(p->module));

        if (g_str_has_prefix(line->message, tag)) {
            char *stripped = g_strdup(line->message + strlen(tag));

            g_free(line->message);
            line->message = stripped;
        }
        g_free(tag);
        g_ptr_array_add(items, line);
        pending_line_free(p);
    }
    remaining = g_queue_get_length(&lw->queue->lines);
    g_mutex_unlock(&lw->queue->lock);

    if (items->len > 0) {
        g_list_store_splice(lw->store, g_list_model_get_n_items(G_LIST_MODEL(lw->store)), 0, items->pdata, items->len);

        guint total = g_list_model_get_n_items(G_LIST_MODEL(lw->store));

        if (total > SYNCVIEW_LOG_WINDOW_MAX_LINES) {
            g_list_store_splice(lw->store, 0, total - SYNCVIEW_LOG_WINDOW_MAX_LINES, NULL, 0);
        }
        if (lw->autoscroll && !lw->paused) {
            scroll_to_bottom(lw);
        }
    }
    g_ptr_array_free(items, TRUE);
    return remaining;
}

static gboolean
on_drain_idle(gpointer user_data)
{
    LogQueue *q = user_data;
    GtkWidget *window = g_weak_ref_get(&q->window);
    LogWindow *lw = window ? lw_of(window) : NULL;
    gboolean paused = TRUE;  /* senza finestra non c'è nulla da mostrare */

    if (lw) {
        paused = lw->paused;
        if (!paused) {
            drain_queue(lw, DRAIN_BATCH);
        }
        update_footer(lw);
    }

    /*
     * «Ci sono righe da mostrare?» e «azzera il flag di idle programmato» vanno decisi INSIEME sotto lo stesso lock: se il
     * flag si azzerasse dopo aver visto la coda vuota, una riga accodata nel frattempo da un altro thread (che vede il
     * flag ancora attivo e non programma nulla) resterebbe in coda per sempre.
     */
    g_mutex_lock(&q->lock);
    gboolean again = !paused && !g_queue_is_empty(&q->lines);

    if (!again) {
        q->drain_scheduled = FALSE;
    }
    g_mutex_unlock(&q->lock);

    g_clear_object(&window);
    if (!again) {
        log_queue_unref(q);  /* il riferimento preso quando l'idle è stato programmato */
    }
    return again ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
}

/* Sink del logger: gira nel thread di chi logga. Non tocca widget: accoda e programma il main thread. */
static void
log_sink(LoggerLevel level, LoggerModule module, const char *timestamp, const char *message, gpointer user_data)
{
    LogQueue *q = user_data;
    gboolean schedule = FALSE;

    g_mutex_lock(&q->lock);
    if (!q->closed) {
        PendingLine *p = g_new0(PendingLine, 1);

        p->level = level;
        p->module = module;
        p->time = g_strdup(timestamp);
        p->message = g_strdup(message);
        g_queue_push_tail(&q->lines, p);
        if (g_queue_get_length(&q->lines) > QUEUE_MAX_LINES) {
            pending_line_free(g_queue_pop_head(&q->lines));
            q->dropped++;
        }
        if (!q->drain_scheduled) {
            q->drain_scheduled = TRUE;
            schedule = TRUE;
        }
    }
    g_mutex_unlock(&q->lock);

    if (schedule) {
        g_idle_add_full(DRAIN_PRIORITY, on_drain_idle, log_queue_ref(q), NULL);
    }
}

/* ---- Filtro di vista ---- */

static gboolean
line_visible(gpointer item, gpointer user_data)
{
    LogWindow *lw = user_data;
    SvLogLine *line = item;

    return line->level >= lw->min_level && lw->module_visible[line->module];
}

static void
refilter(LogWindow *lw)
{
    gtk_filter_changed(lw->filter, GTK_FILTER_CHANGE_DIFFERENT);
    if (lw->autoscroll && !lw->paused) {
        scroll_to_bottom(lw);
    }
    update_footer(lw);
}

static void
on_level_selected(GObject *dropdown, GParamSpec *pspec, gpointer user_data)
{
    LogWindow *lw = user_data;

    (void)pspec;
    lw->min_level = (LoggerLevel)gtk_drop_down_get_selected(GTK_DROP_DOWN(dropdown));
    refilter(lw);
}

static void
on_module_chip_toggled(GtkToggleButton *chip, gpointer user_data)
{
    LogWindow *lw = user_data;

    for (int m = 0; m < LOGGER_MODULE_COUNT; m++) {
        if (GTK_WIDGET(chip) == lw->module_chips[m]) {
            gboolean on = gtk_toggle_button_get_active(chip);
            char *label = g_strdup_printf("%s%s", on ? "✓ " : "", logger_module_name((LoggerModule)m));

            lw->module_visible[m] = on;
            gtk_button_set_label(GTK_BUTTON(chip), label);
            g_free(label);
            refilter(lw);
            return;
        }
    }
}

/* ---- Pausa, copia, svuota, scorrimento ---- */

static void
update_live_chip(LogWindow *lw)
{
    gtk_label_set_text(GTK_LABEL(lw->live_chip), lw->paused ? "‖ In pausa" : "● In diretta");
    if (lw->paused) {
        gtk_widget_add_css_class(lw->live_chip, "sv-paused");
    } else {
        gtk_widget_remove_css_class(lw->live_chip, "sv-paused");
    }
    gtk_button_set_label(GTK_BUTTON(lw->pause_button), lw->paused ? "Riprendi" : "Pausa");
}

static void
on_pause_toggled(GtkToggleButton *button, gpointer user_data)
{
    LogWindow *lw = user_data;

    lw->paused = gtk_toggle_button_get_active(button);
    update_live_chip(lw);
    if (!lw->paused) {
        /* Alla ripresa compaiono le righe arrivate nel frattempo e si torna a seguire il fondo. */
        lw->autoscroll = TRUE;
        g_idle_add_full(DRAIN_PRIORITY, on_drain_idle, log_queue_ref(lw->queue), NULL);
        g_mutex_lock(&lw->queue->lock);
        lw->queue->drain_scheduled = TRUE;
        g_mutex_unlock(&lw->queue->lock);
    }
    update_footer(lw);
}

static void
on_clear_clicked(GtkButton *button, gpointer user_data)
{
    LogWindow *lw = user_data;

    (void)button;
    g_list_store_remove_all(lw->store);
    g_mutex_lock(&lw->queue->lock);
    log_queue_discard_lines(lw->queue);
    lw->queue->dropped = 0;
    g_mutex_unlock(&lw->queue->lock);
    update_footer(lw);
}

static void
on_copy_clicked(GtkButton *button, gpointer user_data)
{
    LogWindow *lw = user_data;
    GString *text = g_string_new(NULL);
    guint n = g_list_model_get_n_items(G_LIST_MODEL(lw->filtered));

    (void)button;
    for (guint i = 0; i < n; i++) {
        SvLogLine *line = g_list_model_get_item(G_LIST_MODEL(lw->filtered), i);

        g_string_append_printf(text, "%s %s %s %s\n", line->time, LEVEL_LABELS[line->level],
                               logger_module_name(line->module), line->message);
        g_object_unref(line);
    }
    gdk_clipboard_set_text(gtk_widget_get_clipboard(lw->window), text->str);
    g_string_free(text, TRUE);
}

static void
on_vadjustment_changed(GtkAdjustment *adjustment, gpointer user_data)
{
    LogWindow *lw = user_data;

    if (lw->scrolling_by_us) {
        return;
    }

    /* Scorrere verso l'alto sospende lo scorrimento automatico; tornare in fondo lo riprende. */
    double bottom = gtk_adjustment_get_upper(adjustment) - gtk_adjustment_get_page_size(adjustment);
    gboolean at_bottom = gtk_adjustment_get_value(adjustment) >= bottom - 2.0;

    if (at_bottom != lw->autoscroll) {
        lw->autoscroll = at_bottom;
        update_footer(lw);
    }
}

/* ---- Righe della lista ---- */

static void
on_factory_setup(GtkSignalListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *time = gtk_label_new(NULL);
    GtkWidget *level = gtk_label_new(NULL);
    GtkWidget *module = gtk_label_new(NULL);
    GtkWidget *message = gtk_label_new(NULL);

    (void)factory;
    (void)user_data;
    gtk_widget_add_css_class(row, "sv-log-row");
    gtk_widget_add_css_class(time, "sv-log-time");
    gtk_widget_add_css_class(level, "sv-lvl");
    gtk_widget_add_css_class(module, "sv-log-module");
    gtk_widget_add_css_class(message, "sv-log-message");
    gtk_label_set_xalign(GTK_LABEL(time), 0.0f);
    gtk_label_set_width_chars(GTK_LABEL(time), 12);
    gtk_label_set_width_chars(GTK_LABEL(level), 7);
    gtk_label_set_xalign(GTK_LABEL(module), 0.0f);
    gtk_label_set_width_chars(GTK_LABEL(module), 7);
    gtk_label_set_xalign(GTK_LABEL(message), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(message), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(message, TRUE);
    gtk_widget_set_valign(level, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(row), time);
    gtk_box_append(GTK_BOX(row), level);
    gtk_box_append(GTK_BOX(row), module);
    gtk_box_append(GTK_BOX(row), message);
    gtk_list_item_set_child(item, row);
}

static void
on_factory_bind(GtkSignalListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
    SvLogLine *line = gtk_list_item_get_item(item);
    GtkWidget *row = gtk_list_item_get_child(item);
    GtkWidget *time = gtk_widget_get_first_child(row);
    GtkWidget *level = gtk_widget_get_next_sibling(time);
    GtkWidget *module = gtk_widget_get_next_sibling(level);
    GtkWidget *message = gtk_widget_get_next_sibling(module);

    (void)factory;
    (void)user_data;
    gtk_label_set_text(GTK_LABEL(time), line->time);
    gtk_label_set_text(GTK_LABEL(level), LEVEL_LABELS[line->level]);
    for (int i = 0; i < 4; i++) {
        gtk_widget_remove_css_class(level, LEVEL_CLASSES[i]);
    }
    gtk_widget_add_css_class(level, LEVEL_CLASSES[line->level]);
    gtk_label_set_text(GTK_LABEL(module), logger_module_name(line->module));
    gtk_label_set_text(GTK_LABEL(message), line->message);
    gtk_widget_set_tooltip_text(message, line->message);
}

/* ---- Costruzione ---- */

static GtkWidget *
reg(LogWindow *lw, const char *name, GtkWidget *widget)
{
    g_hash_table_insert(lw->widgets, g_strdup(name), widget);
    return widget;
}

static GtkWidget *
make_small_button(const char *label)
{
    GtkWidget *button = gtk_button_new_with_label(label);

    gtk_widget_add_css_class(button, "sv-step");
    return button;
}

static gboolean
release_sink_reference(gpointer user_data)
{
    log_queue_unref(user_data);
    return G_SOURCE_REMOVE;
}

static void
on_window_destroy(GtkWidget *window, gpointer user_data)
{
    LogWindow *lw = user_data;

    (void)window;
    g_mutex_lock(&lw->queue->lock);
    lw->queue->closed = TRUE;
    g_mutex_unlock(&lw->queue->lock);
    logger_remove_sink(lw->sink_id);

    /*
     * Dopo logger_remove_sink() una chiamata già in corso in un altro thread può ancora usare la coda (vedi logger.h):
     * il riferimento che ne ha il sink si rilascia con un po' di ritardo, quando quella chiamata è sicuramente finita.
     * Il riferimento della finestra si rilascia subito.
     */
    g_timeout_add_seconds(QUEUE_RELEASE_DELAY_S, release_sink_reference, lw->queue);
    log_queue_unref(lw->queue);

    /* Il modello filtrato possiede lo store; il filtro non deve più chiamare la funzione che usa `lw`. */
    gtk_custom_filter_set_filter_func(GTK_CUSTOM_FILTER(lw->filter), NULL, NULL, NULL);
    g_object_unref(lw->filter);
    g_object_unref(lw->filtered);
    g_hash_table_destroy(lw->widgets);
    g_free(lw);
}

GtkWidget *
syncview_debug_log_window_new(GtkApplication *app, GtkWindow *parent)
{
    LogWindow *lw = g_new0(LogWindow, 1);

    syncview_theme_init(gdk_display_get_default());

    lw->window = gtk_application_window_new(app);
    lw->widgets = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    lw->min_level = LOGGER_LEVEL_DEBUG;
    lw->autoscroll = TRUE;
    for (int m = 0; m < LOGGER_MODULE_COUNT; m++) {
        lw->module_visible[m] = TRUE;
    }
    lw->queue = log_queue_new(lw->window);

    gtk_window_set_title(GTK_WINDOW(lw->window), "Debug · Log in tempo reale");
    gtk_window_set_default_size(GTK_WINDOW(lw->window), 960, 560);
    gtk_widget_add_css_class(lw->window, "syncview");
    if (parent) {
        gtk_window_set_transient_for(GTK_WINDOW(lw->window), parent);
    }
    g_object_set_data(G_OBJECT(lw->window), LOG_WINDOW_DATA, lw);

    /* Barra del titolo: nome + stato «in diretta»/«in pausa». */
    GtkWidget *header = gtk_header_bar_new();
    GtkWidget *title_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *title = gtk_label_new("Debug · Log in tempo reale");

    gtk_widget_add_css_class(title, "sv-title");
    lw->live_chip = reg(lw, "live", gtk_label_new(""));
    gtk_widget_add_css_class(lw->live_chip, "sv-chip-live");
    gtk_box_append(GTK_BOX(title_box), title);
    gtk_box_append(GTK_BOX(title_box), lw->live_chip);
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header), title_box);
    gtk_window_set_titlebar(GTK_WINDOW(lw->window), header);

    /* Barra degli strumenti: chip dei moduli, livello, Pausa, Copia, Svuota. */
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *modules_label = gtk_label_new("Moduli");

    gtk_widget_add_css_class(toolbar, "sv-log-toolbar");
    gtk_widget_add_css_class(modules_label, "sv-log-time");
    gtk_box_append(GTK_BOX(toolbar), modules_label);
    for (int m = 0; m < LOGGER_MODULE_COUNT; m++) {
        char *label = g_strdup_printf("✓ %s", logger_module_name((LoggerModule)m));
        char *name = g_strdup_printf("chip-%s", logger_module_name((LoggerModule)m));
        GtkWidget *chip = gtk_toggle_button_new_with_label(label);

        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chip), TRUE);
        gtk_widget_add_css_class(chip, "sv-filter");
        g_signal_connect(chip, "toggled", G_CALLBACK(on_module_chip_toggled), lw);
        lw->module_chips[m] = reg(lw, name, chip);
        gtk_box_append(GTK_BOX(toolbar), chip);
        g_free(name);
        g_free(label);
    }
    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(toolbar), spacer);

    const char *levels[] = { "Livello: Debug", "Livello: Info", "Livello: Avviso", "Livello: Errore", NULL };

    lw->level_dropdown = reg(lw, "level", gtk_drop_down_new_from_strings(levels));
    gtk_widget_add_css_class(lw->level_dropdown, "sv-dropdown");
    g_signal_connect(lw->level_dropdown, "notify::selected", G_CALLBACK(on_level_selected), lw);
    gtk_box_append(GTK_BOX(toolbar), lw->level_dropdown);

    lw->pause_button = reg(lw, "pause", gtk_toggle_button_new_with_label("Pausa"));
    gtk_widget_add_css_class(lw->pause_button, "sv-step");
    g_signal_connect(lw->pause_button, "toggled", G_CALLBACK(on_pause_toggled), lw);
    gtk_box_append(GTK_BOX(toolbar), lw->pause_button);

    GtkWidget *copy = reg(lw, "copy", make_small_button("Copia"));
    GtkWidget *clear = reg(lw, "clear", make_small_button("Svuota"));

    g_signal_connect(copy, "clicked", G_CALLBACK(on_copy_clicked), lw);
    g_signal_connect(clear, "clicked", G_CALLBACK(on_clear_clicked), lw);
    gtk_box_append(GTK_BOX(toolbar), copy);
    gtk_box_append(GTK_BOX(toolbar), clear);

    /* Lista: modello → filtro di vista → nessuna selezione. */
    lw->store = g_list_store_new(SV_TYPE_LOG_LINE);
    lw->filter = GTK_FILTER(gtk_custom_filter_new(line_visible, lw, NULL));
    lw->filtered = gtk_filter_list_model_new(G_LIST_MODEL(lw->store), g_object_ref(lw->filter));

    GtkListItemFactory *factory = gtk_signal_list_item_factory_new();

    g_signal_connect(factory, "setup", G_CALLBACK(on_factory_setup), NULL);
    g_signal_connect(factory, "bind", G_CALLBACK(on_factory_bind), NULL);
    GtkNoSelection *selection = gtk_no_selection_new(g_object_ref(G_LIST_MODEL(lw->filtered)));

    lw->list_view = reg(lw, "list", gtk_list_view_new(GTK_SELECTION_MODEL(selection), factory));
    gtk_widget_add_css_class(lw->list_view, "sv-log-list");
    lw->scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(lw->scrolled), lw->list_view);
    gtk_widget_set_vexpand(lw->scrolled, TRUE);
    g_signal_connect(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(lw->scrolled)), "value-changed",
                     G_CALLBACK(on_vadjustment_changed), lw);

    /* Riga di stato. */
    GtkWidget *status = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);

    gtk_widget_add_css_class(status, "sv-statusbar");
    lw->footer_left = reg(lw, "footer", gtk_label_new(""));
    lw->footer_right = reg(lw, "autoscroll", gtk_label_new(""));
    gtk_widget_set_hexpand(lw->footer_left, TRUE);
    gtk_label_set_xalign(GTK_LABEL(lw->footer_left), 0.0f);
    gtk_box_append(GTK_BOX(status), lw->footer_left);
    gtk_box_append(GTK_BOX(status), lw->footer_right);

    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    gtk_box_append(GTK_BOX(content), toolbar);
    gtk_box_append(GTK_BOX(content), lw->scrolled);
    gtk_box_append(GTK_BOX(content), status);
    gtk_window_set_child(GTK_WINDOW(lw->window), content);

    update_live_chip(lw);
    update_footer(lw);
    g_signal_connect(lw->window, "destroy", G_CALLBACK(on_window_destroy), lw);

    /* Ultimo passo: da qui in poi il logger può recapitare righe. */
    lw->sink_id = logger_add_sink(log_sink, log_queue_ref(lw->queue));
    return lw->window;
}

/* ---- Accessori (test) ---- */

guint
syncview_debug_log_window_get_line_count(GtkWidget *window)
{
    LogWindow *lw = lw_of(window);

    g_return_val_if_fail(lw != NULL, 0);
    return g_list_model_get_n_items(G_LIST_MODEL(lw->store));
}

guint
syncview_debug_log_window_get_visible_count(GtkWidget *window)
{
    LogWindow *lw = lw_of(window);

    g_return_val_if_fail(lw != NULL, 0);
    return g_list_model_get_n_items(G_LIST_MODEL(lw->filtered));
}

guint
syncview_debug_log_window_get_pending_count(GtkWidget *window)
{
    LogWindow *lw = lw_of(window);

    g_return_val_if_fail(lw != NULL, 0);
    g_mutex_lock(&lw->queue->lock);
    guint n = g_queue_get_length(&lw->queue->lines);
    g_mutex_unlock(&lw->queue->lock);
    return n;
}

guint
syncview_debug_log_window_get_dropped_count(GtkWidget *window)
{
    LogWindow *lw = lw_of(window);

    g_return_val_if_fail(lw != NULL, 0);
    g_mutex_lock(&lw->queue->lock);
    guint n = lw->queue->dropped;
    g_mutex_unlock(&lw->queue->lock);
    return n;
}

gboolean
syncview_debug_log_window_get_line(GtkWidget *window, guint index, LoggerLevel *level, LoggerModule *module,
                                   const char **message)
{
    LogWindow *lw = lw_of(window);

    g_return_val_if_fail(lw != NULL, FALSE);

    SvLogLine *line = g_list_model_get_item(G_LIST_MODEL(lw->store), index);

    if (!line) {
        return FALSE;
    }
    if (level) {
        *level = line->level;
    }
    if (module) {
        *module = line->module;
    }
    if (message) {
        *message = line->message;  /* il modello tiene la riga viva; vale fino alla prossima modifica */
    }
    g_object_unref(line);
    return TRUE;
}

void
syncview_debug_log_window_flush(GtkWidget *window)
{
    LogWindow *lw = lw_of(window);

    g_return_if_fail(lw != NULL);
    if (!lw->paused) {
        while (drain_queue(lw, DRAIN_BATCH) > 0) {
        }
    }
    update_footer(lw);
}

GtkWidget *
syncview_debug_log_window_get_widget(GtkWidget *window, const char *name)
{
    LogWindow *lw = lw_of(window);

    g_return_val_if_fail(lw != NULL && name != NULL, NULL);
    return g_hash_table_lookup(lw->widgets, name);
}
