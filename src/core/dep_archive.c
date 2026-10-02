#include "core/dep_archive.h"

#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>

G_DEFINE_QUARK(syncview-dep-archive-error-quark, dep_archive_error)

#define SIG_EOCD 0x06054b50u
#define SIG_CENTRAL 0x02014b50u
#define SIG_LOCAL 0x04034b50u

/* ---- CRC-32 ---- */

static guint32 crc_table[256];
static gsize crc_table_ready;

static void
crc_table_init(void)
{
    if (g_once_init_enter(&crc_table_ready)) {
        for (guint32 n = 0; n < 256; n++) {
            guint32 c = n;

            for (int k = 0; k < 8; k++) {
                c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            }
            crc_table[n] = c;
        }
        g_once_init_leave(&crc_table_ready, 1);
    }
}

guint32
dep_crc32(guint32 crc, const guchar *data, gsize length)
{
    crc_table_init();
    crc = ~crc;
    for (gsize i = 0; i < length; i++) {
        crc = crc_table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
    }
    return ~crc;
}

/* ---- Lettura little-endian con controllo dei limiti ---- */

static guint16
rd16(const guchar *p)
{
    return (guint16)(p[0] | (p[1] << 8));
}

static guint32
rd32(const guchar *p)
{
    return (guint32)p[0] | ((guint32)p[1] << 8) | ((guint32)p[2] << 16) | ((guint32)p[3] << 24);
}

static gboolean
fail(GError **error, DepArchiveError code, const char *fmt, ...) G_GNUC_PRINTF(3, 4);

static gboolean
fail(GError **error, DepArchiveError code, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    g_set_error_literal(error, DEP_ARCHIVE_ERROR, code, "");
    if (error && *error) {
        g_free((*error)->message);
        (*error)->message = g_strdup_vprintf(fmt, args);
    }
    va_end(args);
    return FALSE;
}

/* ---- Percorsi ---- */

/*
 * Controlla un nome di voce e lo riduce di `strip` componenti. Ritorna il percorso relativo (g_free), "" se la voce
 * sparisce con lo strip, o NULL (con error) se è pericoloso. `is_dir` se il nome finisce con «/».
 */
static char *
clean_entry_path(const char *name, gsize name_len, int strip, gboolean *is_dir, GError **error)
{
    if (name_len == 0 || memchr(name, '\0', name_len) != NULL) {
        fail(error, DEP_ARCHIVE_ERROR_UNSAFE, "voce con nome vuoto o con byte nulli");
        return NULL;
    }

    char *copy = g_strndup(name, name_len);

    if (!g_utf8_validate(copy, -1, NULL) || strchr(copy, '\\') != NULL || copy[0] == '/' ||
        (g_ascii_isalpha(copy[0]) && copy[1] == ':')) {
        fail(error, DEP_ARCHIVE_ERROR_UNSAFE, "percorso non consentito nell'archivio: «%s»", copy);
        g_free(copy);
        return NULL;
    }

    *is_dir = g_str_has_suffix(copy, "/");

    char **parts = g_strsplit(copy, "/", -1);
    GPtrArray *kept = g_ptr_array_new();
    int index = 0;
    gboolean ok = TRUE;

    for (int i = 0; parts[i]; i++) {
        gboolean last = parts[i + 1] == NULL;

        if (last && *is_dir && parts[i][0] == '\0') {
            continue;  /* il «/» finale */
        }
        if (parts[i][0] == '\0' || strcmp(parts[i], ".") == 0 || strcmp(parts[i], "..") == 0) {
            ok = FALSE;
            break;
        }
        if (index++ >= strip) {
            g_ptr_array_add(kept, parts[i]);
        }
    }

    char *result = NULL;

    if (!ok) {
        fail(error, DEP_ARCHIVE_ERROR_UNSAFE, "percorso non consentito nell'archivio: «%s»", copy);
    } else {
        g_ptr_array_add(kept, NULL);
        result = g_strjoinv("/", (char **)kept->pdata);
    }
    g_ptr_array_free(kept, TRUE);
    g_strfreev(parts);
    g_free(copy);
    return result;
}

/* ---- Decompressione ---- */

typedef struct {
    FILE *out;
    guint32 crc;
    gint64 written;
} Sink;

