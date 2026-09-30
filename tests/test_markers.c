#include "core/markers.h"

#include <assert.h>
#include <string.h>

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

    return 0;
}
