#include "core/user_paths.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>

static char *
read_file(const char *path)
{
    char *content = NULL;
    g_assert_true(g_file_get_contents(path, &content, NULL, NULL));
    return content;
}

static void
test_defaults(const char *dir)
{
    /* Directory padre inesistente: creata; file assente: valori vuoti. */
    char *file = g_build_filename(dir, "a", "b", "user_paths.json", NULL);
    UserPaths *p = user_paths_new(file);
    char *parent = g_path_get_dirname(file);
    g_assert_true(g_file_test(parent, G_FILE_TEST_IS_DIR));
    g_free(parent);

    for (int i = 0; i < SYNCVIEW_MAX_VIDEOS; i++) {
        g_assert_null(user_paths_get_video_path(p, i));
    }
    g_assert_null(user_paths_get_export_dir(p));

    /* Indici non validi: get → NULL, set/clear → errore, nulla scritto. */
    GError *error = NULL;
    g_assert_null(user_paths_get_video_path(p, -1));
    g_assert_null(user_paths_get_video_path(p, SYNCVIEW_MAX_VIDEOS));
    g_assert_false(user_paths_set_video_path(p, SYNCVIEW_MAX_VIDEOS, "/x", &error));
    g_assert_true(error->domain == USER_PATHS_ERROR && error->code == USER_PATHS_ERROR_INDEX);
    g_clear_error(&error);
    g_assert_false(user_paths_clear_video_path(p, -1, &error));
    g_clear_error(&error);
    g_assert_false(g_file_test(file, G_FILE_TEST_EXISTS));

    user_paths_free(p);
    user_paths_free(NULL);
    g_free(file);
}

static void
test_roundtrip(const char *dir)
{
    char *file = g_build_filename(dir, "roundtrip.json", NULL);
    UserPaths *p = user_paths_new(file);

    g_assert_true(user_paths_set_video_path(p, 0, "/video/uno.mp4", NULL));
    g_assert_true(user_paths_set_video_path(p, 3, "/video/quattro \xc3\xa8.mkv", NULL));
    g_assert_true(user_paths_set_export_dir(p, "/export/dir", NULL));
    g_assert_true(g_file_test(file, G_FILE_TEST_IS_REGULAR));  /* salvato subito ad ogni set */

    /* Formato compatibile con la versione Python. */
    char *json = read_file(file);
    g_assert_true(strstr(json, "\"video_paths\"") && strstr(json, "\"last_export_dir\""));
    g_assert_true(strstr(json, "null"));
    g_free(json);
    user_paths_free(p);

    /* Nuova istanza sullo stesso file: stessi valori. */
    p = user_paths_new(file);
    g_assert_cmpstr(user_paths_get_video_path(p, 0), ==, "/video/uno.mp4");
    g_assert_null(user_paths_get_video_path(p, 1));
    g_assert_null(user_paths_get_video_path(p, 2));
    g_assert_cmpstr(user_paths_get_video_path(p, 3), ==, "/video/quattro \xc3\xa8.mkv");
    g_assert_cmpstr(user_paths_get_export_dir(p), ==, "/export/dir");

    /* clear persiste; export_dir NULL lo svuota. */
    g_assert_true(user_paths_clear_video_path(p, 0, NULL));
    g_assert_true(user_paths_set_export_dir(p, NULL, NULL));
    user_paths_free(p);

    p = user_paths_new(file);
    g_assert_null(user_paths_get_video_path(p, 0));
    g_assert_nonnull(user_paths_get_video_path(p, 3));
    g_assert_null(user_paths_get_export_dir(p));
    user_paths_free(p);

    g_free(file);
}

static void
test_load_legacy_and_invalid(const char *dir)
{
    char *file = g_build_filename(dir, "legacy.json", NULL);

    /* File scritto dalla versione Python (indent=2): stringhe vuote e null → slot vuoto. */
    g_assert_true(g_file_set_contents(file,
        "{\n  \"video_paths\": [\n    \"/a.mp4\",\n    null,\n    \"\",\n    \"/d.mp4\"\n  ],\n"
        "  \"last_export_dir\": \"/out\"\n}", -1, NULL));
    UserPaths *p = user_paths_new(file);
    g_assert_cmpstr(user_paths_get_video_path(p, 0), ==, "/a.mp4");
    g_assert_true(user_paths_get_video_path(p, 1) == NULL && user_paths_get_video_path(p, 2) == NULL);
    g_assert_cmpstr(user_paths_get_video_path(p, 3), ==, "/d.mp4");
    g_assert_cmpstr(user_paths_get_export_dir(p), ==, "/out");
    user_paths_free(p);

    /* Lista più corta: slot mancanti vuoti; più lunga: extra ignorati. */
    g_assert_true(g_file_set_contents(file, "{\"video_paths\": [\"/x.mp4\"]}", -1, NULL));
    p = user_paths_new(file);
    g_assert_cmpstr(user_paths_get_video_path(p, 0), ==, "/x.mp4");
    g_assert_true(user_paths_get_video_path(p, 1) == NULL && user_paths_get_export_dir(p) == NULL);
    user_paths_free(p);

    g_assert_true(g_file_set_contents(file,
        "{\"video_paths\": [\"/1\",\"/2\",\"/3\",\"/4\",\"/5\",\"/6\"]}", -1, NULL));
    p = user_paths_new(file);
    g_assert_cmpstr(user_paths_get_video_path(p, 3), ==, "/4");
    user_paths_free(p);

    /* File corrotto o di struttura inattesa: valori vuoti, nessun crash. */
    const char *bad[] = { "non json {", "[1,2]", "", "{\"video_paths\": \"stringa\"}" };
    for (size_t i = 0; i < G_N_ELEMENTS(bad); i++) {
        g_assert_true(g_file_set_contents(file, bad[i], -1, NULL));
        p = user_paths_new(file);
        for (int s = 0; s < SYNCVIEW_MAX_VIDEOS; s++) {
            g_assert_null(user_paths_get_video_path(p, s));
        }
        g_assert_null(user_paths_get_export_dir(p));
        user_paths_free(p);
    }

    g_free(file);
}

