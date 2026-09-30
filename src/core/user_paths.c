#include "core/user_paths.h"

#include "core/logger.h"

#include <glib/gstdio.h>
#include <json-glib/json-glib.h>

struct UserPaths {
    char *file_path;
    char *video_paths[SYNCVIEW_MAX_VIDEOS];  /* owned, NULL = slot vuoto */
    char *last_export_dir;                   /* owned, NULL = nessuna */
};

G_DEFINE_QUARK(user-paths-error-quark, user_paths_error)

char *
user_paths_default_file(void)
{
    return g_build_filename(g_get_home_dir(), ".syncview", "user_paths.json", NULL);
}

static void
reset_values(UserPaths *paths)
{
    for (int i = 0; i < SYNCVIEW_MAX_VIDEOS; i++) {
        g_clear_pointer(&paths->video_paths[i], g_free);
    }
    g_clear_pointer(&paths->last_export_dir, g_free);
}

/* Stringa non vuota → copia; null/assente/stringa vuota/tipo diverso → NULL (come `Path(p) if p else None`). */
static char *
node_to_path(JsonNode *node)
{
    if (!node || !JSON_NODE_HOLDS_VALUE(node) || json_node_get_value_type(node) != G_TYPE_STRING) {
        return NULL;
    }

    const char *s = json_node_get_string(node);
    return (s && *s) ? g_strdup(s) : NULL;
}

static void
load_paths(UserPaths *paths)
{
    JsonParser *parser = json_parser_new();

    /* Qualunque errore (file assente, JSON invalido, struttura inattesa) → valori vuoti. */
    if (!json_parser_load_from_file(parser, paths->file_path, NULL)) {
        g_object_unref(parser);
        return;
    }

    JsonNode *root = json_parser_get_root(parser);
    if (!root || !JSON_NODE_HOLDS_OBJECT(root)) {
        g_object_unref(parser);
        return;
    }

    JsonObject *obj = json_node_get_object(root);
    JsonNode *videos = json_object_get_member(obj, "video_paths");

    if (videos && JSON_NODE_HOLDS_ARRAY(videos)) {
        JsonArray *array = json_node_get_array(videos);
        guint n = json_array_get_length(array);

        /* Oltre SYNCVIEW_MAX_VIDEOS elementi vengono ignorati; se ne mancano, gli slot restano vuoti. */
        for (guint i = 0; i < n && i < SYNCVIEW_MAX_VIDEOS; i++) {
            paths->video_paths[i] = node_to_path(json_array_get_element(array, i));
        }
    }

    paths->last_export_dir = node_to_path(json_object_get_member(obj, "last_export_dir"));

    g_object_unref(parser);
}

UserPaths *
user_paths_new(const char *file_path)
{
    UserPaths *paths = g_new0(UserPaths, 1);
    paths->file_path = g_strdup(file_path);

    char *dir = g_path_get_dirname(file_path);
    g_mkdir_with_parents(dir, 0755);
    g_free(dir);

    load_paths(paths);
    return paths;
}

void
user_paths_free(UserPaths *paths)
{
    if (!paths) {
        return;
    }

    reset_values(paths);
    g_free(paths->file_path);
    g_free(paths);
}

static void
add_path_or_null(JsonBuilder *builder, const char *path)
{
    if (path) {
        json_builder_add_string_value(builder, path);
    } else {
        json_builder_add_null_value(builder);
    }
}

static gboolean
save_paths(const UserPaths *paths, GError **error)
{
    JsonBuilder *builder = json_builder_new();

    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "video_paths");
    json_builder_begin_array(builder);
    for (int i = 0; i < SYNCVIEW_MAX_VIDEOS; i++) {
        add_path_or_null(builder, paths->video_paths[i]);
    }
    json_builder_end_array(builder);
    json_builder_set_member_name(builder, "last_export_dir");
    add_path_or_null(builder, paths->last_export_dir);
    json_builder_end_object(builder);

    JsonNode *root = json_builder_get_root(builder);
    JsonGenerator *generator = json_generator_new();
    json_generator_set_root(generator, root);
    json_generator_set_pretty(generator, TRUE);
    json_generator_set_indent(generator, 2);

    char *dir = g_path_get_dirname(paths->file_path);
    g_mkdir_with_parents(dir, 0755);
    g_free(dir);

    /* Scrittura atomica (file temporaneo + rename): un crash non lascia un JSON troncato. */
    gboolean ok = json_generator_to_file(generator, paths->file_path, NULL);
    if (ok) {
        char *details = g_strdup_printf("File: %s", paths->file_path);
        log_user_action("user_paths.json salvato", details);
        g_free(details);
    } else {
        g_set_error(error, USER_PATHS_ERROR, USER_PATHS_ERROR_IO, "Impossibile salvare %s",
                    paths->file_path);

        char *message = g_strdup_printf("Errore salvataggio user paths: impossibile scrivere %s",
                                        paths->file_path);
        log_error(message, NULL);
        g_free(message);
    }

    g_object_unref(generator);
    json_node_unref(root);
    g_object_unref(builder);
    return ok;
}

static gboolean
check_index(int index, GError **error)
{
    if (index >= 0 && index < SYNCVIEW_MAX_VIDEOS) {
        return TRUE;
    }

    g_set_error(error, USER_PATHS_ERROR, USER_PATHS_ERROR_INDEX, "Slot video non valido: %d", index);
    return FALSE;
}

gboolean
user_paths_set_video_path(UserPaths *paths, int index, const char *path, GError **error)
{
    if (!check_index(index, error)) {
        return FALSE;
    }

    g_free(paths->video_paths[index]);
    paths->video_paths[index] = g_strdup(path);

    char *details = g_strdup_printf("Slot %d: %s", index, path ? path : "");
    log_user_action("Percorso salvato in user_paths", details);
    g_free(details);

    return save_paths(paths, error);
}

const char *
user_paths_get_video_path(const UserPaths *paths, int index)
{
    if (index < 0 || index >= SYNCVIEW_MAX_VIDEOS) {
        return NULL;
    }
    return paths->video_paths[index];
}

gboolean
user_paths_clear_video_path(UserPaths *paths, int index, GError **error)
{
    if (!check_index(index, error)) {
        return FALSE;
    }

    g_clear_pointer(&paths->video_paths[index], g_free);
    return save_paths(paths, error);
}

gboolean
user_paths_set_export_dir(UserPaths *paths, const char *path, GError **error)
{
    g_free(paths->last_export_dir);
    paths->last_export_dir = (path && *path) ? g_strdup(path) : NULL;
    return save_paths(paths, error);
}

const char *
user_paths_get_export_dir(const UserPaths *paths)
{
    return paths->last_export_dir;
}

int
user_paths_get_valid_video_paths(UserPaths *paths, const char *out[SYNCVIEW_MAX_VIDEOS],
                                 GError **save_error)
{
    int valid = 0;
    gboolean changed = FALSE;

    for (int i = 0; i < SYNCVIEW_MAX_VIDEOS; i++) {
        out[i] = NULL;

        if (!paths->video_paths[i]) {
            continue;
        }

        if (g_file_test(paths->video_paths[i], G_FILE_TEST_IS_REGULAR)) {
            out[i] = paths->video_paths[i];
            valid++;
        } else {
            char *details = g_strdup_printf("Slot %d: %s (file non trovato)", i, paths->video_paths[i]);
            log_user_action("Percorso non valido rimosso", details);
            g_free(details);

            g_clear_pointer(&paths->video_paths[i], g_free);
            changed = TRUE;
        }
    }

    if (changed) {
        save_paths(paths, save_error);
    }

    return valid;
}
