#include "core/markers.h"

#include <assert.h>
#include <glib.h>
#include <string.h>

static void
assert_sorted(const MarkerStore *store)
{
    for (size_t i = 1; i < marker_store_count(store); i++) {
        assert(marker_store_get(store, i - 1)->timestamp_ms <= marker_store_get(store, i)->timestamp_ms);
    }
}

static void
test_store_crud(void)
{
    MarkerStore *store = marker_store_new();
    assert(marker_store_count(store) == 0);
    assert(marker_store_get(store, 0) == NULL);

    const Marker *a = marker_store_add(store, 1000, "#e74c3c", "a", "action", 0);
    assert(a != NULL);
    assert(marker_store_count(store) == 1);
    assert(marker_store_get(store, 0) == a);
    assert(marker_store_find_by_id(store, a->id) == a);
    assert(marker_store_find_by_id(store, "inesistente") == NULL);

    /* update parziale: solo i campi indicati cambiano */
    char *a_id = g_strdup(a->id);
    MarkerUpdate up = { .fields = MARKER_FIELD_COLOR | MARKER_FIELD_DESCRIPTION,
                        .color = "#2ecc71", .description = "nuova" };
    const Marker *u = marker_store_update(store, a_id, &up);
    assert(u == a);
    assert(strcmp(u->color, "#2ecc71") == 0);
    assert(strcmp(u->description, "nuova") == 0);
    assert(strcmp(u->category, "action") == 0);
    assert(u->timestamp_ms == 1000 && u->video_index == 0);

    /* description NULL -> "" */
    up = (MarkerUpdate){ .fields = MARKER_FIELD_DESCRIPTION, .description = NULL };
    assert(strcmp(marker_store_update(store, a_id, &up)->description, "") == 0);

    up = (MarkerUpdate){ .fields = MARKER_FIELD_VIDEO_INDEX | MARKER_FIELD_CATEGORY,
                         .video_index = SYNCVIEW_MARKER_VIDEO_INDEX_ALL, .category = "note" };
    u = marker_store_update(store, a_id, &up);
    assert(u->video_index == SYNCVIEW_MARKER_VIDEO_INDEX_ALL && strcmp(u->category, "note") == 0);

    /* update di id inesistente */
    assert(marker_store_update(store, "inesistente", &up) == NULL);

    /* remove */
    assert(!marker_store_remove(store, "inesistente"));
    assert(marker_store_count(store) == 1);
    assert(marker_store_remove(store, a_id));
    assert(marker_store_count(store) == 0);
    assert(!marker_store_remove(store, a_id));

    g_free(a_id);
    marker_store_free(store);
    marker_store_free(NULL);
}

static void
test_store_ordering(void)
{
    MarkerStore *store = marker_store_new();

    /* inserimento in ordine sparso -> store sempre ordinato */
    int64_t ts[] = { 5000, 1000, 3000, 9000, 2000, 7000 };
    for (size_t i = 0; i < G_N_ELEMENTS(ts); i++) {
        marker_store_add(store, ts[i], "#000000", NULL, NULL, SYNCVIEW_MARKER_VIDEO_INDEX_ALL);
        assert_sorted(store);
    }
    assert(marker_store_count(store) == G_N_ELEMENTS(ts));
    assert(marker_store_get(store, 0)->timestamp_ms == 1000);
    assert(marker_store_get(store, 5)->timestamp_ms == 9000);

    /* a parità di timestamp vale l'ordine di inserimento (stabile) */
    const Marker *first = marker_store_add(store, 3000, "#000000", "primo", NULL, 0);
    const Marker *second = marker_store_add(store, 3000, "#000000", "secondo", NULL, 0);
    size_t i_first = 0, i_second = 0;
    for (size_t i = 0; i < marker_store_count(store); i++) {
        if (marker_store_get(store, i) == first) i_first = i;
        if (marker_store_get(store, i) == second) i_second = i;
    }
    assert(i_first < i_second);

    /* update del timestamp riposiziona il marker */
    char *second_id = g_strdup(second->id);
    MarkerUpdate up = { .fields = MARKER_FIELD_TIMESTAMP, .timestamp_ms = 100 };
    const Marker *moved = marker_store_update(store, second_id, &up);
    assert(moved->timestamp_ms == 100);
    assert(marker_store_get(store, 0) == moved);
    assert_sorted(store);

    up.timestamp_ms = 100000;
    moved = marker_store_update(store, second_id, &up);
    assert(marker_store_get(store, marker_store_count(store) - 1) == moved);
    assert_sorted(store);

    /* remove dal mezzo mantiene l'ordine */
    size_t before = marker_store_count(store);
    assert(marker_store_remove(store, first->id));
    assert(marker_store_count(store) == before - 1);
    assert_sorted(store);

    g_free(second_id);
    marker_store_free(store);
}

