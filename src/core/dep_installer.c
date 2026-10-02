#include "core/dep_installer.h"

#include "core/dep_archive.h"
#include "core/logger.h"

#include <glib/gstdio.h>
#include <libsoup/soup.h>
#include <string.h>

#ifdef G_OS_UNIX
#include <signal.h>
#endif
#ifdef G_OS_WIN32
#include <windows.h>
#include <shellapi.h>
#endif

G_DEFINE_QUARK(syncview-dep-installer-error-quark, dep_installer_error)

#define DEFAULT_STEP_TIMEOUT_S 1800
#define DOWNLOAD_CHUNK 65536
#define PROGRESS_STEP_BYTES (256 * 1024)
#define OUTPUT_TAIL_LINES 6
#define REPO_FEDORA_ID "fedora"

struct DepPlan {
    GPtrArray *steps;  /* DepStep* */
};

static void
fail_with(GError **error, DepInstallerError code, const char *fmt, ...) G_GNUC_PRINTF(3, 4);

static void
fail_with(GError **error, DepInstallerError code, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    char *message = g_strdup_vprintf(fmt, args);

    va_end(args);
    g_set_error_literal(error, DEP_INSTALLER_ERROR, code, message);
    g_free(message);
}

/* ------------------------------------------------------------------ */
/* Comandi dei gestori di pacchetti                                    */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *program;
    const char *options[5];  /* opzioni non interattive, NULL-terminate */
} ManagerSyntax;

static const ManagerSyntax MANAGERS[] = {
    { "pacman", { "-S", "--noconfirm", "--needed", NULL } },
    { "apt-get", { "install", "-y", NULL } },
    { "dnf", { "install", "-y", NULL } },
    { "zypper", { "--non-interactive", "install", NULL } },
};

/* Nome di pacchetto plausibile: minuscole, cifre, «+._-», non inizia con «-» (non sarebbe confuso con un'opzione). */
static gboolean
package_name_is_wellformed(const char *name)
{
    if (!name || !g_ascii_isalnum(name[0]) || g_ascii_isupper(name[0])) {
        return FALSE;
    }
    for (const char *p = name; *p; p++) {
        if (!(g_ascii_islower(*p) || g_ascii_isdigit(*p) || *p == '+' || *p == '.' || *p == '_' || *p == '-')) {
            return FALSE;
        }
    }
    return strlen(name) <= 100;
}

static const ManagerSyntax *
find_manager(const char *program)
{
    for (size_t i = 0; program && i < G_N_ELEMENTS(MANAGERS); i++) {
        if (strcmp(MANAGERS[i].program, program) == 0) {
            return &MANAGERS[i];
        }
    }
    return NULL;
}

/* Costruisce l'argv <gestore> <opzioni> <extra...> — con `known` si impone la tabella di deps_check, altrimenti la lista fissa. */
static char **
build_argv(const ManagerSyntax *manager, const char *const *packages, GError **error)
{
    GPtrArray *argv = g_ptr_array_new();

    g_ptr_array_add(argv, g_strdup(manager->program));
    for (int i = 0; manager->options[i]; i++) {
        g_ptr_array_add(argv, g_strdup(manager->options[i]));
    }
    for (size_t i = 0; packages && packages[i]; i++) {
        g_ptr_array_add(argv, g_strdup(packages[i]));
    }
    g_ptr_array_add(argv, NULL);
    (void)error;
    return (char **)g_ptr_array_free(argv, FALSE);
}

char **
dep_installer_build_package_argv(const char *package_manager, const char *const *packages, GError **error)
{
    const ManagerSyntax *manager = find_manager(package_manager);

    if (!manager) {
        fail_with(error, DEP_INSTALLER_ERROR_INVALID, "gestore di pacchetti non supportato: «%s»",
                  package_manager ? package_manager : "(nessuno)");
        return NULL;
    }
    if (!packages || !packages[0]) {
        fail_with(error, DEP_INSTALLER_ERROR_INVALID, "nessun pacchetto da installare");
        return NULL;
    }
    for (size_t i = 0; packages[i]; i++) {
        if (!package_name_is_wellformed(packages[i])) {
            fail_with(error, DEP_INSTALLER_ERROR_INVALID, "nome di pacchetto non valido: «%s»", packages[i]);
            return NULL;
        }
        if (!deps_package_is_known(package_manager, packages[i])) {
            fail_with(error, DEP_INSTALLER_ERROR_INVALID, "il pacchetto «%s» non è nella tabella dei pacchetti per %s",
                      packages[i], package_manager);
            return NULL;
        }
    }
    return build_argv(manager, packages, error);
}

/* ------------------------------------------------------------------ */
/* Piano                                                               */
/* ------------------------------------------------------------------ */

static void
dep_step_free(gpointer data)
{
    DepStep *step = data;

    g_free(step->title);
    g_free(step->summary);
    g_strfreev(step->argv);
    g_free(step);
}

static DepPlan *
plan_new_empty(void)
{
    DepPlan *plan = g_new0(DepPlan, 1);

    plan->steps = g_ptr_array_new_with_free_func(dep_step_free);
    return plan;
}

