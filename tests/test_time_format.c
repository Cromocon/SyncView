#include "util/time_format.h"

#include <assert.h>
#include <string.h>

static void
check(int64_t ms, const char *expected)
{
    char buf[SYNCVIEW_TIME_FORMAT_BUFSIZE];
    syncview_format_time_ms(ms, buf, sizeof(buf));
    assert(strcmp(buf, expected) == 0);
}

int
main(void)
{
    /* 0ms */
    check(0, "00:00:00.000");

    /* ms singoli */
    check(1, "00:00:00.001");
    check(999, "00:00:00.999");

    /* secondi/minuti */
    check(1000, "00:00:01.000");
    check(61000, "00:01:01.000");

    /* > 1h */
    check(3600000, "01:00:00.000");
    check(3661001, "01:01:01.001");

    /* valore negativo: clamp a 0 */
    check(-500, "00:00:00.000");

    return 0;
}