/* Riferimenti lineari: copie dirette delle scansioni di MarkerManager. */
static const Marker *
linear_get_at(const MarkerStore *store, int64_t t, int64_t tolerance)
{
    const Marker *closest = NULL;
    int64_t min_distance = tolerance;

    for (size_t i = 0; i < marker_store_count(store); i++) {
        const Marker *m = marker_store_get(store, i);
        int64_t d = m->timestamp_ms > t ? m->timestamp_ms - t : t - m->timestamp_ms;
        if (d <= min_distance) {
            min_distance = d;
            closest = m;
        }
    }
    return closest;
}

static const Marker *
linear_get_next(const MarkerStore *store, int64_t t)
{
    for (size_t i = 0; i < marker_store_count(store); i++) {
        const Marker *m = marker_store_get(store, i);
        if (m->timestamp_ms > t) {
            return m;
        }
    }
    return NULL;
}

static const Marker *
linear_get_previous(const MarkerStore *store, int64_t t)
{
    for (size_t i = marker_store_count(store); i > 0; i--) {
        const Marker *m = marker_store_get(store, i - 1);
        if (m->timestamp_ms < t) {
            return m;
        }
    }
    return NULL;
}

static void
check_queries_against_linear(const MarkerStore *store, int64_t t, int64_t tolerance,
                             int64_t range_end)
{
    assert(marker_store_get_at(store, t, tolerance) == linear_get_at(store, t, tolerance));
    assert(marker_store_get_next(store, t) == linear_get_next(store, t));
    assert(marker_store_get_previous(store, t) == linear_get_previous(store, t));

    size_t first = 0;
    size_t n = marker_store_get_range(store, t, range_end, &first);
    size_t expected = 0;
    size_t expected_first = 0;
    for (size_t i = 0; i < marker_store_count(store); i++) {
        int64_t ts = marker_store_get(store, i)->timestamp_ms;
        if (ts >= t && ts <= range_end) {
            if (expected == 0) {
                expected_first = i;
            }
            expected++;
        }
    }
    assert(n == expected);
    if (n > 0) {
        assert(first == expected_first);
    }
}

static void
test_store_queries_empty(void)
{
    MarkerStore *store = marker_store_new();
    assert(marker_store_get_at(store, 100, 500) == NULL);
    assert(marker_store_get_next(store, 100) == NULL);
    assert(marker_store_get_previous(store, 100) == NULL);
    assert(marker_store_get_range(store, 0, 1000, NULL) == 0);
    marker_store_free(store);
}

static void
test_store_queries_basic(void)
{
    MarkerStore *store = marker_store_new();
    marker_store_add(store, 1000, "#000000", NULL, NULL, 0);
    marker_store_add(store, 2000, "#000000", NULL, NULL, 0);
    marker_store_add(store, 3000, "#000000", NULL, NULL, 0);

    /* next/previous sono strettamente > e < */
    assert(marker_store_get_next(store, 2000)->timestamp_ms == 3000);
    assert(marker_store_get_next(store, 3000) == NULL);
    assert(marker_store_get_previous(store, 2000)->timestamp_ms == 1000);
    assert(marker_store_get_previous(store, 1000) == NULL);

    /* get_at: tolleranza inclusiva, pareggio di distanza -> successivo */
    assert(marker_store_get_at(store, 1500, 500)->timestamp_ms == 2000);
    assert(marker_store_get_at(store, 2100, 500)->timestamp_ms == 2000);
    assert(marker_store_get_at(store, 2600, 500)->timestamp_ms == 3000);
    assert(marker_store_get_at(store, 2500, 500)->timestamp_ms == 3000);
    assert(marker_store_get_at(store, 5000, 500) == NULL);
    assert(marker_store_get_at(store, 2100, 50) == NULL);

    /* range inclusivo agli estremi */
    size_t first = 99;
    assert(marker_store_get_range(store, 1000, 2000, &first) == 2 && first == 0);
    assert(marker_store_get_range(store, 1001, 1999, &first) == 0);
    assert(marker_store_get_range(store, 2000, 1000, &first) == 0);
    assert(marker_store_get_range(store, 0, 10000, NULL) == 3);

    marker_store_free(store);
}

