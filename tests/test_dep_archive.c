/* M2.11: estrazione sicura di archivi ZIP (core/dep_archive) con archivi validi e malevoli costruiti in memoria. */
#include "zip_builder.h"

#include <assert.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define ENTRY(...) ((ZipEntry){ __VA_ARGS__ })

static char *root;       /* directory di lavoro del test */
static int counter;

static char *
new_dest(void)
{
    char *dest = g_strdup_printf("%s/out-%d", root, counter++);

    assert(g_mkdir_with_parents(dest, 0700) == 0);
    return dest;
}

static char *
write_zip(const ZipEntry *entries, size_t n)
{
    GBytes *zip = zip_build(entries, n);
    char *path = g_strdup_printf("%s/archive-%d.zip", root, counter++);
    gsize size;
    const char *data = g_bytes_get_data(zip, &size);

    assert(g_file_set_contents(path, data, size, NULL));
    g_bytes_unref(zip);
    return path;
}

static char *
read_text(const char *path)
{
    char *text = NULL;

    return g_file_get_contents(path, &text, NULL, NULL) ? text : NULL;
}

/* Elenca ricorsivamente i file sotto `dir` (percorsi relativi, ordinati). */
static void
list_files(const char *base, const char *rel, GPtrArray *out)
{
    char *dir = rel[0] ? g_build_filename(base, rel, NULL) : g_strdup(base);
    GDir *d = g_dir_open(dir, 0, NULL);
    const char *name;

    while (d && (name = g_dir_read_name(d)) != NULL) {
        char *child_rel = rel[0] ? g_build_filename(rel, name, NULL) : g_strdup(name);
        char *child = g_build_filename(base, child_rel, NULL);

        if (g_file_test(child, G_FILE_TEST_IS_DIR)) {
            list_files(base, child_rel, out);
        } else {
            g_ptr_array_add(out, g_strdup(child_rel));
        }
        g_free(child);
        g_free(child_rel);
    }
    if (d) {
        g_dir_close(d);
    }
    g_free(dir);
}

static guint
count_files(const char *dir)
{
    GPtrArray *files = g_ptr_array_new_with_free_func(g_free);
    guint n;

    list_files(dir, "", files);
    n = files->len;
    g_ptr_array_free(files, TRUE);
    return n;
}

/* L'archivio viene rifiutato con il codice atteso e nulla finisce fuori da `dest`. */
static void
expect_rejected(const ZipEntry *entries, size_t n, DepArchiveError code, int strip, gint64 max_total)
{
    char *zip = write_zip(entries, n);
    char *dest = new_dest();
    char *outside = g_strdup_printf("%s/evil.txt", root);
    GError *error = NULL;

    g_remove(outside);
    assert(!dep_archive_extract_zip(zip, dest, strip, max_total, &error));
    assert(error != NULL);
    if (!g_error_matches(error, DEP_ARCHIVE_ERROR, code)) {
        g_printerr("codice errore inatteso: %d (%s), atteso %d\n", error->code, error->message, code);
        abort();
    }
    assert(!g_file_test(outside, G_FILE_TEST_EXISTS));  /* nessun file fuori dalla destinazione */
    g_error_free(error);
    g_free(outside);
    g_free(dest);
    g_free(zip);
}