static gboolean
inflate_entry(const guchar *data, gsize compressed, gint64 expected, Sink *sink, GError **error)
{
    GZlibDecompressor *inflater = g_zlib_decompressor_new(G_ZLIB_COMPRESSOR_FORMAT_RAW);
    guchar buffer[65536];
    gsize in_done = 0;
    gboolean finished = FALSE;
    gboolean ok = TRUE;

    while (!finished && ok) {
        gsize read = 0, written = 0;
        GError *err = NULL;
        GConverterResult result = g_converter_convert(G_CONVERTER(inflater), data + in_done, compressed - in_done, buffer,
                                                      sizeof buffer, G_CONVERTER_INPUT_AT_END, &read, &written, &err);

        in_done += read;
        if (err) {
            ok = fail(error, DEP_ARCHIVE_ERROR_CORRUPT, "dati compressi non validi: %s", err->message);
            g_error_free(err);
            break;
        }
        if (written > 0) {
            sink->written += (gint64)written;
            if (sink->written > expected) {
                ok = fail(error, DEP_ARCHIVE_ERROR_TOO_LARGE, "i dati superano la dimensione dichiarata (zip bomb?)");
                break;
            }
            sink->crc = dep_crc32(sink->crc, buffer, written);
            if (fwrite(buffer, 1, written, sink->out) != written) {
                ok = fail(error, DEP_ARCHIVE_ERROR_IO, "scrittura non riuscita");
                break;
            }
        }
        finished = result == G_CONVERTER_FINISHED;
        if (!finished && read == 0 && written == 0) {
            ok = fail(error, DEP_ARCHIVE_ERROR_CORRUPT, "flusso compresso troncato");
        }
    }
    g_object_unref(inflater);
    return ok;
}

/* ---- Estrazione ---- */

typedef struct {
    guint16 method, flags, made_by;
    guint32 crc, comp_size, size, ext_attrs, local_offset;
    const guchar *name;
    guint16 name_len;
} Entry;

