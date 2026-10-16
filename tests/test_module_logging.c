/*
 * Verifica che i moduli M1 emettano i log previsti dal piano (eventi della
 * modalità debug + azioni utente dell'originale) passando dal logger
 * centrale, e che senza debug stderr resti vuoto.
 */
#include "core/logger.h"
#include "core/marker_db.h"
#include "core/markers.h"
#include "core/sync_manager.h"
#include "core/user_paths.h"

#include <fcntl.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int saved_stderr = -1;

static void
capture_begin(const char *path)
{
    fflush(stderr);
    saved_stderr = dup(2);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    g_assert_true(fd >= 0 && saved_stderr >= 0);
    dup2(fd, 2);
    close(fd);
}

static void
capture_end(void)
{
    fflush(stderr);
    dup2(saved_stderr, 2);
    close(saved_stderr);
}

static char *
read_file(const char *path)
{
    char *content = NULL;
    g_assert_true(g_file_get_contents(path, &content, NULL, NULL));
    return content;
}

/* Mock player minimale per sync_all_to_master. */
static bool mock_loaded(void *ud) { return *(bool *)ud; }
static void mock_seek(void *ud, int64_t pos) { (void)ud; (void)pos; }
static void mock_pause(void *ud) { (void)ud; }

static void
exercise_modules(const char *dir)
{
    /* sync_manager */
    SyncManager sm;
    sync_manager_init(&sm, 2);
    sync_manager_set_offset(&sm, 1, 500);
    sync_manager_set_master(&sm, 0);
    sync_manager_set_enabled(&sm, true);
    sync_manager_calculate_sync_position(&sm, 100, 0, 1);
    bool loaded = true, not_loaded = false;
    SyncPlayerOps players[3] = {
        { .user_data = &loaded, .is_loaded = mock_loaded, .seek = mock_seek, .pause = mock_pause },
        { .user_data = &loaded, .is_loaded = mock_loaded, .seek = mock_seek, .pause = mock_pause },
        { .user_data = &not_loaded, .is_loaded = mock_loaded, .seek = mock_seek, .pause = mock_pause },
    };
    sync_manager_sync_all_to_master(&sm, 2000, players, 3);
    sync_manager_reset_offsets(&sm);

    /* marker store + marker_db */
    char *db_path = g_build_filename(dir, "m.markers.db", NULL);
    char *json_path = g_build_filename(dir, "m.json", NULL);
    g_assert_true(g_file_set_contents(json_path, "{\"markers\":[{\"timestamp\":5,\"color\":\"#fff\"}]}", -1, NULL));

    MarkerDb *db = marker_db_open_migrating(db_path, json_path, NULL, NULL);
    g_assert_nonnull(db);

    MarkerStore *store = marker_store_new();
    const Marker *a = marker_store_add(store, 1000, "#000000", NULL, NULL, 0);
    char *a_id = g_strdup(a->id);
    MarkerUpdate up = { .fields = MARKER_FIELD_COLOR, .color = "#111111" };
    marker_store_update(store, a_id, &up);
    g_assert_true(marker_db_save_batch(db, store, NULL));
    MarkerStore *loaded_store = marker_db_load_all(db, FALSE, NULL);
    marker_store_free(loaded_store);
    g_assert_true(marker_db_delete(db, a_id, NULL));
    marker_store_remove(store, a_id);

    /* Errore: due marker con stessa terna UNIQUE → batch fallito e log_error. */
    Marker *x = marker_new(9, "#000000", NULL, NULL, 1);
    Marker *y = marker_new(9, "#000000", NULL, NULL, 1);
    g_free(y->id);
    y->id = g_strdup("altro");
    g_free(y->created_at);
    y->created_at = g_strdup(x->created_at);
    marker_store_add_marker(store, x);
    marker_store_add_marker(store, y);
    GError *error = NULL;
    g_assert_false(marker_db_save_batch(db, store, &error));
    g_error_free(error);

    marker_store_free(store);
    marker_db_free(db);

    /* user_paths */
    char *paths_file = g_build_filename(dir, "up.json", NULL);
    char *gone_path = g_build_filename(dir, "esiste_no.mp4", NULL);
    UserPaths *up_paths = user_paths_new(paths_file);
    g_assert_true(user_paths_set_video_path(up_paths, 0, gone_path, NULL));
    const char *out[SYNCVIEW_MAX_VIDEOS];
    user_paths_get_valid_video_paths(up_paths, out, NULL);
    user_paths_free(up_paths);

    g_free(a_id);
    g_free(db_path);
    g_free(json_path);
    g_free(paths_file);
    g_free(gone_path);
}

