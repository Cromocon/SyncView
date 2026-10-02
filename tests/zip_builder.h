#ifndef SYNCVIEW_TESTS_ZIP_BUILDER_H
#define SYNCVIEW_TESTS_ZIP_BUILDER_H

/*
 * Costruisce archivi ZIP in memoria per i test, compresi quelli volutamente malevoli: percorsi pericolosi, collegamenti
 * simbolici, voci cifrate, dichiarazioni di dimensione false, CRC sbagliati. Solo per i test.
 */
#include "core/dep_archive.h"

#include <gio/gio.h>
#include <glib.h>
#include <string.h>

typedef struct {
    const char *name;
    const guchar *data;
    gsize length;
    guint16 method;          /* 0 stored, 8 deflate (altro = numero qualunque, i dati restano «stored») */
    guint16 flags;           /* 1 = cifrata */
    guint mode;              /* permessi unix (con tipo: 0100644, 0100755, 0120777 = symlink, 040755 = dir); 0 = non unix */
    gint64 declared_size;    /* se >= 0 sovrascrive la dimensione non compressa dichiarata (bugie sulle dimensioni) */
    gboolean bad_crc;
} ZipEntry;

static void
zip_put16(GByteArray *a, guint16 v)
{
    guint8 b[2] = { v & 0xff, v >> 8 };

    g_byte_array_append(a, b, 2);
}

static void
zip_put32(GByteArray *a, guint32 v)
{
    guint8 b[4] = { v & 0xff, (v >> 8) & 0xff, (v >> 16) & 0xff, (v >> 24) & 0xff };

    g_byte_array_append(a, b, 4);
}

/* Deflate raw dei dati (come nei ZIP). */
static GBytes *
zip_deflate(const guchar *data, gsize length)
{
    GZlibCompressor *compressor = g_zlib_compressor_new(G_ZLIB_COMPRESSOR_FORMAT_RAW, 6);
    GByteArray *out = g_byte_array_new();
    guchar buffer[4096];
    gsize in_done = 0;
    gboolean finished = FALSE;

    while (!finished) {
        gsize read = 0, written = 0;
        GConverterResult result = g_converter_convert(G_CONVERTER(compressor), data + in_done, length - in_done, buffer,
                                                      sizeof buffer, G_CONVERTER_INPUT_AT_END, &read, &written, NULL);

        in_done += read;
        g_byte_array_append(out, buffer, written);
        finished = result == G_CONVERTER_FINISHED;
    }
    g_object_unref(compressor);
    return g_byte_array_free_to_bytes(out);
}

/* Ritorna i byte dello ZIP con le `n` voci. */
static GBytes *
zip_build(const ZipEntry *entries, size_t n)
{
    GByteArray *zip = g_byte_array_new();
    GByteArray *central = g_byte_array_new();

    for (size_t i = 0; i < n; i++) {
        const ZipEntry *e = &entries[i];
        guint32 crc = dep_crc32(0, e->data, e->length);
        GBytes *packed = NULL;
        const guchar *payload = e->data;
        gsize payload_len = e->length;

        if (e->bad_crc) {
            crc ^= 0xdeadbeef;
        }
        if (e->method == 8) {
            packed = zip_deflate(e->data, e->length);
            payload = g_bytes_get_data(packed, &payload_len);
        }

        guint32 offset = zip->len;
        guint16 name_len = strlen(e->name);
        guint32 uncompressed = e->declared_size >= 0 ? (guint32)e->declared_size : (guint32)e->length;

        zip_put32(zip, 0x04034b50);
        zip_put16(zip, 20);
        zip_put16(zip, e->flags);
        zip_put16(zip, e->method);
        zip_put16(zip, 0);
        zip_put16(zip, 0x21);
        zip_put32(zip, crc);
        zip_put32(zip, payload_len);
        zip_put32(zip, uncompressed);
        zip_put16(zip, name_len);
        zip_put16(zip, 0);
        g_byte_array_append(zip, (const guint8 *)e->name, name_len);
        g_byte_array_append(zip, payload, payload_len);

        zip_put32(central, 0x02014b50);
        zip_put16(central, e->mode ? (3 << 8) | 20 : 20);  /* creato su unix se c'è il modo */
        zip_put16(central, 20);
        zip_put16(central, e->flags);
        zip_put16(central, e->method);
        zip_put16(central, 0);
        zip_put16(central, 0x21);
        zip_put32(central, crc);
        zip_put32(central, payload_len);
        zip_put32(central, uncompressed);
        zip_put16(central, name_len);
        zip_put16(central, 0);
        zip_put16(central, 0);
        zip_put16(central, 0);
        zip_put16(central, 0);
        zip_put32(central, e->mode << 16);
        zip_put32(central, offset);
        g_byte_array_append(central, (const guint8 *)e->name, name_len);
        if (packed) {
            g_bytes_unref(packed);
        }
    }

    guint32 cd_offset = zip->len;

    g_byte_array_append(zip, central->data, central->len);
    zip_put32(zip, 0x06054b50);
    zip_put16(zip, 0);
    zip_put16(zip, 0);
    zip_put16(zip, n);
    zip_put16(zip, n);
    zip_put32(zip, central->len);
    zip_put32(zip, cd_offset);
    zip_put16(zip, 0);
    g_byte_array_free(central, TRUE);
    return g_byte_array_free_to_bytes(zip);
}

#endif /* SYNCVIEW_TESTS_ZIP_BUILDER_H */