void
dep_plan_free(DepPlan *plan)
{
    if (plan) {
        g_ptr_array_free(plan->steps, TRUE);
        g_free(plan);
    }
}

size_t
dep_plan_step_count(const DepPlan *plan)
{
    return plan ? plan->steps->len : 0;
}

const DepStep *
dep_plan_step(const DepPlan *plan, size_t index)
{
    return plan && index < plan->steps->len ? g_ptr_array_index(plan->steps, index) : NULL;
}

gboolean
dep_plan_needs_elevation(const DepPlan *plan)
{
    for (guint i = 0; plan && i < plan->steps->len; i++) {
        if (((DepStep *)g_ptr_array_index(plan->steps, i))->needs_elevation) {
            return TRUE;
        }
    }
    return FALSE;
}

gboolean
dep_plan_has_third_party(const DepPlan *plan)
{
    for (guint i = 0; plan && i < plan->steps->len; i++) {
        if (((DepStep *)g_ptr_array_index(plan->steps, i))->third_party) {
            return TRUE;
        }
    }
    return FALSE;
}

gint64
dep_plan_download_bytes(const DepPlan *plan)
{
    gint64 total = 0;

    for (guint i = 0; plan && i < plan->steps->len; i++) {
        total += ((DepStep *)g_ptr_array_index(plan->steps, i))->download_bytes;
    }
    return total;
}

char *
dep_plan_describe(const DepPlan *plan)
{
    GString *text = g_string_new(NULL);

    for (guint i = 0; plan && i < plan->steps->len; i++) {
        const DepStep *step = g_ptr_array_index(plan->steps, i);

        g_string_append_printf(text, "%u. %s%s\n%s\n", i + 1, step->title, step->third_party ? " (repository di terze parti)" : "",
                               step->summary);
        if (step->needs_elevation) {
            g_string_append(text, "   Il sistema chiederà la password di amministratore (SyncView non la vede).\n");
        }
        if (i + 1 < plan->steps->len) {
            g_string_append_c(text, '\n');
        }
    }
    return g_string_free(text, FALSE);
}

static DepStep *
plan_add(DepPlan *plan, DepStepKind kind, const char *title, char *summary, gboolean elevation, gboolean third_party)
{
    DepStep *step = g_new0(DepStep, 1);

    step->kind = kind;
    step->title = g_strdup(title);
    step->summary = summary;
    step->needs_elevation = elevation;
    step->third_party = third_party;
    g_ptr_array_add(plan->steps, step);
    return step;
}

static char *
format_size(gint64 bytes)
{
    if (bytes >= 1024 * 1024) {
        return g_strdup_printf("%.0f MB", bytes / (1024.0 * 1024.0));
    }
    return g_strdup_printf("%.0f KB", bytes / 1024.0);
}

static char *
artifact_summary(const DepArtifact *artifact)
{
    char *size = format_size(artifact->size_bytes);
    char *text = g_strdup_printf("   %s\n   Scaricato da: %s\n   Dimensione: %s · SHA-256: %s", artifact->title, artifact->url, size,
                                 artifact->sha256);

    g_free(size);
    return text;
}

/* Valore di una chiave di os-release senza virgolette, o NULL. */
static char *
os_release_value(const char *path, const char *key)
{
    char *contents = NULL;
    char *value = NULL;

    if (!g_file_get_contents(path ? path : "/etc/os-release", &contents, NULL, NULL)) {
        return NULL;
    }

    char **lines = g_strsplit(contents, "\n", -1);
    char *prefix = g_strdup_printf("%s=", key);

    for (int i = 0; lines[i] && !value; i++) {
        if (g_str_has_prefix(lines[i], prefix)) {
            value = g_strdup(lines[i] + strlen(prefix));
            g_strstrip(value);
            gsize len = strlen(value);

            if (len >= 2 && (value[0] == '"' || value[0] == '\'') && value[len - 1] == value[0]) {
                memmove(value, value + 1, len - 2);
                value[len - 2] = '\0';
            }
        }
    }
    g_free(prefix);
    g_strfreev(lines);
    g_free(contents);
    return value;
}

static gboolean
all_digits(const char *s)
{
    if (!s || !*s || strlen(s) > 4) {
        return FALSE;
    }
    for (; *s; s++) {
        if (!g_ascii_isdigit(*s)) {
            return FALSE;
        }
    }
    return TRUE;
}

/* Pacchetti di terze parti installabili su Fedora (RPM Fusion free): lista fissa, mai da input esterno. */
static const char *const FEDORA_THIRD_PARTY_PACKAGES[] = { "gstreamer1-libav", NULL };