gboolean
dep_archive_extract_zip(const char *zip_path, const char *dest_dir, int strip_components, gint64 max_total_bytes,
                        GError **error)
{
    GMappedFile *map = g_mapped_file_new(zip_path, FALSE, error);

    if (!map) {
        return FALSE;
    }

    const guchar *file = (const guchar *)g_mapped_file_get_contents(map);
    gsize length = g_mapped_file_get_length(map);
    GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    gint64 total = 0;
    gboolean ok = TRUE;

    if (max_total_bytes <= 0) {
        max_total_bytes = DEP_ARCHIVE_MAX_TOTAL_BYTES;
    }

    /* Fine della directory centrale (EOCD): negli ultimi 64 KiB + 22 byte. */
    if (length < 22) {
        ok = fail(error, DEP_ARCHIVE_ERROR_FORMAT, "file troppo corto per essere uno ZIP");
        goto done;
    }

    gsize eocd = G_MAXSIZE;

    for (gsize back = 22; back <= length && back <= 65557; back++) {
        if (rd32(file + length - back) == SIG_EOCD) {
            eocd = length - back;
            break;
        }
    }
    if (eocd == G_MAXSIZE) {
        ok = fail(error, DEP_ARCHIVE_ERROR_FORMAT, "non è uno ZIP (fine della directory centrale non trovata)");
        goto done;
    }

    guint16 disk = rd16(file + eocd + 4), cd_disk = rd16(file + eocd + 6);
    guint16 entries_here = rd16(file + eocd + 8), entries = rd16(file + eocd + 10);
    guint32 cd_size = rd32(file + eocd + 12), cd_offset = rd32(file + eocd + 16);

    if (disk != 0 || cd_disk != 0 || entries != entries_here) {
        ok = fail(error, DEP_ARCHIVE_ERROR_FORMAT, "archivi su più dischi non supportati");
        goto done;
    }
    if (entries == 0xffff || cd_size == 0xffffffffu || cd_offset == 0xffffffffu) {
        ok = fail(error, DEP_ARCHIVE_ERROR_FORMAT, "ZIP64 non supportato");
        goto done;
    }
    if (entries > DEP_ARCHIVE_MAX_ENTRIES) {
        ok = fail(error, DEP_ARCHIVE_ERROR_TOO_LARGE, "troppe voci nell'archivio (%u)", entries);
        goto done;
    }
    if ((gsize)cd_offset + cd_size > eocd) {
        ok = fail(error, DEP_ARCHIVE_ERROR_FORMAT, "directory centrale fuori dal file");
        goto done;
    }

    gsize pos = cd_offset;

    for (guint i = 0; i < entries && ok; i++) {
        Entry e;

        if (pos + 46 > (gsize)cd_offset + cd_size || rd32(file + pos) != SIG_CENTRAL) {
            ok = fail(error, DEP_ARCHIVE_ERROR_FORMAT, "directory centrale non valida");
            break;
        }
        e.made_by = rd16(file + pos + 4);
        e.flags = rd16(file + pos + 8);
        e.method = rd16(file + pos + 10);
        e.crc = rd32(file + pos + 16);
        e.comp_size = rd32(file + pos + 20);
        e.size = rd32(file + pos + 24);
        e.name_len = rd16(file + pos + 28);

        guint16 extra_len = rd16(file + pos + 30), comment_len = rd16(file + pos + 32);

        e.ext_attrs = rd32(file + pos + 38);
        e.local_offset = rd32(file + pos + 42);
        e.name = file + pos + 46;
        if (pos + 46 + e.name_len + extra_len + comment_len > (gsize)cd_offset + cd_size) {
            ok = fail(error, DEP_ARCHIVE_ERROR_FORMAT, "voce della directory centrale fuori dai limiti");
            break;
        }
        pos += 46 + e.name_len + extra_len + comment_len;

        /* Tipo di voce: solo file regolari e directory; niente collegamenti simbolici né file speciali. */
        guint mode = (e.made_by >> 8) == 3 ? (e.ext_attrs >> 16) : 0;
        guint type = mode & 0xF000;

        if (type != 0 && type != 0x8000 && type != 0x4000) {
            ok = fail(error, DEP_ARCHIVE_ERROR_UNSAFE, "voce speciale o collegamento simbolico nell'archivio");
            break;
        }

        gboolean is_dir = FALSE;
        char *relative = clean_entry_path((const char *)e.name, e.name_len, strip_components, &is_dir, error);

        if (!relative) {
            ok = FALSE;
            break;
        }
        if (relative[0] == '\0') {
            g_free(relative);  /* voce scartata dallo strip */
            continue;
        }
        if (!g_hash_table_add(seen, g_strdup(relative))) {
            ok = fail(error, DEP_ARCHIVE_ERROR_UNSAFE, "voce duplicata nell'archivio: «%s»", relative);
            g_free(relative);
            break;
        }

        char *target = g_build_filename(dest_dir, relative, NULL);

        if (is_dir) {
            if (g_mkdir_with_parents(target, 0755) != 0) {
                ok = fail(error, DEP_ARCHIVE_ERROR_IO, "impossibile creare la cartella «%s»", relative);
            }
            g_free(target);
            g_free(relative);
            continue;
        }

        if ((e.flags & 1) || (e.method != 0 && e.method != 8)) {
            ok = fail(error, DEP_ARCHIVE_ERROR_FORMAT, "voce cifrata o con compressione non supportata: «%s»", relative);
        } else if (e.size > DEP_ARCHIVE_MAX_ENTRY_BYTES || (gint64)e.size + total > max_total_bytes) {
            ok = fail(error, DEP_ARCHIVE_ERROR_TOO_LARGE, "«%s» supera i limiti di dimensione", relative);
        } else if ((gsize)e.local_offset + 30 > length || rd32(file + e.local_offset) != SIG_LOCAL) {
            ok = fail(error, DEP_ARCHIVE_ERROR_FORMAT, "intestazione locale non valida per «%s»", relative);
        } else {
            gsize data_start = (gsize)e.local_offset + 30 + rd16(file + e.local_offset + 26) + rd16(file + e.local_offset + 28);

            if (data_start > length || (gsize)e.comp_size > length - data_start) {
                ok = fail(error, DEP_ARCHIVE_ERROR_FORMAT, "dati di «%s» fuori dal file", relative);
            } else if (e.method == 0 && e.comp_size != e.size) {
                ok = fail(error, DEP_ARCHIVE_ERROR_CORRUPT, "dimensioni incoerenti per «%s»", relative);
            } else {
                char *parent = g_path_get_dirname(target);
                FILE *out = NULL;

                if (g_mkdir_with_parents(parent, 0755) != 0 || !(out = g_fopen(target, "wb"))) {
                    ok = fail(error, DEP_ARCHIVE_ERROR_IO, "impossibile scrivere «%s»", relative);
                } else {
                    Sink sink = { out, 0, 0 };
                    const guchar *data = file + data_start;

                    if (e.method == 0) {
                        sink.written = e.size;
                        sink.crc = dep_crc32(0, data, e.size);
                        if (e.size > 0 && fwrite(data, 1, e.size, out) != e.size) {
                            ok = fail(error, DEP_ARCHIVE_ERROR_IO, "scrittura non riuscita");
                        }
                    } else {
                        ok = inflate_entry(data, e.comp_size, e.size, &sink, error);
                    }
                    if (fclose(out) != 0 && ok) {
                        ok = fail(error, DEP_ARCHIVE_ERROR_IO, "scrittura non riuscita");
                    }
                    if (ok && (sink.written != (gint64)e.size || sink.crc != e.crc)) {
                        ok = fail(error, DEP_ARCHIVE_ERROR_CORRUPT, "CRC o dimensione non corrispondono per «%s»", relative);
                    }
                    if (ok) {
                        g_chmod(target, (mode & 0111) ? 0755 : 0644);
                        total += e.size;
                    }
                }
                g_free(parent);
            }
        }
        g_free(target);
        g_free(relative);
    }

done:
    g_hash_table_destroy(seen);
    g_mapped_file_unref(map);
    return ok;
}
