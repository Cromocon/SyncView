/* M2.12: core/deps_state (scelte dell'utente tra un avvio e l'altro). Senza GTK. */
#include "core/deps_check.h"
#include "core/deps_state.h"

#include <assert.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>

/* Report finto con i componenti `missing` (id separati da virgola) dichiarati mancanti. */
static gboolean
all_elements(const char *name, gpointer user_data)
{
    (void)name;
    (void)user_data;
    return TRUE;
}

static char *
ffmpeg_found(const char *name, const char *extra_dir, gpointer user_data)
{
    (void)extra_dir;
    (void)user_data;
    return g_strdup(strcmp(name, "ffmpeg") == 0 ? "/usr/bin/ffmpeg" : "/usr/bin/pacman");
}

static char *
ffmpeg_output(const char *path, const char *const *args, gpointer user_data)
{
    (void)path;
    (void)user_data;
    return args[0] && strcmp(args[0], "-version") == 0
               ? g_strdup("ffmpeg version 7.1\n")
               : g_strdup(" V....D libx264 libx264 H.264 (codec h264)\n");
}

static DepsReport *
report_missing(const char *missing)
{
    DepsProbes probes = { all_elements, ffmpeg_found, ffmpeg_output, DEPS_PLATFORM_LINUX, NULL };

    if (missing) {
        g_setenv("SYNCVIEW_DEPS_FAKE_MISSING", missing, TRUE);
    } else {
        g_unsetenv("SYNCVIEW_DEPS_FAKE_MISSING");
    }
    DepsReport *report = deps_check_run(&probes, "/deps");
    g_unsetenv("SYNCVIEW_DEPS_FAKE_MISSING");
    return report;
}

static void
test_signature(void)
{
    DepsReport *r = report_missing(NULL);
    char *sig = deps_report_missing_signature(r);
    assert(strcmp(sig, "") == 0);
    g_free(sig);
    deps_report_free(r);

    /* Ordine alfabetico, indipendente dall'ordine del report o della lista. */
    r = report_missing("ffmpeg,gst-decoder-av1,gst-gtk4sink");
    sig = deps_report_missing_signature(r);
    assert(strcmp(sig, "ffmpeg,gst-decoder-av1,gst-gtk4sink") == 0);
    g_free(sig);
    deps_report_free(r);
}

static void
test_load_missing_and_corrupt(const char *dir)
{
    char *path = g_build_filename(dir, "no", "such", "state.json", NULL);
    DepsState *st = deps_state_load(path);
    assert(deps_state_get_declined(st) == NULL && deps_state_get_last_check(st) == 0);
    deps_state_free(st);

    const char *bad[] = { "", "{", "not json", "[1,2]", "{\"declined_signature\": 5}", "{\"declined_signature\": null, \"last_check_unix\": \"x\"}" };
    char *file = g_build_filename(dir, "bad.json", NULL);
    for (size_t i = 0; i < G_N_ELEMENTS(bad); i++) {
        assert(g_file_set_contents(file, bad[i], -1, NULL));
        st = deps_state_load(file);
        assert(deps_state_get_declined(st) == NULL && deps_state_get_last_check(st) == 0);
        deps_state_free(st);
    }
    g_free(file);
    g_free(path);
}

static void
test_prompt_logic_and_roundtrip(const char *dir)
{
    char *file = g_build_filename(dir, "sub", "deps_state.json", NULL);  /* la cartella non esiste ancora */
    DepsState *st = deps_state_load(file);
    DepsReport *ok = report_missing(NULL);
    DepsReport *a = report_missing("gst-decoder-hevc");
    DepsReport *ab = report_missing("gst-decoder-hevc,ffmpeg");

    /* Nessuna scelta: si propone solo se manca qualcosa. */
    assert(!deps_state_should_prompt(st, ok));
    assert(deps_state_should_prompt(st, a));

    /* «Continua senza» per {hevc}: non si ripropone per lo stesso insieme... */
    deps_state_set_declined(st, a);
    assert(!deps_state_should_prompt(st, a));
    /* ...ma sì se l'insieme cambia (ne manca uno in più). */
    assert(deps_state_should_prompt(st, ab));

    /* La scelta sopravvive al riavvio. */
    deps_state_mark_checked(st);
    GError *error = NULL;
    assert(deps_state_save(st, &error) && error == NULL);
    assert(g_file_test(file, G_FILE_TEST_IS_REGULAR));
    deps_state_free(st);
    st = deps_state_load(file);
    assert(strcmp(deps_state_get_declined(st), "gst-decoder-hevc") == 0);
    assert(deps_state_get_last_check(st) > 1700000000);
    assert(!deps_state_should_prompt(st, a) && deps_state_should_prompt(st, ab));

    /* Dimenticata: si ripropone. Impostarla con un report completo non lascia una scelta vuota. */
    deps_state_clear_declined(st);
    assert(deps_state_should_prompt(st, a));
    deps_state_set_declined(st, ok);
    assert(deps_state_get_declined(st) == NULL);
    assert(deps_state_save(st, NULL));
    deps_state_free(st);
    st = deps_state_load(file);
    assert(deps_state_get_declined(st) == NULL);

    /* Il file scritto è JSON con la versione. */
    char *text = NULL;
    assert(g_file_get_contents(file, &text, NULL, NULL));
    assert(strstr(text, "\"version\"") != NULL && strstr(text, "last_check_unix") != NULL);
    g_free(text);

    /* Percorso non scrivibile: errore, nessun crash. */
    char *blocked = g_build_filename(dir, "blocker", NULL);
    assert(g_file_set_contents(blocked, "x", -1, NULL));
    char *under_file = g_build_filename(blocked, "deps_state.json", NULL);
    DepsState *bad = deps_state_load(under_file);
    assert(!deps_state_save(bad, &error) && error != NULL);
    g_clear_error(&error);
    deps_state_free(bad);

    deps_state_free(st);
    deps_report_free(ok);
    deps_report_free(a);
    deps_report_free(ab);
    g_free(under_file);
    g_free(blocked);
    g_free(file);
}

int
main(void)
{
    char *dir = g_dir_make_tmp("syncview-state-XXXXXX", NULL);
    assert(dir != NULL);

    test_signature();
    test_load_missing_and_corrupt(dir);
    test_prompt_logic_and_roundtrip(dir);

    char *cmd = g_strdup_printf("rm -rf '%s'", dir);
    assert(system(cmd) == 0);
    g_free(cmd);
    g_free(dir);
    return 0;
}