static void
test_valid_archive(void)
{
    const guchar hello[] = "ciao mondo";
    guchar big[20000];

    for (size_t i = 0; i < sizeof big; i++) {
        big[i] = (guchar)("abcdefghij"[i % 10]);  /* molto comprimibile */
    }
    ZipEntry entries[] = {
        ENTRY("pacchetto/", NULL, 0, 0, 0, 040755, -1, FALSE),
        ENTRY("pacchetto/bin/", NULL, 0, 0, 0, 040755, -1, FALSE),
        ENTRY("pacchetto/bin/ffmpeg", hello, sizeof hello - 1, 8, 0, 0100755, -1, FALSE),
        ENTRY("pacchetto/leggimi.txt", big, sizeof big, 8, 0, 0100644, -1, FALSE),
        ENTRY("pacchetto/note.txt", hello, sizeof hello - 1, 0, 0, 0100644, -1, FALSE),
        ENTRY("pacchetto/vuoto.txt", NULL, 0, 0, 0, 0100644, -1, FALSE),
    };
    char *zip = write_zip(entries, G_N_ELEMENTS(entries));
    char *dest = new_dest();
    GError *error = NULL;

    assert(dep_archive_extract_zip(zip, dest, 1, 0, &error));  /* strip 1: «pacchetto/» sparisce */
    assert(error == NULL);

    char *p = g_build_filename(dest, "bin", "ffmpeg", NULL);
    char *text = read_text(p);

    assert(text && strcmp(text, "ciao mondo") == 0);
    g_free(text);
#ifdef G_OS_UNIX
    struct stat st;

    assert(stat(p, &st) == 0 && (st.st_mode & 0111) != 0);  /* il bit di esecuzione è rispettato */
#endif
    g_free(p);

    p = g_build_filename(dest, "leggimi.txt", NULL);
    text = read_text(p);
    assert(text && strlen(text) == sizeof big && memcmp(text, big, sizeof big) == 0);
    g_free(text);
    g_free(p);
#ifdef G_OS_UNIX
    p = g_build_filename(dest, "note.txt", NULL);
    assert(stat(p, &st) == 0 && (st.st_mode & 0111) == 0);
    g_free(p);
#endif
    p = g_build_filename(dest, "vuoto.txt", NULL);
    assert(g_file_test(p, G_FILE_TEST_EXISTS));
    g_free(p);
    assert(count_files(dest) == 4);

    /* Senza strip la cartella resta. */
    char *dest2 = new_dest();

    assert(dep_archive_extract_zip(zip, dest2, 0, 0, &error));
    p = g_build_filename(dest2, "pacchetto", "bin", "ffmpeg", NULL);
    assert(g_file_test(p, G_FILE_TEST_EXISTS));
    g_free(p);

    g_free(dest2);
    g_free(dest);
    g_free(zip);
}

static void
test_zip_slip_and_unsafe_paths(void)
{
    const guchar data[] = "dati";
    const char *bad_names[] = {
        "../evil.txt",             /* risale fuori */
        "a/../../evil.txt",
        "/etc/evil.txt",           /* assoluto */
        "C:/evil.txt",             /* lettera di unità */
        "dir\\..\\evil.txt",       /* backslash */
        "a//b.txt",                /* componente vuoto */
        "./x.txt",                 /* componente «.» */
        "..",
    };

    for (size_t i = 0; i < G_N_ELEMENTS(bad_names); i++) {
        ZipEntry entries[] = {
            ENTRY("buono.txt", data, 4, 0, 0, 0100644, -1, FALSE),
            ENTRY(bad_names[i], data, 4, 0, 0, 0100644, -1, FALSE),
        };

        expect_rejected(entries, 2, DEP_ARCHIVE_ERROR_UNSAFE, 0, 0);
    }

    /* Anche con lo strip: un «..» dentro i componenti scartati resta un percorso pericoloso. */
    ZipEntry strip_trick[] = { ENTRY("../x/evil.txt", data, 4, 0, 0, 0100644, -1, FALSE) };

    expect_rejected(strip_trick, 1, DEP_ARCHIVE_ERROR_UNSAFE, 2, 0);

    ZipEntry duplicate[] = {
        ENTRY("x.txt", data, 4, 0, 0, 0100644, -1, FALSE),
        ENTRY("x.txt", data, 4, 0, 0, 0100644, -1, FALSE),
    };

    expect_rejected(duplicate, 2, DEP_ARCHIVE_ERROR_UNSAFE, 0, 0);

    ZipEntry symlink_entry[] = { ENTRY("link", (const guchar *)"/etc/passwd", 11, 0, 0, 0120777, -1, FALSE) };

    expect_rejected(symlink_entry, 1, DEP_ARCHIVE_ERROR_UNSAFE, 0, 0);

    ZipEntry device[] = { ENTRY("dev", data, 4, 0, 0, 020666, -1, FALSE) };  /* file speciale */

    expect_rejected(device, 1, DEP_ARCHIVE_ERROR_UNSAFE, 0, 0);
}