static gboolean
add_fedora_third_party_steps(DepPlan *plan, const DepsReport *report, const DepPlanOptions *options, GError **error)
{
    const DepsItem *h264 = deps_report_find(report, "gst-decoder-h264");

    /* Solo se manca il decoder H.264 e l'unica via sono i repository di terze parti (istruzioni, non pacchetti). */
    if (!h264 || h264->status == DEPS_STATUS_OK || h264->resolution != DEPS_RESOLUTION_INSTRUCTIONS ||
        deps_report_get_platform(report) != DEPS_PLATFORM_LINUX) {
        return TRUE;
    }

    char *id = os_release_value(options->os_release_path, "ID");
    char *version = os_release_value(options->os_release_path, "VERSION_ID");
    gboolean fedora = id && strcmp(id, REPO_FEDORA_ID) == 0 && all_digits(version);

    if (fedora) {
        const ManagerSyntax *dnf = find_manager("dnf");
        char *url = g_strdup_printf("https://mirrors.rpmfusion.org/free/fedora/rpmfusion-free-release-%s.noarch.rpm", version);
        const char *repo_args[] = { url, NULL };
        DepStep *repo = plan_add(plan, DEP_STEP_ADD_REPOSITORY, "Aggiungi il repository RPM Fusion (free)",
                                 g_strdup_printf("   dnf installa: %s\n   RPM Fusion è un repository di terze parti, non testato né supportato da SyncView: "
                                                 "il funzionamento corretto dell'app con i suoi pacchetti non è garantito.", url),
                                 TRUE, TRUE);

        repo->argv = build_argv(dnf, repo_args, error);

        char *joined = g_strjoinv(", ", (char **)FEDORA_THIRD_PARTY_PACKAGES);
        DepStep *pkgs = plan_add(plan, DEP_STEP_SYSTEM_PACKAGES, "Installa il decoder H.264 da RPM Fusion",
                                 g_strdup_printf("   Pacchetti (dnf, da RPM Fusion): %s", joined), TRUE, TRUE);

        pkgs->argv = build_argv(dnf, FEDORA_THIRD_PARTY_PACKAGES, error);
        g_free(joined);
        g_free(url);
    }
    g_free(id);
    g_free(version);
    return TRUE;
}

DepPlan *
dep_plan_new(const DepsReport *report, const DepPlanOptions *options, GError **error)
{
    DepPlanOptions defaults = { 0 };
    DepPlan *plan = plan_new_empty();

    g_return_val_if_fail(report != NULL, NULL);
    if (!options) {
        options = &defaults;
    }

    /* 1. Pacchetti di sistema (Linux): un'unica transazione. */
    char *manager = NULL;
    char **packages = deps_report_collect_packages(report, options->include_optional, &manager);

    if (packages) {
        char **argv = dep_installer_build_package_argv(manager, (const char *const *)packages, error);

        if (!argv) {
            g_strfreev(packages);
            g_free(manager);
            dep_plan_free(plan);
            return NULL;
        }

        char *joined = g_strjoinv(", ", packages);
        DepStep *step = plan_add(plan, DEP_STEP_SYSTEM_PACKAGES, "Installa i pacchetti di sistema",
                                 g_strdup_printf("   Pacchetti (%s): %s", manager, joined), TRUE, FALSE);

        step->argv = argv;
        g_free(joined);
    }
    g_strfreev(packages);
    g_free(manager);

    /* 2. Repository di terze parti: solo con il consenso dedicato, e solo su Fedora (vedi dep_installer.h). */
    if (options->allow_third_party && !add_fedora_third_party_steps(plan, report, options, error)) {
        dep_plan_free(plan);
        return NULL;
    }

    /* 3. Installer ufficiale di GStreamer (Windows/macOS): un solo passo anche se i componenti sono più d'uno. */
    gboolean need_installer = FALSE;

    for (size_t i = 0; i < deps_report_count(report); i++) {
        const DepsItem *item = deps_report_get(report, i);

        if (item->status != DEPS_STATUS_OK && item->resolution == DEPS_RESOLUTION_PLATFORM_INSTALLER &&
            (item->feature != DEPS_FEATURE_OPTIONAL || options->include_optional)) {
            need_installer = TRUE;
        }
    }
    if (need_installer) {
        const DepArtifact *artifact = dep_manifest_gstreamer_installer(deps_report_get_platform(report));

        if (artifact && dep_artifact_is_well_formed(artifact)) {
            DepStep *step = plan_add(plan, DEP_STEP_PLATFORM_INSTALLER, "Installa GStreamer con l'installer ufficiale",
                                     artifact_summary(artifact), TRUE, FALSE);

            step->artifact = artifact;
            step->download_bytes = artifact->size_bytes;
        }
    }

    if (plan->steps->len == 0) {
        fail_with(error, DEP_INSTALLER_ERROR_NOTHING_TO_DO, "nulla che SyncView possa installare da solo");
        dep_plan_free(plan);
        return NULL;
    }
    return plan;
}

DepPlan *
dep_plan_new_for_artifact(const DepArtifact *artifact, GError **error)
{
    /* Gli URL di prova su loopback usano http: la forma si controlla al download, qui basta il resto. */
    if (!artifact || !artifact->id || !artifact->url || !artifact->sha256 || artifact->size_bytes <= 0 || !artifact->filename ||
        strchr(artifact->filename, '/') || strchr(artifact->filename, '\\') || strstr(artifact->filename, "..") ||
        strlen(artifact->sha256) != 64) {
        fail_with(error, DEP_INSTALLER_ERROR_INVALID, "voce del manifest non valida");
        return NULL;
    }

    DepPlan *plan = plan_new_empty();
    gboolean installer = artifact->kind == DEP_ARTIFACT_PLATFORM_INSTALLER;
    DepStep *step = plan_add(plan, installer ? DEP_STEP_PLATFORM_INSTALLER : DEP_STEP_ARCHIVE,
                             installer ? "Installa con l'installer ufficiale" : "Scarica ed estrai l'archivio",
                             artifact_summary(artifact), installer, FALSE);

    step->artifact = artifact;
    step->download_bytes = artifact->size_bytes;
    return plan;
}

