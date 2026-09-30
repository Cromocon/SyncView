#include "core/marker_db.h"

#include "core/markers.h"

#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include <limits.h>
#include <stdarg.h>
#include <string.h>
#include <sqlite3.h>
#include <stdlib.h>

#define BUSY_TIMEOUT_MS 5000

/* Schema versione 1, verbatim da DB_SCHEMAS[1] di core/marker_db.py. */
static const char *const SCHEMA_V1 =
    "CREATE TABLE IF NOT EXISTS metadata ("
    "    key TEXT PRIMARY KEY,"
    "    value TEXT NOT NULL"
    ");"
    "CREATE TABLE IF NOT EXISTS markers ("
    "    id TEXT PRIMARY KEY,"
    "    timestamp INTEGER NOT NULL,"
    "    color TEXT NOT NULL,"
    "    description TEXT DEFAULT '',"
    "    category TEXT DEFAULT 'default',"
    "    video_index INTEGER,"
    "    created_at TEXT NOT NULL,"
    "    updated_at TEXT NOT NULL,"
    "    is_deleted INTEGER DEFAULT 0,"
    "    UNIQUE(timestamp, video_index, created_at)"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_timestamp ON markers(timestamp);"
    "CREATE INDEX IF NOT EXISTS idx_category ON markers(category);"
    "CREATE INDEX IF NOT EXISTS idx_video_index ON markers(video_index);"
    "CREATE INDEX IF NOT EXISTS idx_deleted ON markers(is_deleted);";

struct MarkerDb {
    char *path;
};

G_DEFINE_QUARK(marker-db-error-quark, marker_db_error)

/* Apre una connessione (una per operazione, come l'originale). */
static sqlite3 *
connect_db(const MarkerDb *db, GError **error)
{
    sqlite3 *conn = NULL;

    if (sqlite3_open(db->path, &conn) != SQLITE_OK) {
        g_set_error(error, MARKER_DB_ERROR, MARKER_DB_ERROR_OPEN, "Impossibile aprire %s: %s",
                    db->path, conn ? sqlite3_errmsg(conn) : "memoria esaurita");
        sqlite3_close(conn);
        return NULL;
    }

    sqlite3_busy_timeout(conn, BUSY_TIMEOUT_MS);
    return conn;
}

static gboolean
exec_sql(sqlite3 *conn, const char *sql, GError **error)
{
    char *errmsg = NULL;

    if (sqlite3_exec(conn, sql, NULL, NULL, &errmsg) != SQLITE_OK) {
        g_set_error(error, MARKER_DB_ERROR, MARKER_DB_ERROR_SQL, "SQLite: %s",
                    errmsg ? errmsg : "errore sconosciuto");
        sqlite3_free(errmsg);
        return FALSE;
    }
    return TRUE;
}

static gboolean
insert_metadata(sqlite3 *conn, const char *key, const char *value, GError **error)
{
    sqlite3_stmt *stmt = NULL;

    if (sqlite3_prepare_v2(conn, "INSERT INTO metadata (key, value) VALUES (?, ?)", -1, &stmt,
                           NULL) != SQLITE_OK) {
        g_set_error(error, MARKER_DB_ERROR, MARKER_DB_ERROR_SQL, "SQLite: %s", sqlite3_errmsg(conn));
        return FALSE;
    }

    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_STATIC);

    gboolean ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) {
        g_set_error(error, MARKER_DB_ERROR, MARKER_DB_ERROR_SQL, "SQLite: %s", sqlite3_errmsg(conn));
    }
    sqlite3_finalize(stmt);
    return ok;
}

/* Ritorna TRUE se la tabella `metadata` esiste; *has_table è l'esito. */
static gboolean
has_metadata_table(sqlite3 *conn, gboolean *has_table, GError **error)
{
    sqlite3_stmt *stmt = NULL;

    if (sqlite3_prepare_v2(conn,
                           "SELECT name FROM sqlite_master WHERE type='table' AND name='metadata'",
                           -1, &stmt, NULL) != SQLITE_OK) {
        g_set_error(error, MARKER_DB_ERROR, MARKER_DB_ERROR_SQL, "SQLite: %s", sqlite3_errmsg(conn));
        return FALSE;
    }

    *has_table = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return TRUE;
}

