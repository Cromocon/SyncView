#include "core/marker_db.h"

#include <assert.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <sqlite3.h>
#include <stdlib.h>
#include <string.h>

/* Esegue una query e ritorna la prima colonna della prima riga come stringa (g_free), o NULL. */
static char *
query_scalar(const char *path, const char *sql)
{
    sqlite3 *conn = NULL;
    sqlite3_stmt *stmt = NULL;
    char *result = NULL;

    assert(sqlite3_open(path, &conn) == SQLITE_OK);
    assert(sqlite3_prepare_v2(conn, sql, -1, &stmt, NULL) == SQLITE_OK);
    if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_text(stmt, 0)) {
        result = g_strdup((const char *)sqlite3_column_text(stmt, 0));
    }
    sqlite3_finalize(stmt);
    sqlite3_close(conn);
    return result;
}

static void
exec_direct(const char *path, const char *sql)
{
    sqlite3 *conn = NULL;
    assert(sqlite3_open(path, &conn) == SQLITE_OK);
    assert(sqlite3_exec(conn, sql, NULL, NULL, NULL) == SQLITE_OK);
    sqlite3_close(conn);
}

/* Colonne di `markers` via PRAGMA table_info: "name:type:notnull:dflt:pk" concatenate con ','. */
static char *
table_columns(const char *path, const char *table)
{
    sqlite3 *conn = NULL;
    sqlite3_stmt *stmt = NULL;
    GString *out = g_string_new(NULL);
    char *sql = g_strdup_printf("PRAGMA table_info(%s)", table);

    assert(sqlite3_open(path, &conn) == SQLITE_OK);
    assert(sqlite3_prepare_v2(conn, sql, -1, &stmt, NULL) == SQLITE_OK);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char *dflt = sqlite3_column_text(stmt, 4);
        g_string_append_printf(out, "%s%s:%s:%d:%s:%d", out->len ? "," : "",
                               sqlite3_column_text(stmt, 1), sqlite3_column_text(stmt, 2),
                               sqlite3_column_int(stmt, 3), dflt ? (const char *)dflt : "",
                               sqlite3_column_int(stmt, 5));
    }
    sqlite3_finalize(stmt);
    sqlite3_close(conn);
    g_free(sql);
    return g_string_free(out, FALSE);
}

static void
test_creates_schema(const char *dir)
{
    /* Directory intermedia inesistente: deve essere creata. */
    char *path = g_build_filename(dir, "sub", "dir", "markers.db", NULL);
    GError *error = NULL;

    MarkerDb *db = marker_db_open(path, &error);
    assert(db != NULL && error == NULL);
    assert(strcmp(marker_db_get_path(db), path) == 0);
    assert(g_file_test(path, G_FILE_TEST_IS_REGULAR));

    /* Tabelle: esattamente metadata e markers. */
    char *tables = query_scalar(path,
        "SELECT group_concat(name) FROM (SELECT name FROM sqlite_master "
        "WHERE type='table' AND name NOT LIKE 'sqlite_%' ORDER BY name)");
    assert(tables && strcmp(tables, "markers,metadata") == 0);
    g_free(tables);

    char *cols = table_columns(path, "metadata");
    assert(strcmp(cols, "key:TEXT:0::1,value:TEXT:1::0") == 0);
    g_free(cols);

    cols = table_columns(path, "markers");
    assert(strcmp(cols,
        "id:TEXT:0::1,timestamp:INTEGER:1::0,color:TEXT:1::0,description:TEXT:0:'':0,"
        "category:TEXT:0:'default':0,video_index:INTEGER:0::0,created_at:TEXT:1::0,"
        "updated_at:TEXT:1::0,is_deleted:INTEGER:0:0:0") == 0);
    g_free(cols);

    /* I 4 indici espliciti (gli autoindex di PRIMARY KEY/UNIQUE hanno nome sqlite_autoindex_*). */
    char *indexes = query_scalar(path,
        "SELECT group_concat(name || ':' || tbl_name) FROM (SELECT name, tbl_name FROM sqlite_master "
        "WHERE type='index' AND name NOT LIKE 'sqlite_%' ORDER BY name)");
    assert(indexes && strcmp(indexes,
        "idx_category:markers,idx_deleted:markers,idx_timestamp:markers,idx_video_index:markers") == 0);
    g_free(indexes);

    /* Vincolo UNIQUE(timestamp, video_index, created_at) presente come autoindex. */
    char *unique = query_scalar(path,
        "SELECT count(*) FROM pragma_index_list('markers') WHERE origin='u'");
    assert(unique && strcmp(unique, "1") == 0);
    g_free(unique);

    /* Metadata iniziali. */
    char *version = query_scalar(path, "SELECT value FROM metadata WHERE key='db_version'");
    assert(version && strcmp(version, "1") == 0);
    g_free(version);
    char *created = query_scalar(path, "SELECT value FROM metadata WHERE key='created_at'");
    assert(created && strlen(created) >= 11 && created[10] == 'T');
    g_free(created);

    marker_db_free(db);
    g_free(path);
}