/* ------------------------------------------------------------------ */
/* Progresso                                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    const DepRunOptions *options;
    size_t step_index;
    size_t step_count;
    const char *title;
} Reporter;

static void
report_progress(const Reporter *r, DepProgressKind kind, const char *line, gint64 done, gint64 total)
{
    if (r->options->progress) {
        DepProgress p = { kind, r->step_index, r->step_count, r->title, line, done, total };

        r->options->progress(&p, r->options->user_data);
    }
}

static gboolean
is_cancelled(const DepRunOptions *options)
{
    return options->cancellable && g_cancellable_is_cancelled(options->cancellable);
}

/* ------------------------------------------------------------------ */
/* Processo elevato                                                    */
/* ------------------------------------------------------------------ */

typedef struct {
    GSubprocess *process;
    GMutex lock;
    GCond cond;
    gboolean done;
    gboolean timed_out;
    gboolean cancelled;
    guint timeout_s;
} Watch;

static void
watch_terminate(Watch *watch)
{
    /*
     * Dopo l'autorizzazione il processo gira come root: l'invio del segnale da utente normale fallisce (e va bene così: non
     * si interrompe una transazione a metà). Prima, e con i processi finti dei test, la terminazione funziona.
     */
#ifdef G_OS_UNIX
    g_subprocess_send_signal(watch->process, SIGTERM);
#else
    g_subprocess_force_exit(watch->process);
#endif
}

static gpointer
watchdog_main(gpointer data)
{
    Watch *watch = data;
    gint64 deadline = g_get_monotonic_time() + (gint64)watch->timeout_s * G_USEC_PER_SEC;

    g_mutex_lock(&watch->lock);
    while (!watch->done) {
        if (!g_cond_wait_until(&watch->cond, &watch->lock, deadline)) {
            if (!watch->done) {
                watch->timed_out = TRUE;
                watch_terminate(watch);
            }
            break;
        }
    }
    g_mutex_unlock(&watch->lock);
    return NULL;
}

static void
on_cancelled(GCancellable *cancellable, gpointer data)
{
    Watch *watch = data;

    (void)cancellable;
    g_mutex_lock(&watch->lock);
    watch->cancelled = TRUE;
    if (!watch->done) {
        watch_terminate(watch);
    }
    g_mutex_unlock(&watch->lock);
}

static char *
output_tail(GQueue *lines)
{
    GString *text = g_string_new(NULL);

    for (GList *l = lines->head; l; l = l->next) {
        g_string_append_printf(text, "%s%s", text->len ? "\n" : "", (char *)l->data);
    }
    return g_string_free(text, FALSE);
}