static void
test_store_queries_vs_linear(void)
{
    GRand *rng = g_rand_new_with_seed(12345);

    /* Timestamp in un intervallo stretto: molti duplicati e pareggi di distanza. */
    for (int round = 0; round < 4; round++) {
        int64_t span = round == 0 ? 50 : (round == 1 ? 1000 : 20000);
        MarkerStore *store = marker_store_new();
        for (int i = 0; i < 200; i++) {
            marker_store_add(store, g_rand_int_range(rng, 0, (gint32)span), "#000000", NULL, NULL, 0);
        }

        for (int q = 0; q < 2000; q++) {
            int64_t t = g_rand_int_range(rng, -50, (gint32)span + 50);
            int64_t tolerance = g_rand_int_range(rng, 0, 600);
            int64_t range_end = t + g_rand_int_range(rng, -10, 3000);
            check_queries_against_linear(store, t, tolerance, range_end);
        }
        marker_store_free(store);
    }

    g_rand_free(rng);
}

int
main(void)
{
    /* Default: description e category NULL -> "" e "default" */
    Marker *m1 = marker_new(1000, "#3498db", NULL, NULL, SYNCVIEW_MARKER_VIDEO_INDEX_ALL);
    assert(m1 != NULL);
    assert(m1->timestamp_ms == 1000);
    assert(strcmp(m1->color, "#3498db") == 0);
    assert(strcmp(m1->description, "") == 0);
    assert(strcmp(m1->category, "default") == 0);
    assert(m1->video_index == SYNCVIEW_MARKER_VIDEO_INDEX_ALL);

    /* id generato, non NULL, formato "marker_<timestamp>_<epoch>" */
    assert(m1->id != NULL);
    assert(strncmp(m1->id, "marker_1000_", strlen("marker_1000_")) == 0);

    /* created_at generato, formato ISO8601 (almeno "YYYY-MM-DDT" all'inizio) */
    assert(m1->created_at != NULL);
    assert(strlen(m1->created_at) >= 10);
    assert(m1->created_at[4] == '-' && m1->created_at[7] == '-' && m1->created_at[10] == 'T');

    marker_free(m1);

    /* Valori espliciti, video_index specifico */
    Marker *m2 = marker_new(2500, "#e74c3c", "nota di test", "action", 2);
    assert(m2 != NULL);
    assert(strcmp(m2->description, "nota di test") == 0);
    assert(strcmp(m2->category, "action") == 0);
    assert(m2->video_index == 2);
    marker_free(m2);

    /* Due marker creati con lo stesso timestamp devono avere id diversi
     * (componente epoch con precisione al microsecondo). */
    Marker *a = marker_new(5000, "#000000", NULL, NULL, SYNCVIEW_MARKER_VIDEO_INDEX_ALL);
    Marker *b = marker_new(5000, "#000000", NULL, NULL, SYNCVIEW_MARKER_VIDEO_INDEX_ALL);
    assert(strcmp(a->id, b->id) != 0);
    marker_free(a);
    marker_free(b);

    /* marker_free su NULL non deve crashare */
    marker_free(NULL);

    test_store_crud();
    test_store_ordering();
    test_store_queries_empty();
    test_store_queries_basic();
    test_store_queries_vs_linear();

    return 0;
}
