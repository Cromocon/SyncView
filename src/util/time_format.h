#ifndef SYNCVIEW_UTIL_TIME_FORMAT_H
#define SYNCVIEW_UTIL_TIME_FORMAT_H

#include <stddef.h>
#include <stdint.h>

/* "HH:MM:SS.mmm\0" */
#define SYNCVIEW_TIME_FORMAT_BUFSIZE 13

/*
 * Formatta una durata in millisecondi come "HH:MM:SS.mmm" in buf.
 * Valori negativi vengono trattati come 0 (nessun timestamp negativo
 * ha senso nel dominio dell'app). buf deve avere almeno
 * SYNCVIEW_TIME_FORMAT_BUFSIZE byte.
 *
 * Ritorna buf.
 */
char *syncview_format_time_ms(int64_t ms, char *buf, size_t buf_size);

#endif /* SYNCVIEW_UTIL_TIME_FORMAT_H */
