#include "core/markers.h"

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *
dup_or_default(const char *s, const char *fallback)
{
    return g_strdup(s ? s : fallback);
}

/*
 * Riempie buf con l'istante corrente in formato "YYYY-MM-DDTHH:MM:SS.mmmmmm"
 * (locale, naive, senza offset — equivalente a datetime.now().isoformat()
 * in Python) e restituisce anche l'istante come secondi epoch (double, con
 * precisione al microsecondo) per la generazione dell'id.
 */
static void
current_timestamp_iso8601(char *buf, size_t buf_size, double *epoch_seconds_out)
{
    GDateTime *now = g_date_time_new_now_local();

    gint year = g_date_time_get_year(now);
    gint month = g_date_time_get_month(now);
    gint day = g_date_time_get_day_of_month(now);
    gint hour = g_date_time_get_hour(now);
    gint minute = g_date_time_get_minute(now);
    gint second = g_date_time_get_second(now);
    gint microsecond = g_date_time_get_microsecond(now);

    snprintf(buf, buf_size, "%04d-%02d-%02dT%02d:%02d:%02d.%06d",
             year, month, day, hour, minute, second, microsecond);

    if (epoch_seconds_out) {
        *epoch_seconds_out = (double)g_date_time_to_unix(now) + (double)microsecond / 1e6;
    }

    g_date_time_unref(now);
}

Marker *
marker_new(int64_t timestamp_ms, const char *color, const char *description,
           const char *category, int video_index)
{
    Marker *m = malloc(sizeof(Marker));
    if (!m) {
        return NULL;
    }

    m->timestamp_ms = timestamp_ms;
    m->color = g_strdup(color);
    m->description = dup_or_default(description, "");
    m->category = dup_or_default(category, "default");
    m->video_index = video_index;

    char created_at_buf[40];
    double epoch_seconds = 0.0;
    current_timestamp_iso8601(created_at_buf, sizeof(created_at_buf), &epoch_seconds);
    m->created_at = g_strdup(created_at_buf);

    /* Equivalente a f"marker_{timestamp}_{datetime.now().timestamp()}":
     * stessa forma (prefisso + timestamp ms + epoch secondi con
     * precisione al microsecondo), non garantito byte-identico alla
     * rappresentazione float di Python — è un id opaco, non parsato
     * altrove, l'unicità/forma sono ciò che conta. */
    m->id = g_strdup_printf("marker_%lld_%f", (long long)timestamp_ms, epoch_seconds);

    return m;
}

void
marker_free(Marker *marker)
{
    if (!marker) {
        return;
    }

    g_free(marker->id);
    g_free(marker->color);
    g_free(marker->description);
    g_free(marker->category);
    g_free(marker->created_at);
    free(marker);
}

struct MarkerStore {
    GPtrArray *markers;  /* Marker*, ordinati per timestamp_ms, owned (marker_free) */
};

MarkerStore *
marker_store_new(void)
{
    MarkerStore *store = g_new0(MarkerStore, 1);
    store->markers = g_ptr_array_new_with_free_func((GDestroyNotify)marker_free);
    return store;
}

void
marker_store_free(MarkerStore *store)
{
    if (!store) {
        return;
    }

    g_ptr_array_free(store->markers, TRUE);
    g_free(store);
}

size_t
marker_store_count(const MarkerStore *store)
{
    return store->markers->len;
}

const Marker *
marker_store_get(const MarkerStore *store, size_t index)
{
    if (index >= store->markers->len) {
        return NULL;
    }
    return g_ptr_array_index(store->markers, index);
}

static guint
find_index_by_id(const MarkerStore *store, const char *id)
{
    for (guint i = 0; i < store->markers->len; i++) {
        const Marker *m = g_ptr_array_index(store->markers, i);
        if (strcmp(m->id, id) == 0) {
            return i;
        }
    }
    return G_MAXUINT;
}

const Marker *
marker_store_find_by_id(const MarkerStore *store, const char *id)
{
    guint i = find_index_by_id(store, id);
    return i == G_MAXUINT ? NULL : g_ptr_array_index(store->markers, i);
}

/* Primo indice con timestamp > timestamp_ms (upper bound): inserendo lì,
 * i marker con timestamp uguale mantengono l'ordine di inserimento. */