static gboolean
run_elevated_command(const DepStep *step, const DepRunOptions *options, const Reporter *reporter, GError **error)
{
    char *elevation = options->elevation_program ? g_strdup(options->elevation_program) : g_find_program_in_path("pkexec");

    if (!elevation) {
        fail_with(error, DEP_INSTALLER_ERROR_NO_ELEVATION,
                  "pkexec (polkit) non è disponibile: installa i pacchetti a mano con le istruzioni del report");
        return FALSE;
    }

    GPtrArray *argv = g_ptr_array_new();

    g_ptr_array_add(argv, elevation);
    for (int i = 0; step->argv[i]; i++) {
        g_ptr_array_add(argv, step->argv[i]);
    }
    g_ptr_array_add(argv, NULL);

    GSubprocessLauncher *launcher =
        g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE);
    GError *spawn_error = NULL;
    GSubprocess *process = g_subprocess_launcher_spawnv(launcher, (const char *const *)argv->pdata, &spawn_error);

    g_object_unref(launcher);
    g_ptr_array_free(argv, TRUE);  /* solo l'array: le stringhe sono di `step` e `elevation` si libera sotto */

    if (!process) {
        fail_with(error, DEP_INSTALLER_ERROR_FAILED, "impossibile avviare «%s»: %s", elevation, spawn_error->message);
        g_error_free(spawn_error);
        g_free(elevation);
        return FALSE;
    }

    Watch watch;

    memset(&watch, 0, sizeof watch);
    watch.process = process;
    watch.timeout_s = options->step_timeout_seconds ? options->step_timeout_seconds : DEFAULT_STEP_TIMEOUT_S;
    g_mutex_init(&watch.lock);
    g_cond_init(&watch.cond);

    gulong cancel_id = options->cancellable ? g_cancellable_connect(options->cancellable, G_CALLBACK(on_cancelled), &watch, NULL) : 0;
    GThread *watchdog = g_thread_new("dep-watchdog", watchdog_main, &watch);

    /* Output in tempo reale, riga per riga; le ultime righe servono al messaggio d'errore. */
    GDataInputStream *reader = g_data_input_stream_new(g_subprocess_get_stdout_pipe(process));
    GQueue tail = G_QUEUE_INIT;
    char *raw;

    while ((raw = g_data_input_stream_read_line(reader, NULL, NULL, NULL)) != NULL) {
        char *line = g_utf8_make_valid(raw, -1);

        g_free(raw);
        g_strchomp(line);
        report_progress(reporter, DEP_PROGRESS_OUTPUT, line, 0, 0);
        g_queue_push_tail(&tail, line);
        if (g_queue_get_length(&tail) > OUTPUT_TAIL_LINES) {
            g_free(g_queue_pop_head(&tail));
        }
    }
    g_object_unref(reader);
    g_subprocess_wait(process, NULL, NULL);

    g_mutex_lock(&watch.lock);
    watch.done = TRUE;
    g_cond_signal(&watch.cond);
    g_mutex_unlock(&watch.lock);
    g_thread_join(watchdog);
    if (cancel_id) {
        g_cancellable_disconnect(options->cancellable, cancel_id);
    }

    gboolean exited = g_subprocess_get_if_exited(process);
    int status = exited ? g_subprocess_get_exit_status(process) : -1;
    gboolean ok = FALSE;
    char *tail_text = output_tail(&tail);

    if (status == 0) {
        ok = TRUE;
    } else if (watch.cancelled) {
        fail_with(error, DEP_INSTALLER_ERROR_CANCELLED, "operazione annullata (nessuna modifica completata dal passo interrotto)");
    } else if (watch.timed_out) {
        fail_with(error, DEP_INSTALLER_ERROR_TIMEOUT, "il passo non è terminato entro %u secondi", watch.timeout_s);
    } else if (status == 126 || status == 127) {
        fail_with(error, DEP_INSTALLER_ERROR_DENIED,
                  "autorizzazione negata o annullata (oppure comando non trovato): nessuna modifica al sistema");
    } else {
        fail_with(error, DEP_INSTALLER_ERROR_FAILED, "il comando è terminato con errore (codice %d)%s%s", status,
                  *tail_text ? ":\n" : "", tail_text);
    }

    g_free(tail_text);
    g_queue_clear_full(&tail, g_free);
    g_mutex_clear(&watch.lock);
    g_cond_clear(&watch.cond);
    g_object_unref(process);
    g_free(elevation);
    return ok;
}

/* ------------------------------------------------------------------ */
/* Download verificato                                                 */
/* ------------------------------------------------------------------ */

static gboolean
is_loopback_http(GUri *uri)
{
    const char *host = g_uri_get_host(uri);

    return g_strcmp0(g_uri_get_scheme(uri), "http") == 0 && host &&
           (strcmp(host, "127.0.0.1") == 0 || strcmp(host, "localhost") == 0 || strcmp(host, "::1") == 0);
}

static gboolean
uri_is_allowed(GUri *uri, const DepRunOptions *options)
{
    return g_strcmp0(g_uri_get_scheme(uri), "https") == 0 || (options->allow_loopback_http && is_loopback_http(uri));
}

/* SHA-256 di un file (esadecimale minuscolo) e dimensione; NULL se non leggibile. */
static char *
file_sha256(const char *path, gint64 *size)
{
    FILE *f = g_fopen(path, "rb");

    if (!f) {
        return NULL;
    }

    GChecksum *sum = g_checksum_new(G_CHECKSUM_SHA256);
    guchar buffer[DOWNLOAD_CHUNK];
    size_t n;

    *size = 0;
    while ((n = fread(buffer, 1, sizeof buffer, f)) > 0) {
        g_checksum_update(sum, buffer, (gssize)n);
        *size += (gint64)n;
    }
    fclose(f);

    char *hex = g_strdup(g_checksum_get_string(sum));

    g_checksum_free(sum);
    return hex;
}

static gboolean
enough_disk_space(const char *dir, gint64 needed)
{
    GFile *file = g_file_new_for_path(dir);
    GFileInfo *info = g_file_query_filesystem_info(file, G_FILE_ATTRIBUTE_FILESYSTEM_FREE, NULL, NULL);
    gboolean ok = TRUE;

    if (info && g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_FILESYSTEM_FREE)) {
        ok = (gint64)g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_FILESYSTEM_FREE) >= needed + needed / 10;
    }
    g_clear_object(&info);
    g_object_unref(file);
    return ok;
}