/* Versione in metadata; 0 se la riga db_version manca (come l'originale). */
static int
read_db_version(sqlite3 *conn)
{
    sqlite3_stmt *stmt = NULL;
    int version = 0;

    if (sqlite3_prepare_v2(conn, "SELECT value FROM metadata WHERE key = 'db_version'", -1, &stmt,
                           NULL) != SQLITE_OK) {
        return 0;
    }

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        version = atoi((const char *)sqlite3_column_text(stmt, 0));
    }
    sqlite3_finalize(stmt);
    return version;
}

/* Come _migrate_database: per ora non ci sono migrazioni specifiche, aggiorna solo la versione. */
static gboolean
migrate_db(sqlite3 *conn, int to_version, GError **error)
{
    char *sql = g_strdup_printf("UPDATE metadata SET value = '%d' WHERE key = 'db_version'",
                                to_version);
    gboolean ok = exec_sql(conn, sql, error);
    g_free(sql);
    return ok;
}

static gboolean
ensure_schema(const MarkerDb *db, GError **error)
{
    sqlite3 *conn = connect_db(db, error);
    if (!conn) {
        return FALSE;
    }

    gboolean ok = exec_sql(conn, "BEGIN", error);
    gboolean has_table = FALSE;

    ok = ok && has_metadata_table(conn, &has_table, error);

    if (ok && !has_table) {
        char *version = g_strdup_printf("%d", SYNCVIEW_MARKER_DB_VERSION);
        char *created_at = marker_iso8601_now();

        ok = exec_sql(conn, SCHEMA_V1, error)
             && insert_metadata(conn, "db_version", version, error)
             && insert_metadata(conn, "created_at", created_at, error);

        g_free(version);
        g_free(created_at);
    } else if (ok && read_db_version(conn) < SYNCVIEW_MARKER_DB_VERSION) {
        ok = migrate_db(conn, SYNCVIEW_MARKER_DB_VERSION, error);
    }

    if (ok) {
        ok = exec_sql(conn, "COMMIT", error);
    } else {
        sqlite3_exec(conn, "ROLLBACK", NULL, NULL, NULL);
    }

    sqlite3_close(conn);
    return ok;
}

MarkerDb *
marker_db_open(const char *path, GError **error)
{
    char *dir = g_path_get_dirname(path);
    int rc = g_mkdir_with_parents(dir, 0755);
    g_free(dir);

    if (rc != 0) {
        g_set_error(error, MARKER_DB_ERROR, MARKER_DB_ERROR_OPEN,
                    "Impossibile creare la directory del database per %s", path);
        return NULL;
    }

    MarkerDb *db = g_new0(MarkerDb, 1);
    db->path = g_strdup(path);

    if (!ensure_schema(db, error)) {
        marker_db_free(db);
        return NULL;
    }

    return db;
}

void
marker_db_free(MarkerDb *db)
{
    if (!db) {
        return;
    }

    g_free(db->path);
    g_free(db);
}

const char *
marker_db_get_path(const MarkerDb *db)
{
    return db->path;
}

static void
set_sql_error(GError **error, sqlite3 *conn)
{
    g_set_error(error, MARKER_DB_ERROR, MARKER_DB_ERROR_SQL, "SQLite: %s", sqlite3_errmsg(conn));
}

/* Upsert verbatim da save_markers_batch. */
static const char *const UPSERT_SQL =
    "INSERT INTO markers "
    "(id, timestamp, color, description, category, video_index, created_at, updated_at, is_deleted) "
    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, 0) "
    "ON CONFLICT(id) DO UPDATE SET "
    "    timestamp = excluded.timestamp, "
    "    color = excluded.color, "
    "    description = excluded.description, "
    "    category = excluded.category, "
    "    video_index = excluded.video_index, "
    "    updated_at = excluded.updated_at";

static gboolean
upsert_all(sqlite3 *conn, const MarkerStore *store, const char *now, GError **error)
{
    sqlite3_stmt *stmt = NULL;

    if (sqlite3_prepare_v2(conn, UPSERT_SQL, -1, &stmt, NULL) != SQLITE_OK) {
        set_sql_error(error, conn);
        return FALSE;
    }

    gboolean ok = TRUE;
    for (size_t i = 0; ok && i < marker_store_count(store); i++) {
        const Marker *m = marker_store_get(store, i);

        sqlite3_reset(stmt);
        sqlite3_bind_text(stmt, 1, m->id, -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 2, m->timestamp_ms);
        sqlite3_bind_text(stmt, 3, m->color, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, m->description, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5, m->category, -1, SQLITE_STATIC);
        if (m->video_index == SYNCVIEW_MARKER_VIDEO_INDEX_ALL) {
            sqlite3_bind_null(stmt, 6);
        } else {
            sqlite3_bind_int(stmt, 6, m->video_index);
        }
        sqlite3_bind_text(stmt, 7, m->created_at, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 8, now, -1, SQLITE_STATIC);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            set_sql_error(error, conn);
            ok = FALSE;
        }
    }

    sqlite3_finalize(stmt);
    return ok;
}

