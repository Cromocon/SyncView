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

    marker_db_free(NULL);

    /* Pulizia della directory temporanea. */
    char *cmd = g_strdup_printf("rm -rf '%s'", dir);
    assert(system(cmd) == 0);
    g_free(cmd);
    g_free(dir);
    return 0;
}