static void
test_unsupported_and_limits(void)
{
    const guchar data[] = "dati";

    ZipEntry encrypted[] = { ENTRY("x.txt", data, 4, 0, 1, 0100644, -1, FALSE) };

    expect_rejected(encrypted, 1, DEP_ARCHIVE_ERROR_FORMAT, 0, 0);

    ZipEntry method[] = { ENTRY("x.txt", data, 4, 12, 0, 0100644, -1, FALSE) };  /* bzip2: non supportato */

    expect_rejected(method, 1, DEP_ARCHIVE_ERROR_FORMAT, 0, 0);

    /* Dimensione dichiarata oltre il tetto (per voce / totale). */
    ZipEntry huge[] = { ENTRY("x.bin", data, 4, 0, 0, 0100644, (gint64)3 << 30 >> 1, FALSE) };  /* 1,5 GiB dichiarati */

    expect_rejected(huge, 1, DEP_ARCHIVE_ERROR_TOO_LARGE, 0, 0);

    ZipEntry two[] = {
        ENTRY("a.bin", data, 4, 0, 0, 0100644, -1, FALSE),
        ENTRY("b.bin", data, 4, 0, 0, 0100644, -1, FALSE),
    };

    expect_rejected(two, 2, DEP_ARCHIVE_ERROR_TOO_LARGE, 0, 6);  /* tetto totale di 6 byte: la seconda voce lo supera */

    /* Zip bomb: dichiara 10 byte ma i dati compressi ne producono 100000. */
    guchar zeros[100000];

    memset(zeros, 0, sizeof zeros);
    ZipEntry bomb[] = { ENTRY("bomba.bin", zeros, sizeof zeros, 8, 0, 0100644, 10, FALSE) };

    expect_rejected(bomb, 1, DEP_ARCHIVE_ERROR_TOO_LARGE, 0, 0);

    /* CRC sbagliato e dimensione dichiarata diversa da quella reale. */
    ZipEntry crc[] = { ENTRY("x.txt", data, 4, 0, 0, 0100644, -1, TRUE) };

    expect_rejected(crc, 1, DEP_ARCHIVE_ERROR_CORRUPT, 0, 0);

    ZipEntry size_lie[] = { ENTRY("x.txt", data, 4, 8, 0, 0100644, 3, FALSE) };  /* deflate: 4 byte reali, 3 dichiarati */

    expect_rejected(size_lie, 1, DEP_ARCHIVE_ERROR_TOO_LARGE, 0, 0);
}

static void
test_malformed_files(void)
{
    char *dest = new_dest();
    char *path = g_strdup_printf("%s/garbage.zip", root);
    GError *error = NULL;

    assert(g_file_set_contents(path, "questo non e' uno zip, e basta, davvero davvero", -1, NULL));
    assert(!dep_archive_extract_zip(path, dest, 0, 0, &error));
    assert(g_error_matches(error, DEP_ARCHIVE_ERROR, DEP_ARCHIVE_ERROR_FORMAT));
    g_clear_error(&error);

    /* Troncato a metà: fine della directory centrale mancante. */
    const guchar data[] = "dati";
    ZipEntry entries[] = { ENTRY("x.txt", data, 4, 0, 0, 0100644, -1, FALSE) };
    GBytes *zip = zip_build(entries, 1);
    gsize size;
    const char *bytes = g_bytes_get_data(zip, &size);

    assert(g_file_set_contents(path, bytes, size / 2, NULL));
    assert(!dep_archive_extract_zip(path, dest, 0, 0, &error));
    assert(g_error_matches(error, DEP_ARCHIVE_ERROR, DEP_ARCHIVE_ERROR_FORMAT));
    g_clear_error(&error);

    /* File inesistente: errore di I/O, nessun crash. */
    assert(!dep_archive_extract_zip("/non/esiste.zip", dest, 0, 0, &error));
    g_clear_error(&error);
    assert(!dep_archive_extract_zip("/non/esiste.zip", dest, 0, 0, NULL));  /* error NULL non fa crashare */

    g_bytes_unref(zip);
    g_free(path);
    g_free(dest);
}

static void
test_crc32(void)
{
    /* Valore noto: CRC-32 di «123456789» = 0xCBF43926. */
    assert(dep_crc32(0, (const guchar *)"123456789", 9) == 0xCBF43926u);
    assert(dep_crc32(0, (const guchar *)"", 0) == 0);
    /* Calcolo a pezzi = calcolo in un colpo. */
    guint32 part = dep_crc32(0, (const guchar *)"12345", 5);

    assert(dep_crc32(part, (const guchar *)"6789", 4) == 0xCBF43926u);
}

int
main(void)
{
    root = g_dir_make_tmp("syncview-archive-XXXXXX", NULL);
    assert(root != NULL);

    test_crc32();
    test_valid_archive();
    test_zip_slip_and_unsafe_paths();
    test_unsupported_and_limits();
    test_malformed_files();

    g_free(root);
    return 0;
}