gboolean
marker_db_save_batch(MarkerDb *db, const MarkerStore *store, GError **error)
{
    sqlite3 *conn = connect_db(db, error);
    if (!conn) {
        return FALSE;
    }

    char *now = marker_iso8601_now();
    gboolean ok = exec_sql(conn, "BEGIN", error) && upsert_all(conn, store, now, error);

    if (ok) {
        ok = exec_sql(conn, "COMMIT", error);
    }
    if (!ok) {
        sqlite3_exec(conn, "ROLLBACK", NULL, NULL, NULL);
    }

    g_free(now);
    sqlite3_close(conn);
    return ok;
}

static char *
column_text_or(sqlite3_stmt *stmt, int col, const char *fallback)
{
    const unsigned char *text = sqlite3_column_text(stmt, col);
    return g_strdup(text ? (const char *)text : fallback);
}

MarkerStore *
marker_db_load_all(MarkerDb *db, gboolean include_deleted, GError **error)
{
    sqlite3 *conn = connect_db(db, error);
    if (!conn) {
        return NULL;
    }

    const char *sql = include_deleted
        ? "SELECT id, timestamp, color, description, category, video_index, created_at "
          "FROM markers ORDER BY timestamp"
        : "SELECT id, timestamp, color, description, category, video_index, created_at "
          "FROM markers WHERE is_deleted = 0 ORDER BY timestamp";
    sqlite3_stmt *stmt = NULL;

    if (sqlite3_prepare_v2(conn, sql, -1, &stmt, NULL) != SQLITE_OK) {
        set_sql_error(error, conn);
        sqlite3_close(conn);
        return NULL;
    }

    MarkerStore *store = marker_store_new();
    int rc;

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        char *id = column_text_or(stmt, 0, "");
        char *color = column_text_or(stmt, 2, "");
        char *description = column_text_or(stmt, 3, "");
        char *category = column_text_or(stmt, 4, "default");
        char *created_at = column_text_or(stmt, 6, "");
        int video_index = sqlite3_column_type(stmt, 5) == SQLITE_NULL
                              ? SYNCVIEW_MARKER_VIDEO_INDEX_ALL
                              : sqlite3_column_int(stmt, 5);

        Marker *m = marker_new_full(id, sqlite3_column_int64(stmt, 1), color, description, category,
                                    video_index, created_at);

        g_free(id);
        g_free(color);
        g_free(description);
        g_free(category);
        g_free(created_at);

        marker_store_add_marker(store, m);
    }

    if (rc != SQLITE_DONE) {
        set_sql_error(error, conn);
        marker_store_free(store);
        store = NULL;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(conn);
    return store;
}