static void
test_valid_paths_pruning(const char *dir)
{
    char *file = g_build_filename(dir, "prune.json", NULL);
    char *real = g_build_filename(dir, "esiste.mp4", NULL);
    char *gone = g_build_filename(dir, "sparito.mp4", NULL);
    char *subdir = g_build_filename(dir, "una_dir", NULL);
    g_assert_true(g_file_set_contents(real, "x", -1, NULL));
    g_assert_cmpint(g_mkdir_with_parents(subdir, 0755), ==, 0);

    UserPaths *p = user_paths_new(file);
    g_assert_true(user_paths_set_video_path(p, 0, real, NULL));
    g_assert_true(user_paths_set_video_path(p, 1, gone, NULL));
    g_assert_true(user_paths_set_video_path(p, 2, subdir, NULL));  /* esiste ma non è un file */

    const char *out[SYNCVIEW_MAX_VIDEOS];
    g_assert_cmpint(user_paths_get_valid_video_paths(p, out, NULL), ==, 1);
    g_assert_cmpstr(out[0], ==, real);
    g_assert_true(out[1] == NULL && out[2] == NULL && out[3] == NULL);

    /* Gli slot non validi sono stati rimossi dalla memoria e dal file. */
    g_assert_true(user_paths_get_video_path(p, 1) == NULL && user_paths_get_video_path(p, 2) == NULL);
    user_paths_free(p);
    p = user_paths_new(file);
    g_assert_cmpstr(user_paths_get_video_path(p, 0), ==, real);
    g_assert_true(user_paths_get_video_path(p, 1) == NULL && user_paths_get_video_path(p, 2) == NULL);

    /* Nessuna modifica: il file non viene riscritto. */
    char *before = read_file(file);
    g_assert_true(g_file_set_contents(file, before, -1, NULL));
    g_assert_cmpint(user_paths_get_valid_video_paths(p, out, NULL), ==, 1);
    user_paths_free(p);
    g_free(before);

    g_free(file);
    g_free(real);
    g_free(gone);
    g_free(subdir);
}

static void
test_save_failure(const char *dir)
{
    /* Il file di destinazione è una directory: il salvataggio fallisce, il valore resta in memoria. */
    char *file = g_build_filename(dir, "isdir.json", NULL);
    g_assert_cmpint(g_mkdir_with_parents(file, 0755), ==, 0);

    UserPaths *p = user_paths_new(file);
    GError *error = NULL;
    g_assert_false(user_paths_set_video_path(p, 0, "/v.mp4", &error));
    g_assert_true(error->domain == USER_PATHS_ERROR && error->code == USER_PATHS_ERROR_IO);
    g_error_free(error);
    g_assert_cmpstr(user_paths_get_video_path(p, 0), ==, "/v.mp4");

    user_paths_free(p);
    g_free(file);
}

static void
test_default_file(void)
{
    char *f = user_paths_default_file();
    g_assert_true(g_str_has_suffix(f, "user_paths.json"));
    g_assert_nonnull(strstr(f, ".syncview"));
    g_assert_true(g_path_is_absolute(f));
    g_free(f);
}

int
main(void)
{
    char *dir = g_dir_make_tmp("syncview-user-paths-XXXXXX", NULL);
    g_assert_nonnull(dir);

    test_defaults(dir);
    test_roundtrip(dir);
    test_load_legacy_and_invalid(dir);
    test_valid_paths_pruning(dir);
    test_save_failure(dir);
    test_default_file();

    char *cmd = g_strdup_printf("rm -rf '%s'", dir);
    g_assert_cmpint(system(cmd), ==, 0);
    g_free(cmd);
    g_free(dir);
    return 0;
}
