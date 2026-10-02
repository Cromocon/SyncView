/*
 * M2.11: core/dep_installer e core/dep_manifest. Senza rete esterna e senza toccare il sistema: pkexec è uno script
 * finto, i download vanno a un server HTTP locale (libsoup) in un thread, gli installer di piattaforma sono funzioni
 * finte. Gira ovunque (nessun display, nessun GStreamer).
 */
#include "core/dep_installer.h"
#include "core/dep_manifest.h"
#include "core/logger.h"
#include "zip_builder.h"

#include <assert.h>
#include <glib/gstdio.h>
#include <libsoup/soup.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HAS(text, needle) ((text) != NULL && strstr((text), (needle)) != NULL)
#define ENTRY(...) ((ZipEntry){ __VA_ARGS__ })

static char *root;
static int counter;

static char *
new_dir(const char *prefix)
{
    char *dir = g_strdup_printf("%s/%s-%d", root, prefix, counter++);

    assert(g_mkdir_with_parents(dir, 0700) == 0);
    return dir;
}

static gboolean
error_is(GError *error, DepInstallerError code)
{
    if (!g_error_matches(error, DEP_INSTALLER_ERROR, code)) {
        g_printerr("errore inatteso: %s (codice %d), atteso %d\n", error ? error->message : "(nessuno)", error ? error->code : -1,
                   code);
        return FALSE;
    }
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* Sistema finto per costruire i report                                */
/* ------------------------------------------------------------------ */

typedef struct {
    GHashTable *missing_elements;
    GHashTable *programs;
} FakeSystem;

static gboolean
fake_has_element(const char *name, gpointer user_data)
{
    GHashTable *missing = ((FakeSystem *)user_data)->missing_elements;

    /* «*h264*» fa mancare ogni decoder H.264 (nome con h264, o vtdec di macOS): sono molti, software e hardware. */
    return !g_hash_table_contains(missing, name) &&
           !(g_hash_table_contains(missing, "*h264*") && (strstr(name, "h264") || strstr(name, "vtdec")));
}

static char *
fake_find_program(const char *name, const char *extra_dir, gpointer user_data)
{
    (void)extra_dir;
    const char *path = g_hash_table_lookup(((FakeSystem *)user_data)->programs, name);

    return path ? g_strdup(path) : NULL;
}

static char *
fake_run_program(const char *path, const char *const *args, gpointer user_data)
{
    (void)path;
    (void)user_data;
    if (args[0] && strcmp(args[0], "-version") == 0) {
        return g_strdup("ffmpeg version 7.1.1 Copyright (c) 2000-2025 the FFmpeg developers\n");
    }
    if (args[0] && args[1] && strcmp(args[1], "-encoders") == 0) {
        return g_strdup("Encoders:\n V....D libx264              libx264 H.264 / AVC\n");
    }
    return NULL;
}

static FakeSystem *
fake_new(const char *manager, const char *const *missing)
{
    FakeSystem *f = g_new0(FakeSystem, 1);

    f->missing_elements = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    f->programs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    g_hash_table_insert(f->programs, g_strdup("ffmpeg"), g_strdup("/usr/bin/ffmpeg"));
    if (manager) {
        g_hash_table_insert(f->programs, g_strdup(manager), g_strdup("/usr/bin/manager"));
    }
    for (size_t i = 0; missing && missing[i]; i++) {
        g_hash_table_add(f->missing_elements, g_strdup(missing[i]));
    }
    return f;
}

static void
fake_free(FakeSystem *f)
{
    g_hash_table_destroy(f->missing_elements);
    g_hash_table_destroy(f->programs);
    g_free(f);
}

static DepsReport *
make_report(DepsPlatform platform, FakeSystem *f)
{
    DepsProbes probes = { fake_has_element, fake_find_program, fake_run_program, platform, f };

    return deps_check_run(&probes, "/deps");
}

static const char *H264_DECODERS[] = { "*h264*", NULL };

static char *
write_os_release(const char *id, const char *version)
{
    char *path = g_strdup_printf("%s/os-release-%d", root, counter++);
    char *text = g_strdup_printf("NAME=\"Test\"\nID=%s\nVERSION_ID=\"%s\"\n", id, version);

    assert(g_file_set_contents(path, text, -1, NULL));
    g_free(text);
    return path;
}

/* ------------------------------------------------------------------ */
/* Manifest                                                            */
/* ------------------------------------------------------------------ */

static void
test_manifest(void)
{
    const DepArtifact *all = NULL;
    size_t n = dep_manifest_all(&all);

    assert(n >= 3);
    for (size_t i = 0; i < n; i++) {
        assert(dep_artifact_is_well_formed(&all[i]));  /* https, SHA-256 di 64 cifre minuscole, nome file semplice */
    }
    assert(dep_manifest_find("gstreamer-official-installer", DEPS_PLATFORM_WINDOWS, "x86_64") != NULL);
    assert(dep_manifest_find("gstreamer-official-installer", DEPS_PLATFORM_WINDOWS, "arm64") != NULL);
    assert(dep_manifest_find("gstreamer-official-installer", DEPS_PLATFORM_MACOS, "universal") != NULL);
    assert(dep_manifest_find("gstreamer-official-installer", DEPS_PLATFORM_LINUX, "x86_64") == NULL);  /* su Linux niente installer */
    assert(dep_manifest_find("non-esiste", DEPS_PLATFORM_WINDOWS, "x86_64") == NULL);
    assert(dep_manifest_gstreamer_installer(DEPS_PLATFORM_WINDOWS) != NULL);
    assert(dep_manifest_gstreamer_installer(DEPS_PLATFORM_MACOS) != NULL);
    assert(dep_manifest_gstreamer_installer(DEPS_PLATFORM_LINUX) == NULL);

    /* Voci mal formate sono riconosciute come tali. */
    DepArtifact bad = *dep_manifest_gstreamer_installer(DEPS_PLATFORM_WINDOWS);

    bad.url = "http://example.com/x.exe";
    assert(!dep_artifact_is_well_formed(&bad));
    bad = *dep_manifest_gstreamer_installer(DEPS_PLATFORM_WINDOWS);
    bad.sha256 = "ABCD";
    assert(!dep_artifact_is_well_formed(&bad));
    bad = *dep_manifest_gstreamer_installer(DEPS_PLATFORM_WINDOWS);
    bad.sha256 = "032FC6062B8539838FC8DA22589CB9B24C5D820BAA7F8CC160AF9EA08395BADF";  /* maiuscole */
    assert(!dep_artifact_is_well_formed(&bad));
    bad = *dep_manifest_gstreamer_installer(DEPS_PLATFORM_WINDOWS);
    bad.filename = "../evil.exe";
    assert(!dep_artifact_is_well_formed(&bad));
    bad = *dep_manifest_gstreamer_installer(DEPS_PLATFORM_WINDOWS);
    bad.size_bytes = 0;
    assert(!dep_artifact_is_well_formed(&bad));
}

/* ------------------------------------------------------------------ */
/* Comandi dei gestori                                                 */
/* ------------------------------------------------------------------ */

static void
expect_argv(const char *manager, const char *const *packages, const char *const *expected)
{
    GError *error = NULL;
    char **argv = dep_installer_build_package_argv(manager, packages, &error);

    assert(argv != NULL && error == NULL);
    size_t i = 0;

    for (; expected[i]; i++) {
        assert(argv[i] != NULL && strcmp(argv[i], expected[i]) == 0);
    }
    assert(argv[i] == NULL);  /* né un argomento in più */
    g_strfreev(argv);
}

static void
expect_invalid(const char *manager, const char *const *packages)
{
    GError *error = NULL;

    assert(dep_installer_build_package_argv(manager, packages, &error) == NULL);
    assert(error_is(error, DEP_INSTALLER_ERROR_INVALID));
    g_error_free(error);
}

static void
test_package_argv(void)
{
    /* argv esatto per ogni gestore, senza shell. */
    const char *arch[] = { "gst-plugin-gtk4", NULL };
    const char *arch_expected[] = { "pacman", "-S", "--noconfirm", "--needed", "gst-plugin-gtk4", NULL };

    expect_argv("pacman", arch, arch_expected);

    const char *apt[] = { "gstreamer1.0-gtk4", "gstreamer1.0-libav", NULL };
    const char *apt_expected[] = { "apt-get", "install", "-y", "gstreamer1.0-gtk4", "gstreamer1.0-libav", NULL };

    expect_argv("apt-get", apt, apt_expected);

    const char *dnf[] = { "gstreamer1-plugin-gtk4", NULL };
    const char *dnf_expected[] = { "dnf", "install", "-y", "gstreamer1-plugin-gtk4", NULL };

    expect_argv("dnf", dnf, dnf_expected);

    const char *zypper[] = { "gstreamer-plugins-base", NULL };
    const char *zypper_expected[] = { "zypper", "--non-interactive", "install", "gstreamer-plugins-base", NULL };

    expect_argv("zypper", zypper, zypper_expected);

    /* Gestore sconosciuto o assente, lista vuota. */
    expect_invalid("yum", arch);
    expect_invalid(NULL, arch);
    expect_invalid("pacman", (const char *[]){ NULL });
    expect_invalid("pacman", NULL);
    expect_invalid("/usr/bin/pacman", arch);

    /* Nomi non presenti nella tabella interna, o con caratteri sospetti. */
    const char *suspicious[] = {
        "evil-package", "gst-plugin-gtk4; rm -rf /", "-S", "--root=/tmp/x", "$(id)", "`id`", "", "GST-PLUGIN-GTK4",
        "gst plugin", "gst-plugin-gtk4\n", "gst-plugin-gtk4 evil", "../gst-plugin-gtk4", "gst-plugin-gtk4|cat",
        "gst-plugin-gtk4&&id", "-gst-plugin-gtk4", NULL,
    };

    for (size_t i = 0; suspicious[i]; i++) {
        const char *one[] = { suspicious[i], NULL };

        expect_invalid("pacman", one);
    }

    /* Un nome valido ma nella lista c'è anche uno sconosciuto: si rifiuta tutto. */
    const char *mixed[] = { "gst-plugin-gtk4", "sconosciuto", NULL };

    expect_invalid("pacman", mixed);

    /* Il nome di un'altra distribuzione non vale per questo gestore. */
    const char *wrong[] = { "gstreamer1.0-gtk4", NULL };

    expect_invalid("pacman", wrong);
    expect_invalid("dnf", arch);
}

/* ------------------------------------------------------------------ */
/* Piano da un report                                                  */
/* ------------------------------------------------------------------ */

static void
test_plan_linux_packages(void)
{
    const char *missing[] = { "gtk4paintablesink", NULL };
    FakeSystem *f = fake_new("pacman", missing);
    DepsReport *report = make_report(DEPS_PLATFORM_LINUX, f);
    GError *error = NULL;
    DepPlan *plan = dep_plan_new(report, NULL, &error);

    assert(plan && !error);
    assert(dep_plan_step_count(plan) == 1);

    const DepStep *step = dep_plan_step(plan, 0);

    assert(step->kind == DEP_STEP_SYSTEM_PACKAGES && step->needs_elevation && !step->third_party);
    assert(strcmp(step->argv[0], "pacman") == 0 && strcmp(step->argv[4], "gst-plugin-gtk4") == 0 && step->argv[5] == NULL);
    assert(dep_plan_needs_elevation(plan) && !dep_plan_has_third_party(plan) && dep_plan_download_bytes(plan) == 0);

    char *text = dep_plan_describe(plan);

    assert(HAS(text, "gst-plugin-gtk4") && HAS(text, "pacman") && HAS(text, "password di amministratore"));
    g_free(text);
    dep_plan_free(plan);
    deps_report_free(report);

    /* Sistema completo: niente da fare. */
    fake_free(f);
    f = fake_new("pacman", NULL);
    report = make_report(DEPS_PLATFORM_LINUX, f);
    assert(dep_plan_new(report, NULL, &error) == NULL);
    assert(error_is(error, DEP_INSTALLER_ERROR_NOTHING_TO_DO));
    g_clear_error(&error);
    deps_report_free(report);
    fake_free(f);

    /* Un opzionale mancante entra nel piano solo se richiesto (e solo se installabile: qui non c'è nulla di opzionale in pacchetti). */
    const char *missing_hevc[] = { "avdec_h265", "x265dec", NULL };

    f = fake_new("pacman", missing_hevc);
    report = make_report(DEPS_PLATFORM_LINUX, f);
    assert(dep_plan_new(report, NULL, &error) == NULL);
    g_clear_error(&error);
    deps_report_free(report);
    fake_free(f);

    /* Nessun gestore riconosciuto: solo istruzioni, il piano è vuoto. */
    f = fake_new(NULL, missing);
    report = make_report(DEPS_PLATFORM_LINUX, f);
    assert(dep_plan_new(report, NULL, &error) == NULL);
    assert(error_is(error, DEP_INSTALLER_ERROR_NOTHING_TO_DO));
    g_clear_error(&error);
    deps_report_free(report);
    fake_free(f);
}

static void
test_plan_platform_installer(void)
{
    const char *missing[] = { "gtk4paintablesink", "playbin3", NULL };
    DepsPlatform platforms[] = { DEPS_PLATFORM_WINDOWS, DEPS_PLATFORM_MACOS };

    for (size_t i = 0; i < G_N_ELEMENTS(platforms); i++) {
        FakeSystem *f = fake_new(NULL, missing);
        DepsReport *report = make_report(platforms[i], f);
        GError *error = NULL;
        DepPlan *plan = dep_plan_new(report, NULL, &error);

        /* Due componenti mancanti, ma UN solo passo: l'installer ufficiale. */
        assert(plan && dep_plan_step_count(plan) == 1);

        const DepStep *step = dep_plan_step(plan, 0);
        const DepArtifact *expected = dep_manifest_gstreamer_installer(platforms[i]);

        assert(step->kind == DEP_STEP_PLATFORM_INSTALLER && step->artifact == expected && step->needs_elevation);
        assert(!step->third_party && step->download_bytes == expected->size_bytes);
        assert(dep_plan_download_bytes(plan) == expected->size_bytes);

        char *text = dep_plan_describe(plan);

        assert(HAS(text, expected->url) && HAS(text, expected->sha256) && HAS(text, " MB") && HAS(text, "password di amministratore"));
        g_free(text);
        dep_plan_free(plan);
        deps_report_free(report);
        fake_free(f);
    }

    /* Windows senza GStreamer mancante (solo ffmpeg): istruzioni, nulla da installare da soli. */
    FakeSystem *f = fake_new(NULL, NULL);
    GError *error = NULL;

    g_hash_table_remove(f->programs, "ffmpeg");
    DepsReport *report = make_report(DEPS_PLATFORM_WINDOWS, f);

    assert(dep_plan_new(report, NULL, &error) == NULL);
    assert(error_is(error, DEP_INSTALLER_ERROR_NOTHING_TO_DO));
    g_clear_error(&error);
    deps_report_free(report);
    fake_free(f);
}

static void
test_plan_third_party_repositories(void)
{
    FakeSystem *f = fake_new("dnf", H264_DECODERS);
    DepsReport *report = make_report(DEPS_PLATFORM_LINUX, f);
    GError *error = NULL;
    char *fedora = write_os_release("fedora", "40");

    /* Senza il consenso dedicato: nessun passo di terze parti (e nient'altro da installare). */
    DepPlanOptions options = { FALSE, FALSE, fedora };

    assert(dep_plan_new(report, &options, &error) == NULL);
    assert(error_is(error, DEP_INSTALLER_ERROR_NOTHING_TO_DO));
    g_clear_error(&error);

    /* Con il consenso: repository RPM Fusion + gstreamer1-libav, entrambi marcati di terze parti. */
    options.allow_third_party = TRUE;

    DepPlan *plan = dep_plan_new(report, &options, &error);

    if (!plan || dep_plan_step_count(plan) != 2) {
        char *dump = deps_report_to_text(report);

        g_printerr("piano inatteso (%s), passi=%zu\n%s\n", error ? error->message : "ok", dep_plan_step_count(plan), dump);
        g_free(dump);
    }
    assert(plan && dep_plan_step_count(plan) == 2 && dep_plan_has_third_party(plan));

    const DepStep *repo = dep_plan_step(plan, 0), *packages = dep_plan_step(plan, 1);

    assert(repo->kind == DEP_STEP_ADD_REPOSITORY && repo->third_party && repo->needs_elevation);
    assert(strcmp(repo->argv[0], "dnf") == 0 && strcmp(repo->argv[1], "install") == 0 && strcmp(repo->argv[2], "-y") == 0);
    assert(strcmp(repo->argv[3], "https://mirrors.rpmfusion.org/free/fedora/rpmfusion-free-release-40.noarch.rpm") == 0);
    assert(repo->argv[4] == NULL);
    assert(packages->kind == DEP_STEP_SYSTEM_PACKAGES && packages->third_party);
    assert(strcmp(packages->argv[3], "gstreamer1-libav") == 0 && packages->argv[4] == NULL);

    char *text = dep_plan_describe(plan);

    assert(HAS(text, "terze parti") && HAS(text, "non è garantito") && HAS(text, "RPM Fusion"));
    g_free(text);
    dep_plan_free(plan);

    /* Altre distribuzioni (openSUSE/Packman non si automatizza) e VERSION_ID sospetto: nessun passo. */
    char *suse = write_os_release("opensuse-tumbleweed", "20260101");
    char *evil = write_os_release("fedora", "40; rm -rf /");
    char *nothing = g_strdup_printf("%s/non-esiste", root);
    const char *paths[] = { suse, evil, nothing };

    for (size_t i = 0; i < G_N_ELEMENTS(paths); i++) {
        options.os_release_path = paths[i];
        assert(dep_plan_new(report, &options, &error) == NULL);
        assert(error_is(error, DEP_INSTALLER_ERROR_NOTHING_TO_DO));
        g_clear_error(&error);
    }

    g_free(nothing);
    g_free(evil);
    g_free(suse);
    g_free(fedora);
    deps_report_free(report);
    fake_free(f);
}

/* ------------------------------------------------------------------ */
/* pkexec finto                                                        */
/* ------------------------------------------------------------------ */

typedef struct {
    GPtrArray *events;      /* "tipo:riga" */
    GCancellable *cancel_on_output;  /* se non NULL, annulla alla prima riga che contiene `cancel_marker` */
    const char *cancel_marker;
} Recorder;

static void
record(const DepProgress *p, gpointer data)
{
    Recorder *r = data;
    const char *kinds[] = { "start", "out", "dl", "verify", "end" };

    g_ptr_array_add(r->events, g_strdup_printf("%s:%zu/%zu:%s", kinds[p->kind], p->step_index, p->step_count, p->line ? p->line : ""));
    if (r->cancel_on_output && p->kind == DEP_PROGRESS_OUTPUT && HAS(p->line, r->cancel_marker)) {
        g_cancellable_cancel(r->cancel_on_output);
    }
}

static Recorder *
recorder_new(void)
{
    Recorder *r = g_new0(Recorder, 1);

    r->events = g_ptr_array_new_with_free_func(g_free);
    return r;
}

static void
recorder_free(Recorder *r)
{
    g_ptr_array_free(r->events, TRUE);
    g_free(r);
}

static gboolean
recorder_has(Recorder *r, const char *needle)
{
    for (guint i = 0; i < r->events->len; i++) {
        if (HAS((const char *)g_ptr_array_index(r->events, i), needle)) {
            return TRUE;
        }
    }
    return FALSE;
}

static char *fake_pkexec_path;
static char *fake_log_path;
static char *fake_pid_path;

static void
setup_fake_pkexec(void)
{
    fake_pkexec_path = g_strdup_printf("%s/fake-pkexec.sh", root);
    fake_log_path = g_strdup_printf("%s/fake-pkexec.log", root);
    fake_pid_path = g_strdup_printf("%s/fake-pkexec.pid", root);

    const char *script =
        "#!/bin/sh\n"
        "printf '%s\\n' \"$@\" >> \"$FAKE_LOG\"\n"
        "echo '--' >> \"$FAKE_LOG\"\n"
        "echo $$ > \"$FAKE_PID\"\n"
        "case \"$FAKE_MODE\" in\n"
        "  ok) echo 'scarico i pacchetti'; echo 'installazione completata'; exit 0;;\n"
        "  fail) echo 'errore: pacchetto non trovato'; echo 'transazione annullata'; exit 1;;\n"
        "  denied) exit 126;;\n"
        "  dismissed) exit 127;;\n"
        "  hang) echo 'avvio'; exec sleep 30;;\n"
        "esac\n"
        "exit 3\n";

    assert(g_file_set_contents(fake_pkexec_path, script, -1, NULL));
    assert(g_chmod(fake_pkexec_path, 0755) == 0);
    g_setenv("FAKE_LOG", fake_log_path, TRUE);
    g_setenv("FAKE_PID", fake_pid_path, TRUE);
}

static char *
fake_log(void)
{
    char *text = NULL;

    return g_file_get_contents(fake_log_path, &text, NULL, NULL) ? text : NULL;
}

static void
fake_reset(const char *mode)
{
    g_remove(fake_log_path);
    g_remove(fake_pid_path);
    g_setenv("FAKE_MODE", mode, TRUE);
}

static gboolean
fake_process_gone(void)
{
    char *text = NULL;

    if (!g_file_get_contents(fake_pid_path, &text, NULL, NULL)) {
        return TRUE;  /* non è mai partito */
    }
    long pid = atol(text);

    g_free(text);
#ifdef G_OS_UNIX
    return kill((pid_t)pid, 0) != 0;  /* il processo non esiste più */
#else
    return TRUE;
#endif
}

static DepPlan *
linux_plan(void)
{
    const char *missing[] = { "gtk4paintablesink", NULL };
    FakeSystem *f = fake_new("pacman", missing);
    DepsReport *report = make_report(DEPS_PLATFORM_LINUX, f);
    DepPlan *plan = dep_plan_new(report, NULL, NULL);

    assert(plan != NULL);
    deps_report_free(report);
    fake_free(f);
    return plan;
}

static void
test_run_with_fake_pkexec(void)
{
    DepPlan *plan = linux_plan();
    GError *error = NULL;
    Recorder *rec = recorder_new();
    DepRunOptions options = { 0 };

    options.elevation_program = fake_pkexec_path;
    options.progress = record;
    options.user_data = rec;

    /* Successo: argv esatto (pkexec + gestore + opzioni + pacchetti), output in tempo reale, eventi nell'ordine giusto. */
    fake_reset("ok");
    assert(dep_installer_run(plan, &options, &error) && error == NULL);

    char *log = fake_log();

    assert(log && strcmp(log, "pacman\n-S\n--noconfirm\n--needed\ngst-plugin-gtk4\n--\n") == 0);
    g_free(log);
    assert(rec->events->len == 4);
    assert(strcmp(g_ptr_array_index(rec->events, 0), "start:0/1:") == 0);
    assert(strcmp(g_ptr_array_index(rec->events, 1), "out:0/1:scarico i pacchetti") == 0);
    assert(strcmp(g_ptr_array_index(rec->events, 2), "out:0/1:installazione completata") == 0);
    assert(strcmp(g_ptr_array_index(rec->events, 3), "end:0/1:") == 0);
    assert(fake_process_gone());

    /* Errore del gestore: codice e ultime righe nel messaggio. */
    fake_reset("fail");
    assert(!dep_installer_run(plan, &options, &error));
    assert(error_is(error, DEP_INSTALLER_ERROR_FAILED));
    assert(HAS(error->message, "codice 1") && HAS(error->message, "pacchetto non trovato") && HAS(error->message, "transazione annullata"));
    g_clear_error(&error);

    /* Autorizzazione negata (126) o finestra chiusa (127): DENIED, un solo tentativo (nessun nuovo giro automatico). */
    const char *modes[] = { "denied", "dismissed" };

    for (size_t i = 0; i < G_N_ELEMENTS(modes); i++) {
        fake_reset(modes[i]);
        assert(!dep_installer_run(plan, &options, &error));
        assert(error_is(error, DEP_INSTALLER_ERROR_DENIED));
        assert(HAS(error->message, "nessuna modifica"));
        g_clear_error(&error);

        log = fake_log();
        assert(log && strcmp(log, "pacman\n-S\n--noconfirm\n--needed\ngst-plugin-gtk4\n--\n") == 0);  /* lanciato UNA volta */
        g_free(log);
    }

    /* Nessun pkexec: errore chiaro e nessun processo. */
    DepRunOptions no_elevation = { 0 };
    char *saved_path = g_strdup(g_getenv("PATH"));

    g_setenv("PATH", "/nonexistent", TRUE);
    fake_reset("ok");
    assert(!dep_installer_run(plan, &no_elevation, &error));
    assert(error_is(error, DEP_INSTALLER_ERROR_NO_ELEVATION));
    g_clear_error(&error);
    g_setenv("PATH", saved_path, TRUE);
    g_free(saved_path);
    assert(fake_log() == NULL);

    /* Programma di elevazione che non esiste (percorso sbagliato): FAILED, nessun crash. */
    DepRunOptions wrong = { 0 };

    wrong.elevation_program = "/nonexistent/pkexec";
    assert(!dep_installer_run(plan, &wrong, &error));
    assert(error_is(error, DEP_INSTALLER_ERROR_FAILED));
    g_clear_error(&error);

    recorder_free(rec);
    dep_plan_free(plan);
}

static void
test_cancel_and_timeout(void)
{
    DepPlan *plan = linux_plan();
    GError *error = NULL;
    GTimer *timer = g_timer_new();

    /* Annullamento a metà: il processo viene terminato, l'esito è CANCELLED e non resta nulla in esecuzione. */
    GCancellable *cancel = g_cancellable_new();
    Recorder *rec = recorder_new();
    DepRunOptions options = { 0 };

    options.elevation_program = fake_pkexec_path;
    options.progress = record;
    options.user_data = rec;
    options.cancellable = cancel;
    rec->cancel_on_output = cancel;
    rec->cancel_marker = "avvio";

    fake_reset("hang");
    g_timer_start(timer);
    assert(!dep_installer_run(plan, &options, &error));
    assert(error_is(error, DEP_INSTALLER_ERROR_CANCELLED));
    assert(g_timer_elapsed(timer, NULL) < 10.0);  /* non ha atteso i 30 secondi del processo finto */
    assert(fake_process_gone());
    assert(!recorder_has(rec, "end:"));  /* il passo non risulta concluso */
    g_clear_error(&error);
    g_object_unref(cancel);
    recorder_free(rec);

    /* Già annullato prima di partire: nemmeno un processo. */
    cancel = g_cancellable_new();
    g_cancellable_cancel(cancel);
    DepRunOptions pre = { 0 };

    pre.elevation_program = fake_pkexec_path;
    pre.cancellable = cancel;
    fake_reset("ok");
    assert(!dep_installer_run(plan, &pre, &error));
    assert(error_is(error, DEP_INSTALLER_ERROR_CANCELLED));
    assert(fake_log() == NULL);
    g_clear_error(&error);
    g_object_unref(cancel);

    /* Timeout del passo: processo terminato, TIMEOUT. */
    DepRunOptions timeout = { 0 };

    timeout.elevation_program = fake_pkexec_path;
    timeout.step_timeout_seconds = 1;
    fake_reset("hang");
    g_timer_start(timer);
    assert(!dep_installer_run(plan, &timeout, &error));
    assert(error_is(error, DEP_INSTALLER_ERROR_TIMEOUT));
    assert(g_timer_elapsed(timer, NULL) < 10.0);
    assert(fake_process_gone());
    g_clear_error(&error);

    g_timer_destroy(timer);
    dep_plan_free(plan);
}

static void
test_third_party_consent_is_enforced_at_run(void)
{
    FakeSystem *f = fake_new("dnf", H264_DECODERS);
    DepsReport *report = make_report(DEPS_PLATFORM_LINUX, f);
    char *fedora = write_os_release("fedora", "41");
    DepPlanOptions plan_options = { FALSE, TRUE, fedora };
    GError *error = NULL;
    DepPlan *plan = dep_plan_new(report, &plan_options, &error);

    assert(plan != NULL);

    DepRunOptions options = { 0 };

    options.elevation_program = fake_pkexec_path;

    /* Il piano c'è, ma senza il consenso dedicato nell'esecuzione non parte NULLA. */
    fake_reset("ok");
    assert(!dep_installer_run(plan, &options, &error));
    assert(error_is(error, DEP_INSTALLER_ERROR_CONSENT));
    g_clear_error(&error);
    assert(fake_log() == NULL);

    /* Con il consenso: i due comandi, nell'ordine (prima il repository, poi i pacchetti). */
    options.consent_third_party = TRUE;
    assert(dep_installer_run(plan, &options, &error));

    char *log = fake_log();

    assert(log && strcmp(log, "dnf\ninstall\n-y\nhttps://mirrors.rpmfusion.org/free/fedora/rpmfusion-free-release-41.noarch.rpm\n--\n"
                              "dnf\ninstall\n-y\ngstreamer1-libav\n--\n") == 0);
    g_free(log);

    /* Se il primo passo fallisce, il secondo non parte. */
    fake_reset("fail");
    assert(!dep_installer_run(plan, &options, &error));
    assert(error_is(error, DEP_INSTALLER_ERROR_FAILED));
    g_clear_error(&error);
    log = fake_log();
    assert(log && strstr(log, "gstreamer1-libav") == NULL);  /* solo il repository è stato tentato */
    g_free(log);

    g_free(fedora);
    dep_plan_free(plan);
    deps_report_free(report);
    fake_free(f);
}

static void
test_recheck_after_install(void)
{
    /* Dopo un'installazione riuscita il chiamante ricontrolla con deps_check: qui il sistema «guarisce». */
    const char *missing[] = { "gtk4paintablesink", NULL };
    FakeSystem *f = fake_new("pacman", missing);
    DepsReport *before = make_report(DEPS_PLATFORM_LINUX, f);

    assert(!deps_report_can_play(before));

    DepPlan *plan = dep_plan_new(before, NULL, NULL);
    DepRunOptions options = { 0 };

    options.elevation_program = fake_pkexec_path;
    fake_reset("ok");
    assert(dep_installer_run(plan, &options, NULL));
    g_hash_table_remove_all(f->missing_elements);  /* il pacchetto adesso c'è */

    DepsReport *after = make_report(DEPS_PLATFORM_LINUX, f);

    assert(deps_report_can_play(after) && deps_report_is_complete(after));
    deps_report_free(after);
    dep_plan_free(plan);
    deps_report_free(before);
    fake_free(f);
}

/* ------------------------------------------------------------------ */
/* Server HTTP locale (thread dedicato)                                */
/* ------------------------------------------------------------------ */

typedef struct {
    GThread *thread;
    GMutex lock;
    GCond cond;
    gboolean ready;
    guint port;
    GMainLoop *loop;
    GBytes *body;          /* contenuto di /ok */
    GBytes *big;           /* contenuto di /big (per l'annullamento) */
    GBytes *zip_good;
    GBytes *zip_evil;
    gint requests;
} TestServer;

static TestServer server;

static void
respond_chunked(SoupServerMessage *msg, const guchar *data, gsize length)
{
    soup_message_headers_set_encoding(soup_server_message_get_response_headers(msg), SOUP_ENCODING_CHUNKED);
    SoupMessageBody *body = soup_server_message_get_response_body(msg);

    soup_message_body_append(body, SOUP_MEMORY_COPY, data, length);
    soup_message_body_complete(body);
    soup_server_message_set_status(msg, SOUP_STATUS_OK, NULL);
}

static void
respond_plain(SoupServerMessage *msg, GBytes *bytes, gsize length)
{
    gsize size;
    const guchar *data = g_bytes_get_data(bytes, &size);

    soup_server_message_set_response(msg, "application/octet-stream", SOUP_MEMORY_COPY, (const char *)data, MIN(length, size));
    soup_server_message_set_status(msg, SOUP_STATUS_OK, NULL);
}

static void
server_handler(SoupServer *s, SoupServerMessage *msg, const char *path, GHashTable *query, gpointer user_data)
{
    gsize size;
    const guchar *body = g_bytes_get_data(server.body, &size);

    (void)s;
    (void)query;
    (void)user_data;
    g_atomic_int_inc(&server.requests);

    if (strcmp(path, "/ok") == 0) {
        respond_plain(msg, server.body, size);
    } else if (strcmp(path, "/chunked") == 0) {
        respond_chunked(msg, body, size);                    /* senza Content-Length */
    } else if (strcmp(path, "/short") == 0) {
        respond_plain(msg, server.body, size / 2);           /* Content-Length diverso da quello atteso */
    } else if (strcmp(path, "/truncated") == 0) {
        respond_chunked(msg, body, size / 2);                /* senza Content-Length, finisce prima */
    } else if (strcmp(path, "/longer") == 0) {
        guchar *extra = g_malloc(size + 1000);

        memcpy(extra, body, size);
        memset(extra + size, 'x', 1000);
        respond_chunked(msg, extra, size + 1000);            /* più dati del previsto */
        g_free(extra);
    } else if (strcmp(path, "/big") == 0) {
        gsize big_size;

        g_bytes_get_data(server.big, &big_size);
        respond_plain(msg, server.big, big_size);
    } else if (strcmp(path, "/big-chunked") == 0) {
        gsize big_size;
        const guchar *big = g_bytes_get_data(server.big, &big_size);

        respond_chunked(msg, big, big_size);                 /* 3 MB senza Content-Length */
    } else if (strcmp(path, "/zip-good") == 0) {
        gsize zip_size;

        g_bytes_get_data(server.zip_good, &zip_size);
        respond_plain(msg, server.zip_good, zip_size);
    } else if (strcmp(path, "/zip-evil") == 0) {
        gsize zip_size;

        g_bytes_get_data(server.zip_evil, &zip_size);
        respond_plain(msg, server.zip_evil, zip_size);
    } else {
        soup_server_message_set_status(msg, SOUP_STATUS_NOT_FOUND, NULL);
    }
}

static gpointer
server_thread(gpointer data)
{
    (void)data;
    GMainContext *context = g_main_context_new();

    g_main_context_push_thread_default(context);
    server.loop = g_main_loop_new(context, FALSE);

    SoupServer *s = soup_server_new(NULL, NULL);
    GError *error = NULL;

    soup_server_add_handler(s, "/", server_handler, NULL, NULL);
    assert(soup_server_listen_local(s, 0, SOUP_SERVER_LISTEN_IPV4_ONLY, &error));

    GSList *uris = soup_server_get_uris(s);

    server.port = g_uri_get_port(uris->data);
    g_slist_free_full(uris, (GDestroyNotify)g_uri_unref);

    g_mutex_lock(&server.lock);
    server.ready = TRUE;
    g_cond_signal(&server.cond);
    g_mutex_unlock(&server.lock);

    g_main_loop_run(server.loop);

    soup_server_disconnect(s);
    g_object_unref(s);
    g_main_loop_unref(server.loop);
    g_main_context_pop_thread_default(context);
    g_main_context_unref(context);
    return NULL;
}

static guchar *
pseudo_random(gsize length, guint seed)
{
    guchar *data = g_malloc(length);
    guint x = seed;

    for (gsize i = 0; i < length; i++) {
        x = x * 1664525u + 1013904223u;
        data[i] = (guchar)(x >> 24);
    }
    return data;
}

static void
server_start(void)
{
    guchar *body = pseudo_random(300 * 1024, 1);
    guchar *big = pseudo_random(3 * 1024 * 1024, 2);
    const guchar tool[] = "#!/bin/sh\necho ffmpeg finto\n";
    const guchar notes[] = "note della versione";
    const guchar evil[] = "dentro";
    ZipEntry good[] = {
        ENTRY("ffmpeg-1.0/", NULL, 0, 0, 0, 040755, -1, FALSE),
        ENTRY("ffmpeg-1.0/bin/ffmpeg", tool, sizeof tool - 1, 8, 0, 0100755, -1, FALSE),
        ENTRY("ffmpeg-1.0/LEGGIMI.txt", notes, sizeof notes - 1, 0, 0, 0100644, -1, FALSE),
    };
    ZipEntry bad[] = {
        ENTRY("ffmpeg-1.0/bin/ffmpeg", tool, sizeof tool - 1, 8, 0, 0100755, -1, FALSE),
        ENTRY("ffmpeg-1.0/../../evil.txt", evil, sizeof evil - 1, 0, 0, 0100644, -1, FALSE),
    };

    server.body = g_bytes_new_take(body, 300 * 1024);
    server.big = g_bytes_new_take(big, 3 * 1024 * 1024);
    server.zip_good = zip_build(good, G_N_ELEMENTS(good));
    server.zip_evil = zip_build(bad, G_N_ELEMENTS(bad));
    g_mutex_init(&server.lock);
    g_cond_init(&server.cond);
    server.thread = g_thread_new("test-http-server", server_thread, NULL);

    g_mutex_lock(&server.lock);
    while (!server.ready) {
        g_cond_wait(&server.cond, &server.lock);
    }
    g_mutex_unlock(&server.lock);
}

static void
server_stop(void)
{
    g_main_loop_quit(server.loop);
    g_thread_join(server.thread);
    g_bytes_unref(server.body);
    g_bytes_unref(server.big);
    g_bytes_unref(server.zip_good);
    g_bytes_unref(server.zip_evil);
}

static DepArtifact
make_artifact(const char *path, GBytes *content, DepArtifactKind kind, char **keep_alive)
{
    gsize size;
    const guchar *data = g_bytes_get_data(content, &size);
    DepArtifact a = { 0 };

    keep_alive[0] = g_strdup_printf("http://127.0.0.1:%u%s", server.port, path);
    keep_alive[1] = g_compute_checksum_for_data(G_CHECKSUM_SHA256, data, size);
    a.id = "prova";
    a.kind = kind;
    a.platform = DEPS_PLATFORM_LINUX;
    a.arch = "x86_64";
    a.version = "1";
    a.url = keep_alive[0];
    a.sha256 = keep_alive[1];
    a.size_bytes = (gint64)size;
    a.filename = "prova.bin";
    a.install_subdir = "bin";
    a.strip_components = 1;
    a.title = "Artefatto di prova";
    return a;
}

static gboolean
no_residue(const char *dest)
{
    char *part = g_strconcat(dest, ".part", NULL);
    gboolean clean = !g_file_test(dest, G_FILE_TEST_EXISTS) && !g_file_test(part, G_FILE_TEST_EXISTS);

    g_free(part);
    return clean;
}

static void
test_download_ok_and_reuse(void)
{
    char *dir = new_dir("dl");
    char *dest = g_build_filename(dir, "file.bin", NULL);
    char *keep[2];
    DepArtifact a = make_artifact("/ok", server.body, DEP_ARTIFACT_PLATFORM_INSTALLER, keep);
    DepRunOptions options = { 0 };
    GError *error = NULL;
    Recorder *rec = recorder_new();

    options.allow_loopback_http = TRUE;
    options.progress = record;
    options.user_data = rec;

    assert(dep_installer_download(&a, dest, &options, &error) && error == NULL);
    assert(g_file_test(dest, G_FILE_TEST_IS_REGULAR));

    char *part = g_strconcat(dest, ".part", NULL);

    assert(!g_file_test(part, G_FILE_TEST_EXISTS));
    g_free(part);
    assert(recorder_has(rec, "dl:") && recorder_has(rec, "verify:"));

    /* Contenuto identico. */
    gsize size;
    char *contents = NULL;

    assert(g_file_get_contents(dest, &contents, &size, NULL) && size == 300 * 1024);
    g_free(contents);

    /* Già scaricato e corretto: nessuna nuova richiesta di rete. */
    gint before = g_atomic_int_get(&server.requests);

    assert(dep_installer_download(&a, dest, &options, &error));
    assert(g_atomic_int_get(&server.requests) == before);

    /* Senza Content-Length (chunked) funziona lo stesso, verificando alla fine. */
    g_remove(dest);
    char *keep2[2];
    DepArtifact chunked = make_artifact("/chunked", server.body, DEP_ARTIFACT_PLATFORM_INSTALLER, keep2);

    assert(dep_installer_download(&chunked, dest, &options, &error));
    g_free(keep2[0]);
    g_free(keep2[1]);

    /* File presente ma corrotto: viene riscaricato. */
    assert(g_file_set_contents(dest, "corrotto", -1, NULL));
    assert(dep_installer_download(&a, dest, &options, &error));
    assert(g_atomic_int_get(&server.requests) > before);
    assert(g_file_get_contents(dest, &contents, &size, NULL) && size == 300 * 1024);
    g_free(contents);

    recorder_free(rec);
    g_free(keep[0]);
    g_free(keep[1]);
    g_free(dest);
    g_free(dir);
}

static void
expect_download_fails(const char *path, GBytes *content, const char *tweak, DepInstallerError code, const char *message_part)
{
    char *dir = new_dir("dlfail");
    char *dest = g_build_filename(dir, "file.bin", NULL);
    char *keep[2];
    DepArtifact a = make_artifact(path, content, DEP_ARTIFACT_PLATFORM_INSTALLER, keep);
    DepRunOptions options = { 0 };
    GError *error = NULL;

    options.allow_loopback_http = TRUE;
    if (tweak && strcmp(tweak, "hash") == 0) {
        a.sha256 = "0000000000000000000000000000000000000000000000000000000000000000";
    } else if (tweak && strcmp(tweak, "size") == 0) {
        a.size_bytes += 5;
    }
    assert(!dep_installer_download(&a, dest, &options, &error));
    assert(error_is(error, code));
    if (message_part && !HAS(error->message, message_part)) {
        g_printerr("messaggio inatteso: %s (atteso «%s»)\n", error->message, message_part);
        abort();
    }
    assert(no_residue(dest));  /* né il file né un .part */
    g_clear_error(&error);
    g_free(keep[0]);
    g_free(keep[1]);
    g_free(dest);
    g_free(dir);
}

static void
test_download_rejections(void)
{
    expect_download_fails("/ok", server.body, "hash", DEP_INSTALLER_ERROR_VERIFY, "SHA-256");       /* SHA-256 diverso */
    expect_download_fails("/ok", server.body, "size", DEP_INSTALLER_ERROR_VERIFY, "dimensione");    /* dimensione dichiarata diversa */
    expect_download_fails("/short", server.body, NULL, DEP_INSTALLER_ERROR_VERIFY, "dimensione");   /* Content-Length diverso dall'atteso */
    expect_download_fails("/truncated", server.body, NULL, DEP_INSTALLER_ERROR_VERIFY, "troncato"); /* finisce prima (chunked) */
    expect_download_fails("/longer", server.body, NULL, DEP_INSTALLER_ERROR_VERIFY, "più dati");    /* più dati del previsto */
    expect_download_fails("/non-esiste", server.body, NULL, DEP_INSTALLER_ERROR_DOWNLOAD, "404");   /* HTTP 404 */

    /* URL non consentiti: rifiutati subito, senza rete. */
    char *dir = new_dir("urls");
    char *dest = g_build_filename(dir, "file.bin", NULL);
    char *keep[2];
    DepArtifact a = make_artifact("/ok", server.body, DEP_ARTIFACT_PLATFORM_INSTALLER, keep);
    DepRunOptions strict = { 0 };  /* niente loopback: solo https */
    GError *error = NULL;
    gint before = g_atomic_int_get(&server.requests);

    assert(!dep_installer_download(&a, dest, &strict, &error));  /* http su loopback senza consenso di prova */
    assert(error_is(error, DEP_INSTALLER_ERROR_DOWNLOAD) && HAS(error->message, "https"));
    g_clear_error(&error);

    const char *bad_urls[] = { "http://example.com/x.exe", "ftp://example.com/x.exe", "file:///etc/passwd", "gopher://x/y", "javascript:alert(1)" };
    DepRunOptions loose = { 0 };

    loose.allow_loopback_http = TRUE;
    for (size_t i = 0; i < G_N_ELEMENTS(bad_urls); i++) {
        DepArtifact b = a;

        b.url = bad_urls[i];
        assert(!dep_installer_download(&b, dest, &loose, &error));
        assert(error_is(error, DEP_INSTALLER_ERROR_DOWNLOAD));
        g_clear_error(&error);
    }
    assert(g_atomic_int_get(&server.requests) == before);  /* nessuna richiesta è partita */
    assert(no_residue(dest));

    /* Server irraggiungibile: errore di rete pulito. */
    DepArtifact down = a;

    down.url = "http://127.0.0.1:1/x";
    assert(!dep_installer_download(&down, dest, &loose, &error));
    assert(error_is(error, DEP_INSTALLER_ERROR_DOWNLOAD));
    g_clear_error(&error);
    assert(no_residue(dest));

    g_free(keep[0]);
    g_free(keep[1]);
    g_free(dest);
    g_free(dir);
}

static void
test_download_already_cancelled(void)
{
    char *dir = new_dir("dlcancel");
    char *dest = g_build_filename(dir, "file.bin", NULL);
    char *keep[2];
    DepArtifact a = make_artifact("/big", server.big, DEP_ARTIFACT_PLATFORM_INSTALLER, keep);
    DepRunOptions options = { 0 };
    GCancellable *cancel = g_cancellable_new();
    GError *error = NULL;
    gint before = g_atomic_int_get(&server.requests);

    options.allow_loopback_http = TRUE;
    options.cancellable = cancel;
    g_cancellable_cancel(cancel);  /* già annullato prima di partire */
    assert(!dep_installer_download(&a, dest, &options, &error));
    assert(error_is(error, DEP_INSTALLER_ERROR_CANCELLED));
    assert(no_residue(dest));
    g_clear_error(&error);
    (void)before;

    g_object_unref(cancel);
    g_free(keep[0]);
    g_free(keep[1]);
    g_free(dest);
    g_free(dir);
}

typedef struct {
    gint64 max_reported;
} MaxBytes;

static void
track_max_bytes(const DepProgress *p, gpointer data)
{
    MaxBytes *m = data;

    if (p->kind == DEP_PROGRESS_DOWNLOAD && p->bytes_done > m->max_reported) {
        m->max_reported = p->bytes_done;
    }
}

/* Un server che invia molto più del previsto (chunked, senza Content-Length): il download si ferma subito oltre la dimensione attesa. */
static void
test_download_oversize_stops_early(void)
{
    char *dir = new_dir("dlover");
    char *dest = g_build_filename(dir, "file.bin", NULL);
    char *keep[2];
    DepArtifact a = make_artifact("/big-chunked", server.body, DEP_ARTIFACT_PLATFORM_INSTALLER, keep);  /* attesi 300 KB, ne arrivano 3 MB */
    MaxBytes max = { 0 };
    DepRunOptions options = { 0 };
    GError *error = NULL;

    options.allow_loopback_http = TRUE;
    options.progress = track_max_bytes;
    options.user_data = &max;
    assert(!dep_installer_download(&a, dest, &options, &error));
    assert(error_is(error, DEP_INSTALLER_ERROR_VERIFY) && HAS(error->message, "più dati"));
    assert(max.max_reported <= a.size_bytes);  /* non ha letto oltre la dimensione attesa */
    assert(no_residue(dest));
    g_clear_error(&error);
    g_free(keep[0]);
    g_free(keep[1]);
    g_free(dest);
    g_free(dir);
}

typedef struct {
    GCancellable *cancel;
    gint64 threshold;
    gboolean fired;
} CancelAt;

static void
cancel_at_progress(const DepProgress *p, gpointer data)
{
    CancelAt *c = data;

    if (p->kind == DEP_PROGRESS_DOWNLOAD && p->bytes_done >= c->threshold && !c->fired) {
        c->fired = TRUE;
        g_cancellable_cancel(c->cancel);
    }
}

static void
test_download_cancel_midway(void)
{
    char *dir = new_dir("dlmid");
    char *dest = g_build_filename(dir, "file.bin", NULL);
    char *keep[2];
    DepArtifact a = make_artifact("/big", server.big, DEP_ARTIFACT_PLATFORM_INSTALLER, keep);
    GCancellable *cancel = g_cancellable_new();
    CancelAt at = { cancel, 512 * 1024, FALSE };
    DepRunOptions options = { 0 };
    GError *error = NULL;

    options.allow_loopback_http = TRUE;
    options.cancellable = cancel;
    options.progress = cancel_at_progress;
    options.user_data = &at;

    assert(!dep_installer_download(&a, dest, &options, &error));
    assert(at.fired);  /* l'annullamento è avvenuto davvero a metà download */
    assert(error_is(error, DEP_INSTALLER_ERROR_CANCELLED));
    assert(no_residue(dest));  /* niente file né .part */
    g_clear_error(&error);

    g_object_unref(cancel);
    g_free(keep[0]);
    g_free(keep[1]);
    g_free(dest);
    g_free(dir);
}

/* ------------------------------------------------------------------ */
/* Installer di piattaforma e archivi, con esecuzione del piano        */
/* ------------------------------------------------------------------ */

typedef struct {
    int calls;
    char *seen_path;
    gint64 seen_size;
    gboolean deny;
    gboolean file_existed;
} InstallerSpy;

static gboolean
spy_installer(const char *path, GCancellable *cancellable, GError **error, gpointer user_data)
{
    InstallerSpy *spy = user_data;
    char *contents = NULL;
    gsize size = 0;

    (void)cancellable;
    spy->calls++;
    g_free(spy->seen_path);
    spy->seen_path = g_strdup(path);
    spy->file_existed = g_file_get_contents(path, &contents, &size, NULL);
    spy->seen_size = (gint64)size;
    g_free(contents);
    if (spy->deny) {
        g_set_error_literal(error, DEP_INSTALLER_ERROR, DEP_INSTALLER_ERROR_DENIED, "autorizzazione negata (UAC)");
        return FALSE;
    }
    return TRUE;
}

static void
test_platform_installer_flow(void)
{
    char *deps = new_dir("deps-inst");
    char *keep[2];
    DepArtifact a = make_artifact("/ok", server.body, DEP_ARTIFACT_PLATFORM_INSTALLER, keep);
    InstallerSpy spy = { 0 };
    DepRunOptions options = { 0 };
    GError *error = NULL;
    Recorder *rec = recorder_new();
    DepPlan *plan = dep_plan_new_for_artifact(&a, &error);

    assert(plan && dep_plan_step(plan, 0)->kind == DEP_STEP_PLATFORM_INSTALLER && dep_plan_step(plan, 0)->needs_elevation);
    options.deps_dir = deps;
    options.allow_loopback_http = TRUE;
    options.run_installer = spy_installer;
    options.run_installer_data = &spy;
    options.progress = record;
    options.user_data = rec;

    /* Successo: scaricato, verificato, poi (solo allora) lanciato; il file scaricato viene tolto. */
    assert(dep_installer_run(plan, &options, &error));
    assert(spy.calls == 1 && spy.file_existed && spy.seen_size == 300 * 1024);
    assert(HAS(spy.seen_path, "downloads") && HAS(spy.seen_path, "prova.bin"));
    assert(!g_file_test(spy.seen_path, G_FILE_TEST_EXISTS));

    /* Autorizzazione negata dal sistema: l'esito è DENIED, il file scaricato non resta, nessun nuovo tentativo. */
    spy.deny = TRUE;
    assert(!dep_installer_run(plan, &options, &error));
    assert(error_is(error, DEP_INSTALLER_ERROR_DENIED));
    g_clear_error(&error);
    assert(spy.calls == 2 && !g_file_test(spy.seen_path, G_FILE_TEST_EXISTS));

    /* Hash sbagliato: l'installer NON viene mai lanciato. */
    DepArtifact tampered = a;

    tampered.sha256 = "1111111111111111111111111111111111111111111111111111111111111111";
    dep_plan_free(plan);
    plan = dep_plan_new_for_artifact(&tampered, &error);
    spy.deny = FALSE;
    assert(!dep_installer_run(plan, &options, &error));
    assert(error_is(error, DEP_INSTALLER_ERROR_VERIFY));
    g_clear_error(&error);
    assert(spy.calls == 2);  /* invariato */

    /* Voci non valide nel piano. */
    DepArtifact bad_name = a;

    bad_name.filename = "../fuori.exe";
    dep_plan_free(plan);
    assert(dep_plan_new_for_artifact(&bad_name, &error) == NULL);
    assert(error_is(error, DEP_INSTALLER_ERROR_INVALID));
    g_clear_error(&error);
    assert(dep_plan_new_for_artifact(NULL, &error) == NULL);
    g_clear_error(&error);

    recorder_free(rec);
    g_free(spy.seen_path);
    g_free(keep[0]);
    g_free(keep[1]);
    g_free(deps);
}

static void
test_archive_install_flow(void)
{
    char *deps = new_dir("deps-zip");
    char *keep[2], *keep_evil[2];
    DepArtifact good = make_artifact("/zip-good", server.zip_good, DEP_ARTIFACT_ARCHIVE, keep);
    DepArtifact evil = make_artifact("/zip-evil", server.zip_evil, DEP_ARTIFACT_ARCHIVE, keep_evil);
    DepRunOptions options = { 0 };
    GError *error = NULL;

    options.deps_dir = deps;
    options.allow_loopback_http = TRUE;

    DepPlan *plan = dep_plan_new_for_artifact(&good, &error);

    assert(plan && dep_plan_step(plan, 0)->kind == DEP_STEP_ARCHIVE && !dep_plan_step(plan, 0)->needs_elevation);
    assert(dep_installer_run(plan, &options, &error));  /* senza privilegi */

    /* Estratto in <deps>/bin con strip 1 (la cartella «ffmpeg-1.0» sparisce), eseguibile, senza residui. */
    char *tool = g_build_filename(deps, "bin", "bin", "ffmpeg", NULL);
    char *notes = g_build_filename(deps, "bin", "LEGGIMI.txt", NULL);

    assert(g_file_test(tool, G_FILE_TEST_IS_REGULAR) && g_file_test(notes, G_FILE_TEST_IS_REGULAR));
#ifdef G_OS_UNIX
    assert(g_access(tool, X_OK) == 0);
#endif
    GDir *dir = g_dir_open(deps, 0, NULL);
    const char *name;
    int entries = 0;

    while ((name = g_dir_read_name(dir)) != NULL) {
        entries++;
        assert(strcmp(name, "bin") == 0 || strcmp(name, "downloads") == 0);  /* nessuna cartella .staging/.old */
    }
    g_dir_close(dir);
    assert(entries >= 1);
    char *zip_left = g_build_filename(deps, "downloads", "prova.bin", NULL);

    assert(!g_file_test(zip_left, G_FILE_TEST_EXISTS));  /* l'archivio scaricato è stato tolto */
    g_free(zip_left);

    /* Archivio malevolo (zip-slip) con SHA-256 corretto: supera la verifica, ma l'estrazione lo rifiuta e la versione
     * precedente resta intatta, senza file fuori dalla cartella. */
    dep_plan_free(plan);
    plan = dep_plan_new_for_artifact(&evil, &error);
    assert(!dep_installer_run(plan, &options, &error));
    assert(error_is(error, DEP_INSTALLER_ERROR_EXTRACT));
    g_clear_error(&error);
    assert(g_file_test(tool, G_FILE_TEST_IS_REGULAR));  /* installazione precedente intatta */
    char *outside = g_build_filename(root, "evil.txt", NULL);
    char *outside2 = g_build_filename(deps, "evil.txt", NULL);

    assert(!g_file_test(outside, G_FILE_TEST_EXISTS) && !g_file_test(outside2, G_FILE_TEST_EXISTS));
    dir = g_dir_open(deps, 0, NULL);
    while ((name = g_dir_read_name(dir)) != NULL) {
        assert(strcmp(name, "bin") == 0 || strcmp(name, "downloads") == 0);  /* niente residui di staging */
    }
    g_dir_close(dir);

    /* Nuova versione dello stesso archivio: sostituisce l'installazione in modo atomico, senza residui. */
    dep_plan_free(plan);
    plan = dep_plan_new_for_artifact(&good, &error);
    assert(g_file_set_contents(notes, "vecchio", -1, NULL));
    assert(dep_installer_run(plan, &options, &error));
    char *text = NULL;

    assert(g_file_get_contents(notes, &text, NULL, NULL) && strcmp(text, "note della versione") == 0);
    g_free(text);

    g_free(outside);
    g_free(outside2);
    g_free(tool);
    g_free(notes);
    dep_plan_free(plan);
    for (int i = 0; i < 2; i++) {
        g_free(keep[i]);
        g_free(keep_evil[i]);
    }
    g_free(deps);
}

int
main(void)
{
    root = g_dir_make_tmp("syncview-installer-XXXXXX", NULL);
    assert(root != NULL);
    g_log_set_always_fatal(G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    test_manifest();
    test_package_argv();
    test_plan_linux_packages();
    test_plan_platform_installer();
    test_plan_third_party_repositories();

#ifdef G_OS_UNIX
    setup_fake_pkexec();
    test_run_with_fake_pkexec();
    test_cancel_and_timeout();
    test_third_party_consent_is_enforced_at_run();
    test_recheck_after_install();
#endif

    server_start();
    test_download_ok_and_reuse();
    test_download_rejections();
    test_download_already_cancelled();
    test_download_cancel_midway();
    test_download_oversize_stops_early();
    test_platform_installer_flow();
    test_archive_install_flow();
    server_stop();

    g_free(root);
    return 0;
}
