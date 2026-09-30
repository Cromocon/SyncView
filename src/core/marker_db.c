#include "core/marker_db.h"

#include "core/markers.h"

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
        Marker *m = g_new0(Marker, 1);

        m->id = column_text_or(stmt, 0, "");
        m->timestamp_ms = sqlite3_column_int64(stmt, 1);
        m->color = column_text_or(stmt, 2, "");
        m->description = column_text_or(stmt, 3, "");
        m->category = column_text_or(stmt, 4, "default");
        m->video_index = sqlite3_column_type(stmt, 5) == SQLITE_NULL
                             ? SYNCVIEW_MARKER_VIDEO_INDEX_ALL
                             : sqlite3_column_int(stmt, 5);
        m->created_at = column_text_or(stmt, 6, "");

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
