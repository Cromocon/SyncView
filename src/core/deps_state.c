#include "core/deps_state.h"

#include <json-glib/json-glib.h>
#include <gio/gio.h>
#include <string.h>

#define STATE_VERSION 1

struct DepsState {
    char *path;
    char *declined;  /* firma, NULL = nessuna scelta */
    gint64 last_check;
};

char *
deps_state_default_file(void)
{
    return g_build_filename(g_get_home_dir(), ".syncview", "deps_state.json", NULL);
}

DepsState *
deps_state_load(const char *path)
{
    DepsState *state = g_new0(DepsState, 1);
    JsonParser *parser = json_parser_new();

    state->path = path ? g_strdup(path) : deps_state_default_file();
    if (json_parser_load_from_file(parser, state->path, NULL)) {
        JsonNode *root = json_parser_get_root(parser);

        if (root && JSON_NODE_HOLDS_OBJECT(root)) {
            JsonObject *object = json_node_get_object(root);

            if (json_object_has_member(object, "declined_signature") &&
                json_object_get_member(object, "declined_signature") &&
                JSON_NODE_HOLDS_VALUE(json_object_get_member(object, "declined_signature")) &&
                json_node_get_value_type(json_object_get_member(object, "declined_signature")) == G_TYPE_STRING) {
                state->declined = g_strdup(json_object_get_string_member(object, "declined_signature"));
            }
            if (json_object_has_member(object, "last_check_unix") &&
                json_node_get_value_type(json_object_get_member(object, "last_check_unix")) == G_TYPE_INT64) {
                state->last_check = json_object_get_int_member(object, "last_check_unix");
            }
        }
    }
    g_object_unref(parser);
    return state;
}

void
deps_state_free(DepsState *state)
{
    if (state) {
        g_free(state->path);
        g_free(state->declined);
        g_free(state);
    }
}

gboolean
deps_state_save(DepsState *state, GError **error)
{
    g_return_val_if_fail(state != NULL, FALSE);

    JsonBuilder *builder = json_builder_new();

    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "version");
    json_builder_add_int_value(builder, STATE_VERSION);
    json_builder_set_member_name(builder, "declined_signature");
    if (state->declined) {
        json_builder_add_string_value(builder, state->declined);
    } else {
        json_builder_add_null_value(builder);
    }
    json_builder_set_member_name(builder, "last_check_unix");
    json_builder_add_int_value(builder, state->last_check);
    json_builder_end_object(builder);

    JsonGenerator *generator = json_generator_new();
    JsonNode *root = json_builder_get_root(builder);

    json_generator_set_root(generator, root);
    json_generator_set_pretty(generator, TRUE);

    char *data = json_generator_to_data(generator, NULL);
    char *dir = g_path_get_dirname(state->path);
    gboolean ok = g_mkdir_with_parents(dir, 0700) == 0 || g_file_test(dir, G_FILE_TEST_IS_DIR);

    if (!ok) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "Impossibile creare la cartella %s", dir);
    } else {
        ok = g_file_set_contents(state->path, data, -1, error);  /* scrittura atomica (file temporaneo + rename) */
    }

    g_free(dir);
    g_free(data);
    json_node_unref(root);
    g_object_unref(generator);
    g_object_unref(builder);
    return ok;
}

static int
compare_strings(gconstpointer a, gconstpointer b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

char *
deps_report_missing_signature(const DepsReport *report)
{
    GPtrArray *ids = g_ptr_array_new();

    for (size_t i = 0; i < deps_report_count(report); i++) {
        const DepsItem *item = deps_report_get(report, i);

        if (item->status != DEPS_STATUS_OK) {
            g_ptr_array_add(ids, item->id);
        }
    }
    g_ptr_array_sort(ids, compare_strings);
    g_ptr_array_add(ids, NULL);

    char *signature = g_strjoinv(",", (char **)ids->pdata);

    g_ptr_array_free(ids, TRUE);
    return signature;
}

gboolean
deps_state_should_prompt(const DepsState *state, const DepsReport *report)
{
    char *signature = deps_report_missing_signature(report);
    gboolean prompt = *signature && !(state->declined && strcmp(state->declined, signature) == 0);

    g_free(signature);
    return prompt;
}

void
deps_state_set_declined(DepsState *state, const DepsReport *report)
{
    g_free(state->declined);
    state->declined = deps_report_missing_signature(report);
    if (!*state->declined) {
        g_clear_pointer(&state->declined, g_free);
    }
}

void
deps_state_clear_declined(DepsState *state)
{
    g_clear_pointer(&state->declined, g_free);
}

const char *
deps_state_get_declined(const DepsState *state)
{
    return state->declined;
}

gint64
deps_state_get_last_check(const DepsState *state)
{
    return state->last_check;
}

void
deps_state_mark_checked(DepsState *state)
{
    state->last_check = g_get_real_time() / G_USEC_PER_SEC;
}
