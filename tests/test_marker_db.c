#include "core/marker_db.h"

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

    g_assert_true(sqlite3_open(path, &conn) == SQLITE_OK);
    g_assert_true(sqlite3_prepare_v2(conn, sql, -1, &stmt, NULL) == SQLITE_OK);
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
    g_assert_true(sqlite3_open(path, &conn) == SQLITE_OK);
    g_assert_true(sqlite3_exec(conn, sql, NULL, NULL, NULL) == SQLITE_OK);
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

    g_assert_true(sqlite3_open(path, &conn) == SQLITE_OK);
    g_assert_true(sqlite3_prepare_v2(conn, sql, -1, &stmt, NULL) == SQLITE_OK);
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
    g_assert_true(db != NULL && error == NULL);
    g_assert_cmpstr(marker_db_get_path(db), ==, path);
    g_assert_true(g_file_test(path, G_FILE_TEST_IS_REGULAR));

    /* Tabelle: esattamente metadata e markers. */
    char *tables = query_scalar(path,
        "SELECT group_concat(name) FROM (SELECT name FROM sqlite_master "
        "WHERE type='table' AND name NOT LIKE 'sqlite_%' ORDER BY name)");
    g_assert_true(tables && strcmp(tables, "markers,metadata") == 0);
    g_free(tables);

    char *cols = table_columns(path, "metadata");
    g_assert_cmpstr(cols, ==, "key:TEXT:0::1,value:TEXT:1::0");
    g_free(cols);

    cols = table_columns(path, "markers");
    g_assert_true(strcmp(cols,
        "id:TEXT:0::1,timestamp:INTEGER:1::0,color:TEXT:1::0,description:TEXT:0:'':0,"
        "category:TEXT:0:'default':0,video_index:INTEGER:0::0,created_at:TEXT:1::0,"
        "updated_at:TEXT:1::0,is_deleted:INTEGER:0:0:0") == 0);
    g_free(cols);

    /* I 4 indici espliciti (gli autoindex di PRIMARY KEY/UNIQUE hanno nome sqlite_autoindex_*). */
    char *indexes = query_scalar(path,
        "SELECT group_concat(name || ':' || tbl_name) FROM (SELECT name, tbl_name FROM sqlite_master "
        "WHERE type='index' AND name NOT LIKE 'sqlite_%' ORDER BY name)");
    g_assert_true(indexes && strcmp(indexes,
        "idx_category:markers,idx_deleted:markers,idx_timestamp:markers,idx_video_index:markers") == 0);
    g_free(indexes);

    /* Vincolo UNIQUE(timestamp, video_index, created_at) presente come autoindex. */
    char *unique = query_scalar(path,
        "SELECT count(*) FROM pragma_index_list('markers') WHERE origin='u'");
    g_assert_true(unique && strcmp(unique, "1") == 0);
    g_free(unique);

    /* Metadata iniziali. */
    char *version = query_scalar(path, "SELECT value FROM metadata WHERE key='db_version'");
    g_assert_true(version && strcmp(version, "1") == 0);
    g_free(version);
    char *created = query_scalar(path, "SELECT value FROM metadata WHERE key='created_at'");
    g_assert_true(created && strlen(created) >= 11 && created[10] == 'T');
    g_free(created);

    marker_db_free(db);
    g_free(path);
}

static void
test_reopen_is_idempotent(const char *dir)
{
    char *path = g_build_filename(dir, "reopen.db", NULL);

    MarkerDb *db = marker_db_open(path, NULL);
    g_assert_nonnull(db);
    marker_db_free(db);

    char *created_before = query_scalar(path, "SELECT value FROM metadata WHERE key='created_at'");
    exec_direct(path, "INSERT INTO markers (id, timestamp, color, created_at, updated_at) "
                      "VALUES ('x', 1, '#fff', 'c', 'u')");

    /* Riapertura: nessun reset di schema, dati e created_at invariati. */
    db = marker_db_open(path, NULL);
    g_assert_nonnull(db);
    marker_db_free(db);

    char *count = query_scalar(path, "SELECT count(*) FROM markers");
    g_assert_cmpstr(count, ==, "1");
    char *created_after = query_scalar(path, "SELECT value FROM metadata WHERE key='created_at'");
    g_assert_cmpstr(created_before, ==, created_after);
    char *meta_rows = query_scalar(path, "SELECT count(*) FROM metadata");
    g_assert_cmpstr(meta_rows, ==, "2");

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
    g_assert_nonnull(db);
    marker_db_free(db);

    char *version = query_scalar(path, "SELECT value FROM metadata WHERE key='db_version'");
    g_assert_cmpstr(version, ==, "1");

    g_free(version);
    g_free(path);
}