static void
test_reopen_is_idempotent(const char *dir)
{
    char *path = g_build_filename(dir, "reopen.db", NULL);

    MarkerDb *db = marker_db_open(path, NULL);
    assert(db != NULL);
    marker_db_free(db);

    char *created_before = query_scalar(path, "SELECT value FROM metadata WHERE key='created_at'");
    exec_direct(path, "INSERT INTO markers (id, timestamp, color, created_at, updated_at) "
                      "VALUES ('x', 1, '#fff', 'c', 'u')");

    /* Riapertura: nessun reset di schema, dati e created_at invariati. */
    db = marker_db_open(path, NULL);
    assert(db != NULL);
    marker_db_free(db);

    char *count = query_scalar(path, "SELECT count(*) FROM markers");
    assert(strcmp(count, "1") == 0);
    char *created_after = query_scalar(path, "SELECT value FROM metadata WHERE key='created_at'");
    assert(strcmp(created_before, created_after) == 0);
    char *meta_rows = query_scalar(path, "SELECT count(*) FROM metadata");
    assert(strcmp(meta_rows, "2") == 0);

    g_free(created_before);
    g_free(created_after);
    g_free(count);
    g_free(meta_rows);
    g_free(path);
}

static void
test_migrates_older_version(const char *dir)
{
    char *path = g_build_filename(dir, "old.db", NULL);

    MarkerDb *db = marker_db_open(path, NULL);
    marker_db_free(db);
    exec_direct(path, "UPDATE metadata SET value='0' WHERE key='db_version'");

    db = marker_db_open(path, NULL);
    assert(db != NULL);
    marker_db_free(db);

    char *version = query_scalar(path, "SELECT value FROM metadata WHERE key='db_version'");
    assert(strcmp(version, "1") == 0);

    g_free(version);
    g_free(path);
}

static void
test_open_failure(const char *dir)
{
    /* Il "file" è in realtà una directory: SQLite non può aprirlo come database. */
    char *path = g_build_filename(dir, "isdir.db", NULL);
    assert(g_mkdir(path, 0755) == 0);

    GError *error = NULL;
    MarkerDb *db = marker_db_open(path, &error);
    assert(db == NULL);
    assert(error != NULL && error->domain == MARKER_DB_ERROR);

    g_error_free(error);
    g_free(path);
}

static void
assert_markers_equal(const Marker *a, const Marker *b)
{
    assert(strcmp(a->id, b->id) == 0);
    assert(a->timestamp_ms == b->timestamp_ms);
    assert(strcmp(a->color, b->color) == 0);
    assert(strcmp(a->description, b->description) == 0);
    assert(strcmp(a->category, b->category) == 0);
    assert(a->video_index == b->video_index);
    assert(strcmp(a->created_at, b->created_at) == 0);
}

static void
test_save_load_roundtrip(const char *dir)
{
    char *path = g_build_filename(dir, "roundtrip.db", NULL);
    MarkerDb *db = marker_db_open(path, NULL);
    assert(db != NULL);

    /* Store vuoto: save ok, load ritorna store vuoto. */
    MarkerStore *empty = marker_store_new();
    assert(marker_db_save_batch(db, empty, NULL));
    marker_store_free(empty);
    MarkerStore *loaded = marker_db_load_all(db, FALSE, NULL);
    assert(loaded != NULL && marker_store_count(loaded) == 0);
    marker_store_free(loaded);

    /* 50 marker: timestamp sparsi (con duplicati), video_index misti, stringhe non banali. */
    MarkerStore *store = marker_store_new();
    for (int i = 0; i < 50; i++) {
        int video = (i % 5) - 1;  /* -1 (globale), 0, 1, 2, 3 */
        char *desc = i % 3 == 0 ? g_strdup_printf("nota '%d' \"quote\" è perché", i) : NULL;
        marker_store_add(store, (int64_t)((i * 7919) % 20000), "#3498db", desc,
                         i % 2 ? "action" : NULL, video);
        g_free(desc);
    }

    assert(marker_db_save_batch(db, store, NULL));

    /* video_index globale salvato come NULL. */
    char *nulls = query_scalar(path, "SELECT count(*) FROM markers WHERE video_index IS NULL");
    assert(strcmp(nulls, "10") == 0);
    g_free(nulls);

    loaded = marker_db_load_all(db, FALSE, NULL);
    assert(loaded != NULL);
    assert(marker_store_count(loaded) == marker_store_count(store));

    /* Ordinati per timestamp; confronto per id (l'ordine a pari timestamp non è garantito). */
    for (size_t i = 0; i < marker_store_count(loaded); i++) {
        const Marker *l = marker_store_get(loaded, i);
        if (i > 0) {
            assert(marker_store_get(loaded, i - 1)->timestamp_ms <= l->timestamp_ms);
        }
        const Marker *orig = marker_store_find_by_id(store, l->id);
        assert(orig != NULL);
        assert_markers_equal(orig, l);
    }
    marker_store_free(loaded);

    /* Upsert: modifico alcuni marker e risalvo -> stesso numero di righe, campi aggiornati. */
    const Marker *first = marker_store_get(store, 0);
    char *first_id = g_strdup(first->id);
    char *first_created = g_strdup(first->created_at);
    MarkerUpdate up = { .fields = MARKER_FIELD_COLOR | MARKER_FIELD_DESCRIPTION
                                  | MARKER_FIELD_TIMESTAMP | MARKER_FIELD_VIDEO_INDEX,
                        .color = "#e74c3c", .description = "modificato", .timestamp_ms = 99999,
                        .video_index = 3 };
    assert(marker_store_update(store, first_id, &up) != NULL);
    assert(marker_db_save_batch(db, store, NULL));

    char *count = query_scalar(path, "SELECT count(*) FROM markers");
    assert(strcmp(count, "50") == 0);
    g_free(count);

    loaded = marker_db_load_all(db, FALSE, NULL);
    const Marker *l = marker_store_find_by_id(loaded, first_id);
    assert(l != NULL);
    assert(l->timestamp_ms == 99999 && l->video_index == 3);
    assert(strcmp(l->color, "#e74c3c") == 0 && strcmp(l->description, "modificato") == 0);
    assert(strcmp(l->created_at, first_created) == 0);  /* created_at non viene riscritto */
    marker_store_free(loaded);

    /* updated_at valorizzato ISO8601 su tutte le righe. */
    char *bad_updated = query_scalar(path,
        "SELECT count(*) FROM markers WHERE substr(updated_at, 11, 1) != 'T'");
    assert(strcmp(bad_updated, "0") == 0);
    g_free(bad_updated);

    g_free(first_id);
    g_free(first_created);
    marker_store_free(store);
    marker_db_free(db);
    g_free(path);
}