static guint
upper_bound(const MarkerStore *store, int64_t timestamp_ms)
{
    guint lo = 0;
    guint hi = store->markers->len;

    while (lo < hi) {
        guint mid = lo + (hi - lo) / 2;
        const Marker *m = g_ptr_array_index(store->markers, mid);
        if (m->timestamp_ms <= timestamp_ms) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

void
marker_store_add_marker(MarkerStore *store, Marker *marker)
{
    g_ptr_array_insert(store->markers, (gint)upper_bound(store, marker->timestamp_ms), marker);
}

const Marker *
marker_store_add(MarkerStore *store, int64_t timestamp_ms, const char *color,
                 const char *description, const char *category, int video_index)
{
    Marker *m = marker_new(timestamp_ms, color, description, category, video_index);
    if (!m) {
        return NULL;
    }

    marker_store_add_marker(store, m);
    return m;
}

gboolean
marker_store_remove(MarkerStore *store, const char *id)
{
    guint i = find_index_by_id(store, id);
    if (i == G_MAXUINT) {
        return FALSE;
    }

    g_ptr_array_remove_index(store->markers, i);
    return TRUE;
}

const Marker *
marker_store_update(MarkerStore *store, const char *id, const MarkerUpdate *update)
{
    guint i = find_index_by_id(store, id);
    if (i == G_MAXUINT) {
        return NULL;
    }

    Marker *m = g_ptr_array_index(store->markers, i);

    if (update->fields & MARKER_FIELD_COLOR) {
        g_free(m->color);
        m->color = g_strdup(update->color);
    }
    if (update->fields & MARKER_FIELD_DESCRIPTION) {
        g_free(m->description);
        m->description = dup_or_default(update->description, "");
    }
    if (update->fields & MARKER_FIELD_CATEGORY) {
        g_free(m->category);
        m->category = dup_or_default(update->category, "default");
    }
    if (update->fields & MARKER_FIELD_VIDEO_INDEX) {
        m->video_index = update->video_index;
    }

    if ((update->fields & MARKER_FIELD_TIMESTAMP) && update->timestamp_ms != m->timestamp_ms) {
        m->timestamp_ms = update->timestamp_ms;
        g_ptr_array_steal_index(store->markers, i);
        marker_store_add_marker(store, m);
    }

    return m;
}

/* Primo indice con timestamp >= timestamp_ms (lower bound). */
static guint
lower_bound(const MarkerStore *store, int64_t timestamp_ms)
{
    guint lo = 0;
    guint hi = store->markers->len;

    while (lo < hi) {
        guint mid = lo + (hi - lo) / 2;
        const Marker *m = g_ptr_array_index(store->markers, mid);
        if (m->timestamp_ms < timestamp_ms) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

const Marker *
marker_store_get_at(const MarkerStore *store, int64_t timestamp_ms, int64_t tolerance_ms)
{
    guint idx = upper_bound(store, timestamp_ms);
    const Marker *left = idx > 0 ? g_ptr_array_index(store->markers, idx - 1) : NULL;
    const Marker *right = NULL;

    if (idx < store->markers->len) {
        /* Tra i marker con lo stesso timestamp a destra vince l'ultimo. */
        const Marker *first_right = g_ptr_array_index(store->markers, idx);
        right = g_ptr_array_index(store->markers, upper_bound(store, first_right->timestamp_ms) - 1);
    }

    int64_t left_dist = left ? timestamp_ms - left->timestamp_ms : G_MAXINT64;
    int64_t right_dist = right ? right->timestamp_ms - timestamp_ms : G_MAXINT64;

    if (left && left_dist <= tolerance_ms && left_dist < right_dist) {
        return left;
    }
    if (right && right_dist <= tolerance_ms) {
        return right;
    }
    return NULL;
}

const Marker *
marker_store_get_next(const MarkerStore *store, int64_t timestamp_ms)
{
    guint idx = upper_bound(store, timestamp_ms);
    return idx < store->markers->len ? g_ptr_array_index(store->markers, idx) : NULL;
}

const Marker *
marker_store_get_previous(const MarkerStore *store, int64_t timestamp_ms)
{
    guint idx = lower_bound(store, timestamp_ms);
    return idx > 0 ? g_ptr_array_index(store->markers, idx - 1) : NULL;
}

size_t
marker_store_get_range(const MarkerStore *store, int64_t start_ms, int64_t end_ms,
                       size_t *first_index)
{
    if (first_index) {
        *first_index = 0;
    }
    if (start_ms > end_ms) {
        return 0;
    }

    guint first = lower_bound(store, start_ms);
    guint end = upper_bound(store, end_ms);

    if (first_index) {
        *first_index = first;
    }
    return end > first ? end - first : 0;
}