static gboolean
download_verified(const DepArtifact *artifact, const char *dest_path, const DepRunOptions *options, const Reporter *reporter,
                  GError **error)
{
    char *dir = g_path_get_dirname(dest_path);
    char *part = g_strconcat(dest_path, ".part", NULL);
    gboolean ok = FALSE;
    GChecksum *sum = NULL;
    FILE *out = NULL;
    SoupSession *session = NULL;
    SoupMessage *message = NULL;
    GInputStream *in = NULL;
    GUri *parsed = g_uri_parse(artifact->url, G_URI_FLAGS_NONE, NULL);

    if (!parsed || !uri_is_allowed(parsed, options)) {
        fail_with(error, DEP_INSTALLER_ERROR_DOWNLOAD, "URL non consentito (serve https): %s", artifact->url);
        goto done;
    }
    if (g_mkdir_with_parents(dir, 0700) != 0) {
        fail_with(error, DEP_INSTALLER_ERROR_DOWNLOAD, "impossibile creare la cartella %s", dir);
        goto done;
    }

    /* Già scaricato e corretto (per esempio un tentativo precedente): non si riscarica. */
    if (g_file_test(dest_path, G_FILE_TEST_IS_REGULAR)) {
        gint64 size = 0;
        char *hex = file_sha256(dest_path, &size);
        gboolean same = hex && size == artifact->size_bytes && g_ascii_strcasecmp(hex, artifact->sha256) == 0;

        g_free(hex);
        if (same) {
            report_progress(reporter, DEP_PROGRESS_DOWNLOAD, NULL, artifact->size_bytes, artifact->size_bytes);
            ok = TRUE;
            goto done;
        }
        g_remove(dest_path);
    }
    g_remove(part);  /* residuo di un tentativo interrotto */

    if (!enough_disk_space(dir, artifact->size_bytes)) {
        fail_with(error, DEP_INSTALLER_ERROR_DOWNLOAD, "spazio su disco insufficiente per %lld byte in %s",
                  (long long)artifact->size_bytes, dir);
        goto done;
    }

    session = soup_session_new();
    g_object_set(session, "user-agent", "SyncView-deps/1.0", "timeout", 30u, "idle-timeout", 30u, NULL);
    message = soup_message_new(SOUP_METHOD_GET, artifact->url);

    GError *soup_error = NULL;

    in = soup_session_send(session, message, options->cancellable, &soup_error);
    if (!in) {
        if (g_error_matches(soup_error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
            fail_with(error, DEP_INSTALLER_ERROR_CANCELLED, "download annullato");
        } else {
            fail_with(error, DEP_INSTALLER_ERROR_DOWNLOAD, "download non riuscito: %s", soup_error->message);
        }
        g_error_free(soup_error);
        goto done;
    }
    if (soup_message_get_status(message) != SOUP_STATUS_OK) {
        fail_with(error, DEP_INSTALLER_ERROR_DOWNLOAD, "il server ha risposto HTTP %u", soup_message_get_status(message));
        goto done;
    }
    if (!uri_is_allowed(soup_message_get_uri(message), options)) {  /* dopo i redirect */
        fail_with(error, DEP_INSTALLER_ERROR_DOWNLOAD, "reindirizzamento verso un URL non consentito");
        goto done;
    }

    goffset declared = soup_message_headers_get_content_length(soup_message_get_response_headers(message));

    if (declared > 0 && declared != artifact->size_bytes) {
        fail_with(error, DEP_INSTALLER_ERROR_VERIFY, "dimensione dichiarata dal server (%lld) diversa da quella attesa (%lld)",
                  (long long)declared, (long long)artifact->size_bytes);
        goto done;
    }

    out = g_fopen(part, "wb");
    if (!out) {
        fail_with(error, DEP_INSTALLER_ERROR_DOWNLOAD, "impossibile scrivere %s", part);
        goto done;
    }

    sum = g_checksum_new(G_CHECKSUM_SHA256);

    guchar buffer[DOWNLOAD_CHUNK];
    gint64 done_bytes = 0, next_report = 0;

    for (;;) {
        GError *read_error = NULL;
        gssize n = g_input_stream_read(in, buffer, sizeof buffer, options->cancellable, &read_error);

        if (n < 0) {
            if (g_error_matches(read_error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
                fail_with(error, DEP_INSTALLER_ERROR_CANCELLED, "download annullato");
            } else {
                fail_with(error, DEP_INSTALLER_ERROR_DOWNLOAD, "download interrotto: %s", read_error->message);
            }
            g_error_free(read_error);
            goto done;
        }
        if (n == 0) {
            break;
        }
        done_bytes += n;
        if (done_bytes > artifact->size_bytes) {  /* più dati del previsto: si ferma subito */
            fail_with(error, DEP_INSTALLER_ERROR_VERIFY, "il server invia più dati di quelli attesi (%lld byte)",
                      (long long)artifact->size_bytes);
            goto done;
        }
        g_checksum_update(sum, buffer, n);
        if (fwrite(buffer, 1, (size_t)n, out) != (size_t)n) {
            fail_with(error, DEP_INSTALLER_ERROR_DOWNLOAD, "scrittura non riuscita su %s", part);
            goto done;
        }
        if (done_bytes >= next_report) {
            report_progress(reporter, DEP_PROGRESS_DOWNLOAD, NULL, done_bytes, artifact->size_bytes);
            next_report = done_bytes + PROGRESS_STEP_BYTES;
        }
    }
    if (fclose(out) != 0) {
        out = NULL;
        fail_with(error, DEP_INSTALLER_ERROR_DOWNLOAD, "scrittura non riuscita su %s", part);
        goto done;
    }
    out = NULL;
    report_progress(reporter, DEP_PROGRESS_DOWNLOAD, NULL, done_bytes, artifact->size_bytes);

    report_progress(reporter, DEP_PROGRESS_VERIFYING, NULL, done_bytes, artifact->size_bytes);
    if (done_bytes != artifact->size_bytes) {
        fail_with(error, DEP_INSTALLER_ERROR_VERIFY, "file troncato: %lld byte ricevuti su %lld", (long long)done_bytes,
                  (long long)artifact->size_bytes);
        goto done;
    }
    if (g_ascii_strcasecmp(g_checksum_get_string(sum), artifact->sha256) != 0) {
        fail_with(error, DEP_INSTALLER_ERROR_VERIFY, "SHA-256 non corrispondente: file scartato");
        goto done;
    }
    if (g_rename(part, dest_path) != 0) {
        fail_with(error, DEP_INSTALLER_ERROR_DOWNLOAD, "impossibile rendere disponibile %s", dest_path);
        goto done;
    }
    ok = TRUE;

done:
    if (out) {
        fclose(out);
    }
    if (!ok) {
        g_remove(part);  /* nessun residuo se annullato o fallito */
    }
    if (sum) {
        g_checksum_free(sum);
    }
    g_clear_object(&in);
    g_clear_object(&message);
    g_clear_object(&session);
    if (parsed) {
        g_uri_unref(parsed);
    }
    g_free(part);
    g_free(dir);
    return ok;
}

gboolean
dep_installer_download(const DepArtifact *artifact, const char *dest_path, const DepRunOptions *options, GError **error)
{
    DepRunOptions defaults = { 0 };
    Reporter reporter;

    g_return_val_if_fail(artifact != NULL && dest_path != NULL, FALSE);
    if (!options) {
        options = &defaults;
    }
    reporter = (Reporter){ options, 0, 1, artifact->title ? artifact->title : "Download" };
    return download_verified(artifact, dest_path, options, &reporter, error);
}

/* ------------------------------------------------------------------ */
/* Lancio dell'installer di piattaforma                                */
/* ------------------------------------------------------------------ */

static gboolean
default_run_installer(const char *path, GCancellable *cancellable, GError **error, gpointer user_data)
{
    (void)cancellable;
    (void)user_data;

#if defined(G_OS_WIN32)
    /* UAC: «runas» fa chiedere a Windows la conferma/credenziali; l'app non vede nulla. */
    gunichar2 *wide_path = g_utf8_to_utf16(path, -1, NULL, NULL, NULL);
    SHELLEXECUTEINFOW info = { 0 };

    info.cbSize = sizeof info;
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = L"runas";
    info.lpFile = (LPCWSTR)wide_path;
    info.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&info)) {
        DWORD code = GetLastError();

        g_free(wide_path);
        if (code == ERROR_CANCELLED) {
            fail_with(error, DEP_INSTALLER_ERROR_DENIED, "autorizzazione negata o annullata (UAC): nessuna modifica al sistema");
        } else {
            fail_with(error, DEP_INSTALLER_ERROR_FAILED, "impossibile avviare l'installer (errore Windows %lu)", (unsigned long)code);
        }
        return FALSE;
    }
    g_free(wide_path);

    /* L'installer elevato non si può interrompere da qui: si attende che termini. */
    WaitForSingleObject(info.hProcess, INFINITE);

    DWORD exit_code = 1;

    GetExitCodeProcess(info.hProcess, &exit_code);
    CloseHandle(info.hProcess);
    if (exit_code != 0 && exit_code != 3010) {  /* 3010 = riavvio necessario */
        fail_with(error, DEP_INSTALLER_ERROR_FAILED, "l'installer è terminato con errore (codice %lu)", (unsigned long)exit_code);
        return FALSE;
    }
    return TRUE;
#elif defined(__APPLE__)
    /* `open -W` apre il .pkg con l'Installer di macOS e attende che si chiuda: l'autorizzazione la chiede macOS. */
    const char *argv[] = { "open", "-W", path, NULL };
    GSubprocess *process = g_subprocess_newv(argv, G_SUBPROCESS_FLAGS_NONE, error);

    if (!process) {
        return FALSE;
    }
    gboolean waited = g_subprocess_wait_check(process, NULL, error);

    g_object_unref(process);
    return waited;
#else
    (void)path;
    fail_with(error, DEP_INSTALLER_ERROR_UNSUPPORTED, "l'installer ufficiale non è previsto su questa piattaforma");
    return FALSE;
#endif
}