gboolean
marker_db_delete(MarkerDb *db, const char *id, GError **error)
{
    sqlite3 *conn = connect_db(db, error);
    if (!conn) {
        return FALSE;
    }

    sqlite3_stmt *stmt = NULL;
    gboolean ok = FALSE;

    if (sqlite3_prepare_v2(conn, "UPDATE markers SET is_deleted = 1, updated_at = ? WHERE id = ?",
                           -1, &stmt, NULL) == SQLITE_OK) {
        char *now = marker_iso8601_now();

        sqlite3_bind_text(stmt, 1, now, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, id, -1, SQLITE_STATIC);
        ok = sqlite3_step(stmt) == SQLITE_DONE;

        g_free(now);
    }

    if (!ok) {
        set_sql_error(error, conn);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(conn);
    return ok;
}

/* --- Migrazione JSON legacy -> SQLite (porting di MarkerManager._migrate_from_json) --- */

static void
set_json_error(GError **error, const char *json_path, const char *fmt, ...) G_GNUC_PRINTF(3, 4);

static void
set_json_error(GError **error, const char *json_path, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    char *detail = g_strdup_vprintf(fmt, args);
    va_end(args);

    g_set_error(error, MARKER_DB_ERROR, MARKER_DB_ERROR_JSON, "JSON legacy %s: %s", json_path,
                detail);
    g_free(detail);
}

/*
 * Campo stringa opzionale: assente o null -> *out = NULL; altrimenti deve essere una stringa.
 * Ritorna FALSE (con error) se il tipo è sbagliato.
 */
static gboolean
json_get_string(JsonObject *obj, const char *name, const char *json_path, const char **out,
                GError **error)
{
    *out = NULL;
    JsonNode *node = json_object_get_member(obj, name);
    if (!node || JSON_NODE_HOLDS_NULL(node)) {
        return TRUE;
    }
    if (!JSON_NODE_HOLDS_VALUE(node) || json_node_get_value_type(node) != G_TYPE_STRING) {
        set_json_error(error, json_path, "il campo '%s' deve essere una stringa", name);
        return FALSE;
    }
    *out = json_node_get_string(node);
    return TRUE;
}

/* Campo numerico intero (accetta anche 1000.0, come un int Python). *present = FALSE se assente/null. */
static gboolean
json_get_int(JsonObject *obj, const char *name, const char *json_path, int64_t *out,
             gboolean *present, GError **error)
{
    *present = FALSE;
    JsonNode *node = json_object_get_member(obj, name);
    if (!node || JSON_NODE_HOLDS_NULL(node)) {
        return TRUE;
    }

    GType type = JSON_NODE_HOLDS_VALUE(node) ? json_node_get_value_type(node) : G_TYPE_INVALID;
    if (type != G_TYPE_INT64 && type != G_TYPE_DOUBLE) {
        set_json_error(error, json_path, "il campo '%s' deve essere un numero", name);
        return FALSE;
    }

    *out = type == G_TYPE_INT64 ? json_node_get_int(node) : (int64_t)json_node_get_double(node);
    *present = TRUE;
    return TRUE;
}

static Marker *
marker_from_json_object(JsonObject *obj, const char *json_path, GError **error)
{
    /* Chiavi ammesse: i campi della dataclass + 'label' (scartato). Come
     * Marker(**data), qualunque altra chiave è un errore per l'intera migrazione. */
    static const char *const known[] = { "timestamp", "color", "description", "category",
                                         "video_index", "created_at", "id", "label" };
    GList *members = json_object_get_members(obj);

    for (GList *l = members; l; l = l->next) {
        gboolean ok = FALSE;
        for (size_t i = 0; i < G_N_ELEMENTS(known); i++) {
            if (strcmp(l->data, known[i]) == 0) {
                ok = TRUE;
                break;
            }
        }
        if (!ok) {
            set_json_error(error, json_path, "campo marker sconosciuto '%s'", (const char *)l->data);
            g_list_free(members);
            return NULL;
        }
    }
    g_list_free(members);

    int64_t timestamp = 0;
    int64_t video = 0;
    gboolean has_timestamp, has_video;
    const char *color, *description, *category, *created_at, *id;

    if (!json_get_int(obj, "timestamp", json_path, &timestamp, &has_timestamp, error)
        || !json_get_int(obj, "video_index", json_path, &video, &has_video, error)
        || !json_get_string(obj, "color", json_path, &color, error)
        || !json_get_string(obj, "description", json_path, &description, error)
        || !json_get_string(obj, "category", json_path, &category, error)
        || !json_get_string(obj, "created_at", json_path, &created_at, error)
        || !json_get_string(obj, "id", json_path, &id, error)) {
        return NULL;
    }

    /* timestamp e color sono obbligatori (senza default nella dataclass). */
    if (!has_timestamp || !color) {
        set_json_error(error, json_path, "marker senza '%s'", !has_timestamp ? "timestamp" : "color");
        return NULL;
    }
    if (has_video && (video < INT_MIN || video > INT_MAX)) {
        set_json_error(error, json_path, "video_index fuori range");
        return NULL;
    }

    Marker *m = marker_new_full(id, timestamp, color, description, category,
                                has_video ? (int)video : SYNCVIEW_MARKER_VIDEO_INDEX_ALL, created_at);
    if (!m) {
        set_json_error(error, json_path, "memoria esaurita");
    }
    return m;
}

/* Parsing completo in un MarkerStore; NULL + error al primo marker non valido (nulla viene salvato). */
static MarkerStore *
load_legacy_json(const char *json_path, GError **error)
{
    JsonParser *parser = json_parser_new();
    GError *parse_error = NULL;

    if (!json_parser_load_from_file(parser, json_path, &parse_error)) {
        set_json_error(error, json_path, "%s", parse_error->message);
        g_error_free(parse_error);
        g_object_unref(parser);
        return NULL;
    }

    JsonNode *root = json_parser_get_root(parser);
    if (!root || !JSON_NODE_HOLDS_OBJECT(root)) {
        set_json_error(error, json_path, "la radice deve essere un oggetto");
        g_object_unref(parser);
        return NULL;
    }

    MarkerStore *store = marker_store_new();
    JsonNode *markers_node = json_object_get_member(json_node_get_object(root), "markers");

    if (markers_node && !JSON_NODE_HOLDS_NULL(markers_node)) {
        if (!JSON_NODE_HOLDS_ARRAY(markers_node)) {
            set_json_error(error, json_path, "'markers' deve essere una lista");
            goto fail;
        }

        JsonArray *array = json_node_get_array(markers_node);
        for (guint i = 0; i < json_array_get_length(array); i++) {
            JsonNode *element = json_array_get_element(array, i);
            if (!JSON_NODE_HOLDS_OBJECT(element)) {
                set_json_error(error, json_path, "marker #%u non è un oggetto", i);
                goto fail;
            }

            Marker *m = marker_from_json_object(json_node_get_object(element), json_path, error);
            if (!m) {
                goto fail;
            }
            marker_store_add_marker(store, m);
        }
    }

    g_object_unref(parser);
    return store;

fail:
    marker_store_free(store);
    g_object_unref(parser);
    return NULL;
}

/* Come Path.with_suffix('.json.backup'): sostituisce l'ultima estensione del nome file (o la aggiunge). */
static char *
legacy_backup_path(const char *json_path)
{
    const char *base = strrchr(json_path, G_DIR_SEPARATOR);
    const char *name = base ? base + 1 : json_path;
    const char *dot = strrchr(name, '.');
    /* Un punto iniziale (".hidden") non è un'estensione, come in pathlib. */
    size_t keep = (dot && dot != name) ? (size_t)(dot - json_path) : strlen(json_path);

    return g_strdup_printf("%.*s.json.backup", (int)keep, json_path);
}

gboolean
marker_db_migrate_from_json(MarkerDb *db, const char *json_path, int *migrated_count,
                            GError **error)
{
    if (migrated_count) {
        *migrated_count = 0;
    }

    /* File assente: niente da migrare, non è un errore (come l'`exists()` iniziale). */
    if (!g_file_test(json_path, G_FILE_TEST_IS_REGULAR)) {
        return TRUE;
    }

    MarkerStore *store = load_legacy_json(json_path, error);
    if (!store) {
        return FALSE;
    }

    size_t count = marker_store_count(store);
    gboolean ok = TRUE;

    /* Lista vuota: come l'originale (`if self._db and markers`) non salva e non crea backup. */
    if (count > 0) {
        ok = marker_db_save_batch(db, store, error);

        if (ok) {
            char *backup = legacy_backup_path(json_path);

            if (g_rename(json_path, backup) != 0) {
                g_set_error(error, MARKER_DB_ERROR, MARKER_DB_ERROR_JSON,
                            "Marker migrati ma impossibile creare il backup %s", backup);
                ok = FALSE;
            } else if (migrated_count) {
                *migrated_count = (int)count;
            }
            g_free(backup);
        }
    }

    marker_store_free(store);
    return ok;
}

MarkerDb *
marker_db_open_migrating(const char *db_path, const char *legacy_json_path,
                         GError **migration_error, GError **error)
{
    /* Come il costruttore di MarkerManager: si migra solo se il DB non esiste ancora. */
    gboolean db_existed = g_file_test(db_path, G_FILE_TEST_EXISTS);

    MarkerDb *db = marker_db_open(db_path, error);
    if (!db) {
        return NULL;
    }

    if (!db_existed && legacy_json_path) {
        /* Un fallimento della migrazione non impedisce l'apertura (l'originale lo registra e prosegue). */
        marker_db_migrate_from_json(db, legacy_json_path, NULL, migration_error);
    }

    return db;
}