static void
test_open_failure(const char *dir)
{
    /* Il "file" è in realtà una directory: SQLite non può aprirlo come database. */
    char *path = g_build_filename(dir, "isdir.db", NULL);
    g_assert_cmpint(g_mkdir(path, 0755), ==, 0);

    GError *error = NULL;
    MarkerDb *db = marker_db_open(path, &error);
    g_assert_null(db);
    g_assert_true(error != NULL && error->domain == MARKER_DB_ERROR);

    g_error_free(error);
    g_free(path);
}

static void
assert_markers_equal(const Marker *a, const Marker *b)
{
    g_assert_cmpstr(a->id, ==, b->id);
    g_assert_true(a->timestamp_ms == b->timestamp_ms);
    g_assert_cmpstr(a->color, ==, b->color);
    g_assert_cmpstr(a->description, ==, b->description);
    g_assert_cmpstr(a->category, ==, b->category);
    g_assert_true(a->video_index == b->video_index);
    g_assert_cmpstr(a->created_at, ==, b->created_at);
}

static void
test_save_load_roundtrip(const char *dir)
{
    char *path = g_build_filename(dir, "roundtrip.db", NULL);
    MarkerDb *db = marker_db_open(path, NULL);
    g_assert_nonnull(db);

    /* Store vuoto: save ok, load ritorna store vuoto. */
    MarkerStore *empty = marker_store_new();
    g_assert_true(marker_db_save_batch(db, empty, NULL));
    marker_store_free(empty);
    MarkerStore *loaded = marker_db_load_all(db, FALSE, NULL);
    g_assert_true(loaded != NULL && marker_store_count(loaded) == 0);
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

    g_assert_true(marker_db_save_batch(db, store, NULL));

    /* video_index globale salvato come NULL. */
    char *nulls = query_scalar(path, "SELECT count(*) FROM markers WHERE video_index IS NULL");
    g_assert_cmpstr(nulls, ==, "10");
    g_free(nulls);

    loaded = marker_db_load_all(db, FALSE, NULL);
    g_assert_nonnull(loaded);
    g_assert_true(marker_store_count(loaded) == marker_store_count(store));

    /* Ordinati per timestamp; confronto per id (l'ordine a pari timestamp non è garantito). */
    for (size_t i = 0; i < marker_store_count(loaded); i++) {
        const Marker *l = marker_store_get(loaded, i);
        if (i > 0) {
            g_assert_true(marker_store_get(loaded, i - 1)->timestamp_ms <= l->timestamp_ms);
        }
        const Marker *orig = marker_store_find_by_id(store, l->id);
        g_assert_nonnull(orig);
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
    g_assert_nonnull(marker_store_update(store, first_id, &up));
    g_assert_true(marker_db_save_batch(db, store, NULL));

    char *count = query_scalar(path, "SELECT count(*) FROM markers");
    g_assert_cmpstr(count, ==, "50");
    g_free(count);

    loaded = marker_db_load_all(db, FALSE, NULL);
    const Marker *l = marker_store_find_by_id(loaded, first_id);
    g_assert_nonnull(l);
    g_assert_true(l->timestamp_ms == 99999 && l->video_index == 3);
    g_assert_true(strcmp(l->color, "#e74c3c") == 0 && strcmp(l->description, "modificato") == 0);
    g_assert_cmpstr(l->created_at, ==, first_created);  /* created_at non viene riscritto */
    marker_store_free(loaded);

    /* updated_at valorizzato ISO8601 su tutte le righe. */
    char *bad_updated = query_scalar(path,
        "SELECT count(*) FROM markers WHERE substr(updated_at, 11, 1) != 'T'");
    g_assert_cmpstr(bad_updated, ==, "0");
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
    g_assert_true(marker_db_save_batch(db, store, NULL));
    marker_store_free(store);

    char *sql = g_strdup_printf("UPDATE markers SET is_deleted = 1 WHERE id = '%s'", gone_id);
    exec_direct(path, sql);
    g_free(sql);

    MarkerStore *loaded = marker_db_load_all(db, FALSE, NULL);
    g_assert_cmpint(marker_store_count(loaded), ==, 1);
    g_assert_nonnull(marker_store_find_by_id(loaded, keep_id));
    g_assert_null(marker_store_find_by_id(loaded, gone_id));
    marker_store_free(loaded);

    loaded = marker_db_load_all(db, TRUE, NULL);
    g_assert_cmpint(marker_store_count(loaded), ==, 2);
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
    g_assert_false(marker_db_save_batch(db, store, &error));
    g_assert_true(error != NULL && error->domain == MARKER_DB_ERROR && error->code == MARKER_DB_ERROR_SQL);
    g_error_free(error);

    char *count = query_scalar(path, "SELECT count(*) FROM markers");
    g_assert_cmpstr(count, ==, "0");  /* anche il marker valido (ts=100) è stato annullato */
    g_free(count);

    marker_store_free(store);
    marker_db_free(db);
    g_free(path);
}

static void
test_soft_delete(const char *dir)
{
    char *path = g_build_filename(dir, "softdelete.db", NULL);
    MarkerDb *db = marker_db_open(path, NULL);

    MarkerStore *store = marker_store_new();
    char *ids[3];
    for (int i = 0; i < 3; i++) {
        ids[i] = g_strdup(marker_store_add(store, 1000 * (i + 1), "#000000", "d", NULL, i)->id);
    }
    g_assert_true(marker_db_save_batch(db, store, NULL));
    marker_store_free(store);

    char *sql = g_strdup_printf("SELECT updated_at FROM markers WHERE id = '%s'", ids[1]);
    char *updated_before = query_scalar(path, sql);
    g_free(sql);

    g_assert_true(marker_db_delete(db, ids[1], NULL));

    /* Riga ancora presente con is_deleted=1 e dati intatti (soft, non hard delete). */
    sql = g_strdup_printf("SELECT is_deleted || '|' || timestamp || '|' || description FROM markers "
                          "WHERE id = '%s'", ids[1]);
    char *row = query_scalar(path, sql);
    g_free(sql);
    g_assert_true(row && strcmp(row, "1|2000|d") == 0);
    g_free(row);

    /* updated_at aggiornato, ISO8601. */
    sql = g_strdup_printf("SELECT updated_at FROM markers WHERE id = '%s'", ids[1]);
    char *updated_after = query_scalar(path, sql);
    g_free(sql);
    g_assert_true(strcmp(updated_before, updated_after) != 0 && updated_after[10] == 'T');
    g_free(updated_before);
    g_free(updated_after);

    /* Gli altri marker non sono toccati. */
    char *others = query_scalar(path, "SELECT count(*) FROM markers WHERE is_deleted = 0");
    g_assert_cmpstr(others, ==, "2");
    g_free(others);

    /* Assente da load_all, presente con include_deleted. */
    MarkerStore *loaded = marker_db_load_all(db, FALSE, NULL);
    g_assert_cmpint(marker_store_count(loaded), ==, 2);
    g_assert_null(marker_store_find_by_id(loaded, ids[1]));
    marker_store_free(loaded);
    loaded = marker_db_load_all(db, TRUE, NULL);
    g_assert_cmpint(marker_store_count(loaded), ==, 3);
    marker_store_free(loaded);

    /* Idempotente; id inesistente non è un errore e non modifica nulla. */
    g_assert_true(marker_db_delete(db, ids[1], NULL));
    g_assert_true(marker_db_delete(db, "inesistente", NULL));
    char *deleted = query_scalar(path, "SELECT count(*) FROM markers WHERE is_deleted = 1");
    g_assert_cmpstr(deleted, ==, "1");
    g_free(deleted);

    for (int i = 0; i < 3; i++) {
        g_free(ids[i]);
    }
    marker_db_free(db);
    g_free(path);
}

/* --- Migrazione JSON legacy --- */

static void
write_file(const char *path, const char *content)
{
    g_assert_true(g_file_set_contents(path, content, -1, NULL));
}

static char *
read_file(const char *path)
{
    char *content = NULL;
    g_assert_true(g_file_get_contents(path, &content, NULL, NULL));
    return content;
}

static const char *LEGACY_JSON =
    "{\n"
    "  \"version\": \"3.0\",\n"
    "  \"created_at\": \"2024-01-01T10:00:00\",\n"
    "  \"markers\": [\n"
    "    {\"timestamp\": 5000, \"color\": \"#e74c3c\", \"description\": \"gol \\u00e8 fatto\","
    " \"category\": \"event\", \"video_index\": 2, \"created_at\": \"2024-01-01T10:00:01.000001\","
    " \"id\": \"marker_5000_1.5\", \"label\": \"vecchia etichetta\"},\n"
    "    {\"timestamp\": 1000, \"color\": \"#3498db\", \"description\": \"\", \"category\": \"default\","
    " \"video_index\": null, \"created_at\": \"2024-01-01T10:00:02.000002\", \"id\": \"marker_1000_2.5\"},\n"
    "    {\"timestamp\": 3000, \"color\": \"#2ecc71\"},\n"
    "    {\"timestamp\": 2000.0, \"color\": \"#9b59b6\", \"id\": null, \"video_index\": 0}\n"
    "  ]\n"
    "}\n";

static void
test_migrate_json(const char *dir)
{
    char *json = g_build_filename(dir, "project.json", NULL);
    char *backup = g_build_filename(dir, "project.json.backup", NULL);
    char *dbpath = g_build_filename(dir, "project.markers.db", NULL);
    write_file(json, LEGACY_JSON);

    MarkerDb *db = marker_db_open(dbpath, NULL);
    int migrated = -1;
    GError *error = NULL;
    g_assert_true(marker_db_migrate_from_json(db, json, &migrated, &error));
    g_assert_true(error == NULL && migrated == 4);

    /* JSON rinominato in .json.backup, contenuto identico all'originale. */
    g_assert_false(g_file_test(json, G_FILE_TEST_EXISTS));
    g_assert_true(g_file_test(backup, G_FILE_TEST_IS_REGULAR));
    char *backup_content = read_file(backup);
    g_assert_cmpstr(backup_content, ==, LEGACY_JSON);
    g_free(backup_content);

    /* Contenuto migrato, ordinato per timestamp. */
    MarkerStore *store = marker_db_load_all(db, FALSE, NULL);
    g_assert_cmpint(marker_store_count(store), ==, 4);
    g_assert_cmpint(marker_store_get(store, 0)->timestamp_ms, ==, 1000);
    g_assert_cmpint(marker_store_get(store, 1)->timestamp_ms, ==, 2000);
    g_assert_cmpint(marker_store_get(store, 2)->timestamp_ms, ==, 3000);
    g_assert_cmpint(marker_store_get(store, 3)->timestamp_ms, ==, 5000);

    const Marker *m = marker_store_find_by_id(store, "marker_5000_1.5");  /* id preservato */
    g_assert_nonnull(m);
    g_assert_true(m->timestamp_ms == 5000 && m->video_index == 2);
    g_assert_true(strcmp(m->color, "#e74c3c") == 0 && strcmp(m->category, "event") == 0);
    g_assert_cmpstr(m->description, ==, "gol \xc3\xa8 fatto");
    g_assert_cmpstr(m->created_at, ==, "2024-01-01T10:00:01.000001");

    m = marker_store_find_by_id(store, "marker_1000_2.5");
    g_assert_true(m != NULL && m->video_index == SYNCVIEW_MARKER_VIDEO_INDEX_ALL);

    /* Campi mancanti: default e id/created_at generati. */
    m = marker_store_get(store, 2);
    g_assert_true(strcmp(m->color, "#2ecc71") == 0 && strcmp(m->description, "") == 0);
    g_assert_true(strcmp(m->category, "default") == 0 && m->video_index == SYNCVIEW_MARKER_VIDEO_INDEX_ALL);
    g_assert_cmpint(strncmp(m->id, "marker_3000_", strlen("marker_3000_")), ==, 0);
    g_assert_true(m->created_at[4] == '-' && m->created_at[10] == 'T');

    /* Timestamp float (2000.0) accettato, id null generato, video_index 0. */
    m = marker_store_get(store, 1);
    g_assert_true(strncmp(m->id, "marker_2000_", strlen("marker_2000_")) == 0 && m->video_index == 0);
    marker_store_free(store);

    /* Un secondo backup sostituisce il precedente. */
    write_file(json, "{\"markers\": [{\"timestamp\": 7, \"color\": \"#fff\"}]}");
    g_assert_true(marker_db_migrate_from_json(db, json, &migrated, NULL) && migrated == 1);
    backup_content = read_file(backup);
    g_assert_nonnull(strstr(backup_content, "\"timestamp\": 7"));
    g_free(backup_content);

    marker_db_free(db);
    g_free(json);
    g_free(backup);
    g_free(dbpath);
}

static void
test_migrate_json_no_op_cases(const char *dir)
{
    char *dbpath = g_build_filename(dir, "noop.markers.db", NULL);
    char *json = g_build_filename(dir, "noop.json", NULL);
    char *backup = g_build_filename(dir, "noop.json.backup", NULL);
    MarkerDb *db = marker_db_open(dbpath, NULL);
    int migrated = -1;

    /* File assente: successo, nulla da fare. */
    g_assert_true(marker_db_migrate_from_json(db, json, &migrated, NULL) && migrated == 0);

    /* Lista vuota (o chiave 'markers' assente): nessun salvataggio e nessun backup. */
    write_file(json, "{\"version\": \"3.0\", \"markers\": []}");
    migrated = -1;
    g_assert_true(marker_db_migrate_from_json(db, json, &migrated, NULL) && migrated == 0);
    g_assert_true(g_file_test(json, G_FILE_TEST_EXISTS) && !g_file_test(backup, G_FILE_TEST_EXISTS));
    write_file(json, "{}");
    g_assert_true(marker_db_migrate_from_json(db, json, &migrated, NULL) && migrated == 0);
    g_assert_true(g_file_test(json, G_FILE_TEST_EXISTS) && !g_file_test(backup, G_FILE_TEST_EXISTS));

    marker_db_free(db);
    g_free(dbpath);
    g_free(json);
    g_free(backup);
}

static void
test_migrate_json_errors(const char *dir)
{
    char *dbpath = g_build_filename(dir, "err.markers.db", NULL);
    char *json = g_build_filename(dir, "err.json", NULL);
    char *backup = g_build_filename(dir, "err.json.backup", NULL);
    MarkerDb *db = marker_db_open(dbpath, NULL);

    const char *bad_inputs[] = {
        "{ non e' json",
        "[1, 2, 3]",
        "{\"markers\": {\"timestamp\": 1}}",
        /* secondo marker con chiave sconosciuta: fallisce tutto, anche il primo valido */
        "{\"markers\": [{\"timestamp\": 1, \"color\": \"#fff\"}, {\"timestamp\": 2, \"color\": \"#fff\", \"extra\": 1}]}",
        "{\"markers\": [{\"color\": \"#fff\"}]}",                  /* timestamp mancante */
        "{\"markers\": [{\"timestamp\": 1}]}",                      /* color mancante */
        "{\"markers\": [{\"timestamp\": \"uno\", \"color\": \"#fff\"}]}",
        "{\"markers\": [{\"timestamp\": 1, \"color\": 5}]}",
        "{\"markers\": [42]}",
    };

    for (size_t i = 0; i < G_N_ELEMENTS(bad_inputs); i++) {
        write_file(json, bad_inputs[i]);
        GError *error = NULL;
        int migrated = -1;
        g_assert_false(marker_db_migrate_from_json(db, json, &migrated, &error));
        g_assert_true(error != NULL && error->domain == MARKER_DB_ERROR && error->code == MARKER_DB_ERROR_JSON);
        g_assert_cmpint(migrated, ==, 0);
        g_error_free(error);

        /* JSON intatto, nessun backup, nulla salvato nel DB. */
        char *content = read_file(json);
        g_assert_cmpstr(content, ==, bad_inputs[i]);
        g_free(content);
        g_assert_false(g_file_test(backup, G_FILE_TEST_EXISTS));
        char *count = query_scalar(dbpath, "SELECT count(*) FROM markers");
        g_assert_cmpstr(count, ==, "0");
        g_free(count);
    }

    marker_db_free(db);
    g_free(dbpath);
    g_free(json);
    g_free(backup);
}

static void
test_open_migrating(const char *dir)
{
    char *dbpath = g_build_filename(dir, "om.markers.db", NULL);
    char *json = g_build_filename(dir, "om.json", NULL);
    char *backup = g_build_filename(dir, "om.json.backup", NULL);
    write_file(json, "{\"markers\": [{\"timestamp\": 10, \"color\": \"#fff\"}]}");

    /* DB nuovo + JSON presente: migrazione automatica. */
    GError *migration_error = NULL;
    MarkerDb *db = marker_db_open_migrating(dbpath, json, &migration_error, NULL);
    g_assert_true(db != NULL && migration_error == NULL);
    g_assert_true(!g_file_test(json, G_FILE_TEST_EXISTS) && g_file_test(backup, G_FILE_TEST_EXISTS));
    MarkerStore *store = marker_db_load_all(db, FALSE, NULL);
    g_assert_cmpint(marker_store_count(store), ==, 1);
    marker_store_free(store);
    marker_db_free(db);

    /* DB già esistente: un nuovo JSON NON viene migrato. */
    write_file(json, "{\"markers\": [{\"timestamp\": 20, \"color\": \"#fff\"}]}");
    db = marker_db_open_migrating(dbpath, json, NULL, NULL);
    g_assert_nonnull(db);
    g_assert_true(g_file_test(json, G_FILE_TEST_EXISTS));
    store = marker_db_load_all(db, FALSE, NULL);
    g_assert_cmpint(marker_store_count(store), ==, 1);
    marker_store_free(store);
    marker_db_free(db);

    /* JSON non valido su DB nuovo: il DB si apre comunque, l'errore è riportato, JSON intatto. */
    char *dbpath2 = g_build_filename(dir, "om2.markers.db", NULL);
    char *json2 = g_build_filename(dir, "om2.json", NULL);
    write_file(json2, "garbage");
    db = marker_db_open_migrating(dbpath2, json2, &migration_error, NULL);
    g_assert_true(db != NULL && migration_error != NULL);
    g_assert_true(migration_error->code == MARKER_DB_ERROR_JSON);
    g_error_free(migration_error);
    g_assert_true(g_file_test(json2, G_FILE_TEST_EXISTS));
    marker_db_free(db);

    /* legacy_json_path NULL: nessun tentativo. */
    char *dbpath3 = g_build_filename(dir, "om3.markers.db", NULL);
    db = marker_db_open_migrating(dbpath3, NULL, NULL, NULL);
    g_assert_nonnull(db);
    marker_db_free(db);

    g_free(dbpath);
    g_free(json);
    g_free(backup);
    g_free(dbpath2);
    g_free(json2);
    g_free(dbpath3);
}

/* db_version NULL (DB legacy con metadata senza NOT NULL): niente crash, trattato come versione 0. */
static void
test_null_db_version(const char *dir)
{
    char *path = g_build_filename(dir, "nullversion.db", NULL);

    exec_direct(path, "CREATE TABLE metadata (key TEXT PRIMARY KEY, value TEXT);"
                      "INSERT INTO metadata (key, value) VALUES ('db_version', NULL);");

    MarkerDb *db = marker_db_open(path, NULL);
    g_assert_nonnull(db);
    marker_db_free(db);

    char *version = query_scalar(path, "SELECT value FROM metadata WHERE key='db_version'");
    g_assert_true(version != NULL && strcmp(version, "1") == 0);

    g_free(version);
    g_free(path);
}

/* Ri-salvare un marker eliminato (undo) lo rende di nuovo visibile. */
static void
test_resave_restores_deleted(const char *dir)
{
    char *path = g_build_filename(dir, "resave.db", NULL);
    MarkerDb *db = marker_db_open(path, NULL);

    MarkerStore *store = marker_store_new();
    char *id = g_strdup(marker_store_add(store, 1000, "#000000", "d", NULL, 0)->id);
    g_assert_true(marker_db_save_batch(db, store, NULL));
    g_assert_true(marker_db_delete(db, id, NULL));

    MarkerStore *loaded = marker_db_load_all(db, FALSE, NULL);
    g_assert_cmpint(marker_store_count(loaded), ==, 0);
    marker_store_free(loaded);

    g_assert_true(marker_db_save_batch(db, store, NULL));
    loaded = marker_db_load_all(db, FALSE, NULL);
    g_assert_true(marker_store_count(loaded) == 1 && marker_store_find_by_id(loaded, id) != NULL);
    marker_store_free(loaded);

    marker_store_free(store);
    g_free(id);
    marker_db_free(db);
    g_free(path);
}

/* timestamp double non finito / fuori da int64: errore, non UB. */
static void
test_migrate_json_out_of_range_timestamp(const char *dir)
{
    char *json = g_build_filename(dir, "range.json", NULL);
    char *dbpath = g_build_filename(dir, "range.db", NULL);
    MarkerDb *db = marker_db_open(dbpath, NULL);
    const char *bad[] = {
        "{\"markers\": [{\"timestamp\": 1e300, \"color\": \"#fff\"}]}",
        "{\"markers\": [{\"timestamp\": -1e300, \"color\": \"#fff\"}]}",
        "{\"markers\": [{\"timestamp\": 9.3e18, \"color\": \"#fff\"}]}",
        "{\"markers\": [{\"timestamp\": 1, \"color\": \"#fff\", \"video_index\": 1e40}]}",
    };

    for (size_t i = 0; i < G_N_ELEMENTS(bad); i++) {
        GError *error = NULL;
        int migrated = -1;

        write_file(json, bad[i]);
        g_assert_false(marker_db_migrate_from_json(db, json, &migrated, &error));
        g_assert_true(error != NULL && error->code == MARKER_DB_ERROR_JSON);
        g_assert_true(g_file_test(json, G_FILE_TEST_EXISTS));
        g_error_free(error);
    }

    marker_db_free(db);
    g_free(json);
    g_free(dbpath);
}

/* Marker che condividono (timestamp, video_index, created_at): i duplicati sono scartati, il resto migra. */
static void
test_migrate_json_duplicates(const char *dir)
{
    char *json = g_build_filename(dir, "dup.json", NULL);
    char *backup = g_build_filename(dir, "dup.json.backup", NULL);
    char *dbpath = g_build_filename(dir, "dup.markers.db", NULL);
    write_file(json,
        "{\"markers\": ["
        "{\"timestamp\": 1000, \"color\": \"#111111\", \"video_index\": 1, \"created_at\": \"2024-01-01T00:00:00\", \"id\": \"a\"},"
        "{\"timestamp\": 1000, \"color\": \"#222222\", \"video_index\": 1, \"created_at\": \"2024-01-01T00:00:00\", \"id\": \"b\"},"
        "{\"timestamp\": 2000, \"color\": \"#333333\", \"video_index\": 1, \"created_at\": \"2024-01-01T00:00:00\", \"id\": \"c\"}"
        "]}");

    MarkerDb *db = marker_db_open(dbpath, NULL);
    int migrated = -1;
    GError *error = NULL;
    g_assert_true(marker_db_migrate_from_json(db, json, &migrated, &error));
    g_assert_true(error == NULL && migrated == 2);
    g_assert_true(g_file_test(backup, G_FILE_TEST_IS_REGULAR));

    MarkerStore *store = marker_db_load_all(db, FALSE, NULL);
    g_assert_cmpint(marker_store_count(store), ==, 2);
    g_assert_nonnull(marker_store_find_by_id(store, "a"));
    g_assert_null(marker_store_find_by_id(store, "b"));
    g_assert_nonnull(marker_store_find_by_id(store, "c"));
    marker_store_free(store);

    marker_db_free(db);
    g_free(json);
    g_free(backup);
    g_free(dbpath);
}

int
main(void)
{
    GError *error = NULL;
    char *dir = g_dir_make_tmp("syncview-marker-db-XXXXXX", &error);
    g_assert_nonnull(dir);

    test_creates_schema(dir);
    test_reopen_is_idempotent(dir);
    test_migrates_older_version(dir);
    test_open_failure(dir);
    test_save_load_roundtrip(dir);
    test_load_excludes_deleted(dir);
    test_save_batch_rolls_back_on_error(dir);
    test_soft_delete(dir);
    test_migrate_json(dir);
    test_migrate_json_no_op_cases(dir);
    test_migrate_json_errors(dir);
    test_open_migrating(dir);
    test_null_db_version(dir);
    test_resave_restores_deleted(dir);
    test_migrate_json_out_of_range_timestamp(dir);
    test_migrate_json_duplicates(dir);

    marker_db_free(NULL);

    /* Pulizia della directory temporanea. */
    char *cmd = g_strdup_printf("rm -rf '%s'", dir);
    g_assert_cmpint(system(cmd), ==, 0);
    g_free(cmd);
    g_free(dir);
    return 0;
}