/* ------------------------------------------------------------------ */
/* Archivio: estrazione e installazione atomica                        */
/* ------------------------------------------------------------------ */

static void
remove_tree(const char *path)
{
    GDir *dir = g_dir_open(path, 0, NULL);

    if (dir) {
        const char *name;

        while ((name = g_dir_read_name(dir)) != NULL) {
            char *child = g_build_filename(path, name, NULL);

            if (g_file_test(child, G_FILE_TEST_IS_DIR) && !g_file_test(child, G_FILE_TEST_IS_SYMLINK)) {
                remove_tree(child);
            } else {
                g_remove(child);
            }
            g_free(child);
        }
        g_dir_close(dir);
    }
    g_rmdir(path);
}

static gboolean
install_archive(const DepArtifact *artifact, const char *zip_path, const char *deps_dir, GError **error)
{
    char *staging = g_strdup_printf("%s%c.staging-%u-%u", deps_dir, G_DIR_SEPARATOR, (unsigned)g_random_int(), (unsigned)g_get_monotonic_time());
    char *target = g_build_filename(deps_dir, artifact->install_subdir, NULL);
    char *old = NULL;
    GError *extract_error = NULL;
    gboolean ok = FALSE;

    if (g_mkdir_with_parents(staging, 0700) != 0) {
        fail_with(error, DEP_INSTALLER_ERROR_EXTRACT, "impossibile creare la cartella di appoggio");
        goto done;
    }
    if (!dep_archive_extract_zip(zip_path, staging, artifact->strip_components, 0, &extract_error)) {
        fail_with(error, DEP_INSTALLER_ERROR_EXTRACT, "archivio rifiutato: %s", extract_error->message);
        goto done;
    }

    char *target_parent = g_path_get_dirname(target);

    g_mkdir_with_parents(target_parent, 0700);
    g_free(target_parent);
    if (g_file_test(target, G_FILE_TEST_EXISTS)) {
        old = g_strdup_printf("%s.old-%u", target, (unsigned)g_random_int());
        if (g_rename(target, old) != 0) {
            fail_with(error, DEP_INSTALLER_ERROR_EXTRACT, "impossibile sostituire l'installazione precedente");
            goto done;
        }
    }
    if (g_rename(staging, target) != 0) {  /* rinomina di directory: atomica sullo stesso volume */
        if (old) {
            g_rename(old, target);  /* ripristina la versione precedente */
        }
        fail_with(error, DEP_INSTALLER_ERROR_EXTRACT, "impossibile completare l'installazione");
        goto done;
    }
    ok = TRUE;

done:
    if (old) {
        remove_tree(old);
    }
    if (!ok || g_file_test(staging, G_FILE_TEST_EXISTS)) {
        remove_tree(staging);  /* nessun residuo */
    }
    g_clear_error(&extract_error);
    g_free(old);
    g_free(target);
    g_free(staging);
    return ok;
}