static void
run(const char *dir, gboolean debug, char **file_out, char **err_out)
{
    g_unsetenv("SYNCVIEW_DEBUG");
    char *sub = g_build_filename(dir, debug ? "dbg" : "norm", NULL);
    g_assert_cmpint(g_mkdir_with_parents(sub, 0755), ==, 0);
    char *log_path = g_build_filename(sub, "log.txt", NULL);
    char *err_path = g_build_filename(sub, "stderr.txt", NULL);

    capture_begin(err_path);
    g_assert_true(logger_init(log_path, debug, NULL));
    exercise_modules(sub);
    logger_shutdown();
    capture_end();

    *file_out = read_file(log_path);
    *err_out = read_file(err_path);
    g_free(sub);
    g_free(log_path);
    g_free(err_path);
}

#define HAS(text, needle) (strstr((text), (needle)) != NULL)

int
main(void)
{
    char *dir = g_dir_make_tmp("syncview-modlog-XXXXXX", NULL);
    g_assert_nonnull(dir);
    char *file, *err;

    /* Normale: azioni utente/errori sul file, nessun dettaglio debug, stderr vuoto. */
    run(dir, FALSE, &file, &err);
    g_assert_cmpint(strlen(err), ==, 0);
    g_assert_true(HAS(file, "INFO - [AZIONE UTENTE] Database marker creato - Versione 1, Path: "));
    g_assert_true(HAS(file, "INFO - [AZIONE UTENTE] Batch save marker - 1 marker salvati"));
    g_assert_true(HAS(file, "[AZIONE UTENTE] Migrazione marker JSON\xe2\x86\x92SQLite - 1 marker migrati, backup: m.json.backup"));
    g_assert_true(HAS(file, "ERROR - Errore batch save marker"));
    g_assert_true(HAS(file, "INFO - [AZIONE UTENTE] user_paths.json salvato - File: "));
    g_assert_true(HAS(file, "[AZIONE UTENTE] Percorso salvato in user_paths - Slot 0: ") && HAS(file, G_DIR_SEPARATOR_S "esiste_no.mp4"));
    g_assert_true(HAS(file, "[AZIONE UTENTE] Percorso non valido rimosso - Slot 0: ") && HAS(file, G_DIR_SEPARATOR_S "esiste_no.mp4 (file non trovato)"));
    g_assert_true(!HAS(file, "[SYNC]") && !HAS(file, "[MARKER]") && !HAS(file, "DEBUG"));
    g_free(file);
    g_free(err);

    /* Debug: in più tutti gli eventi tracciati dal piano, su file e stderr. */
    run(dir, TRUE, &file, &err);
    const char *both[] = { file, err };
    for (int i = 0; i < 2; i++) {
        const char *t = both[i];
        /* sync: input/output di calculate e sync_all */
        g_assert_true(HAS(t, "[SYNC] calculate_sync_position(source_pos=100ms, source=0, target=1) "
                      "offsets[source=0ms, target=500ms] -> 600ms"));
        g_assert_true(HAS(t, "[SYNC] sync_all_to_master(master=0, master_pos=2000ms, n_players=3)"));
        g_assert_true(HAS(t, "[SYNC]   player 1: seek(2500ms) + pause"));
        g_assert_true(HAS(t, "[SYNC]   player 0 (master): pause"));
        g_assert_true(HAS(t, "[SYNC]   player 2: non caricato, ignorato"));
        g_assert_true(HAS(t, "[SYNC] set_offset(video=1, offset=500ms)"));
        /* marker_db: query eseguite e righe coinvolte */
        g_assert_true(HAS(t, "[MARKER] SQL: BEGIN"));
        g_assert_true(HAS(t, "[MARKER] SQL: INSERT INTO markers"));
        g_assert_true(HAS(t, "[MARKER]   upsert id="));
        g_assert_true(HAS(t, "-> 1 riga/e"));
        g_assert_true(HAS(t, "[MARKER] SQL: SELECT id, timestamp, color, description, category, video_index, created_at FROM markers WHERE is_deleted = 0 ORDER BY timestamp"));
        g_assert_true(HAS(t, "[MARKER]   2 marker caricati"));
        g_assert_true(HAS(t, "[MARKER] SQL: UPDATE markers SET is_deleted = 1"));
        g_assert_true(HAS(t, "[MARKER]   1 riga/e modificate"));
        /* store in memoria */
        g_assert_true(HAS(t, "[MARKER] store: aggiunto id="));
        g_assert_true(HAS(t, "[MARKER] store: update id="));
        g_assert_true(HAS(t, "[MARKER] store: rimosso id="));
    }
    g_free(file);
    g_free(err);

    char *cmd = g_strdup_printf("rm -rf '%s'", dir);
    g_assert_cmpint(system(cmd), ==, 0);
    g_free(cmd);
    g_free(dir);
    return 0;
}