static void
test_load_excludes_deleted(const char *dir)
{
    char *path = g_build_filename(dir, "deleted.db", NULL);
    MarkerDb *db = marker_db_open(path, NULL);

    MarkerStore *store = marker_store_new();
    const Marker *keep = marker_store_add(store, 1000, "#000000", NULL, NULL, 0);
    const Marker *gone = marker_store_add(store, 2000, "#000000", NULL, NULL, 0);
    char *keep_id = g_strdup(keep->id);
    char *gone_id = g_strdup(gone->id);
    assert(marker_db_save_batch(db, store, NULL));
    marker_store_free(store);

    char *sql = g_strdup_printf("UPDATE markers SET is_deleted = 1 WHERE id = '%s'", gone_id);
    exec_direct(path, sql);
    g_free(sql);

    MarkerStore *loaded = marker_db_load_all(db, FALSE, NULL);
    assert(marker_store_count(loaded) == 1);
    assert(marker_store_find_by_id(loaded, keep_id) != NULL);
    assert(marker_store_find_by_id(loaded, gone_id) == NULL);
    marker_store_free(loaded);

    loaded = marker_db_load_all(db, TRUE, NULL);
    assert(marker_store_count(loaded) == 2);
    marker_store_free(loaded);

    g_free(keep_id);
    g_free(gone_id);
    marker_db_free(db);
    g_free(path);
}

static void
test_save_batch_rolls_back_on_error(const char *dir)
{
    char *path = g_build_filename(dir, "rollback.db", NULL);
    MarkerDb *db = marker_db_open(path, NULL);

    /* Due marker con id diversi ma stessa terna (timestamp, video_index, created_at):
     * violano UNIQUE, quindi il batch deve fallire per intero. */
    MarkerStore *store = marker_store_new();
    marker_store_add(store, 100, "#000000", NULL, NULL, 0);
    Marker *a = marker_new(500, "#000000", NULL, NULL, 1);
    Marker *b = marker_new(500, "#000000", NULL, NULL, 1);
    g_free(b->id);
    b->id = g_strdup("altro-id");
    g_free(b->created_at);
    b->created_at = g_strdup(a->created_at);
    marker_store_add_marker(store, a);
    marker_store_add_marker(store, b);

    GError *error = NULL;
    assert(!marker_db_save_batch(db, store, &error));
    assert(error != NULL && error->domain == MARKER_DB_ERROR && error->code == MARKER_DB_ERROR_SQL);
    g_error_free(error);

    char *count = query_scalar(path, "SELECT count(*) FROM markers");
    assert(strcmp(count, "0") == 0);  /* anche il marker valido (ts=100) è stato annullato */
    g_free(count);

    marker_store_free(store);
    marker_db_free(db);
    g_free(path);
}

int
main(void)
{
    GError *error = NULL;
    char *dir = g_dir_make_tmp("syncview-marker-db-XXXXXX", &error);
    assert(dir != NULL);

    test_creates_schema(dir);
    test_reopen_is_idempotent(dir);
    test_migrates_older_version(dir);
    test_open_failure(dir);
    test_save_load_roundtrip(dir);
    test_load_excludes_deleted(dir);
    test_save_batch_rolls_back_on_error(dir);

    marker_db_free(NULL);

    /* Pulizia della directory temporanea. */
    char *cmd = g_strdup_printf("rm -rf '%s'", dir);
    assert(system(cmd) == 0);
    g_free(cmd);
    g_free(dir);
    return 0;
}
