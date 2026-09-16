#include "util/time_format.h"

#include <stdio.h>

char *
syncview_format_time_ms(int64_t ms, char *buf, size_t buf_size)
{
    if (ms < 0) {
        ms = 0;
    }

    int64_t hours = ms / 3600000;
    int64_t minutes = (ms / 60000) % 60;
    int64_t seconds = (ms / 1000) % 60;
    int64_t millis = ms % 1000;

    snprintf(buf, buf_size, "%02lld:%02lld:%02lld.%03lld",
             (long long)hours, (long long)minutes, (long long)seconds, (long long)millis);

    return buf;
}