/* ------------------------------------------------------------------ */
/* Esecuzione del piano                                                */
/* ------------------------------------------------------------------ */

static gboolean
run_artifact_step(const DepStep *step, const DepRunOptions *options, const Reporter *reporter, GError **error)
{
    char *deps_dir = options->deps_dir ? g_strdup(options->deps_dir) : deps_default_dir();
    char *downloads = g_build_filename(deps_dir, "downloads", NULL);
    char *dest = g_build_filename(downloads, step->artifact->filename, NULL);
    gboolean ok = FALSE;

    if (!download_verified(step->artifact, dest, options, reporter, error)) {
        goto done;
    }
    if (is_cancelled(options)) {
        fail_with(error, DEP_INSTALLER_ERROR_CANCELLED, "operazione annullata");
        goto done;
    }

    if (step->kind == DEP_STEP_ARCHIVE) {
        ok = install_archive(step->artifact, dest, deps_dir, error);
    } else {
        /* Solo ORA, a file verificato, si lancia l'installer con l'elevazione del sistema. */
        gboolean (*launch)(const char *, GCancellable *, GError **, gpointer) =
            options->run_installer ? options->run_installer : default_run_installer;

        ok = launch(dest, options->cancellable, error, options->run_installer ? options->run_installer_data : NULL);
    }

done:
    g_remove(dest);  /* l'installer/archivio scaricato non serve più (né in caso di errore) */
    g_free(dest);
    g_free(downloads);
    g_free(deps_dir);
    return ok;
}

gboolean
dep_installer_run(const DepPlan *plan, const DepRunOptions *options, GError **error)
{
    DepRunOptions defaults = { 0 };
    size_t count = dep_plan_step_count(plan);

    g_return_val_if_fail(plan != NULL, FALSE);
    if (!options) {
        options = &defaults;
    }

    /* Prima di fare qualunque cosa: nessun passo di terze parti senza il consenso dedicato. */
    if (dep_plan_has_third_party(plan) && !options->consent_third_party) {
        fail_with(error, DEP_INSTALLER_ERROR_CONSENT, "i repository di terze parti richiedono un consenso dedicato");
        return FALSE;
    }

    for (size_t i = 0; i < count; i++) {
        const DepStep *step = dep_plan_step(plan, i);
        Reporter reporter = { options, i, count, step->title };
        GError *step_error = NULL;
        gboolean ok;

        if (is_cancelled(options)) {
            fail_with(error, DEP_INSTALLER_ERROR_CANCELLED, "operazione annullata");
            return FALSE;
        }
        log_user_action("Installazione dipendenze: passo avviato", step->title);
        report_progress(&reporter, DEP_PROGRESS_STEP_STARTED, NULL, 0, 0);

        if (step->kind == DEP_STEP_SYSTEM_PACKAGES || step->kind == DEP_STEP_ADD_REPOSITORY) {
            ok = run_elevated_command(step, options, &reporter, &step_error);
        } else {
            ok = run_artifact_step(step, options, &reporter, &step_error);
        }

        if (!ok) {
            log_user_action("Installazione dipendenze: passo fallito", step_error->message);
            g_propagate_error(error, step_error);
            return FALSE;
        }
        log_user_action("Installazione dipendenze: passo completato", step->title);
        report_progress(&reporter, DEP_PROGRESS_STEP_FINISHED, NULL, 0, 0);
    }
    return TRUE;
}
