#include "core/markers.h"

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>

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
