#include "core/deps_check.h"

#include "core/dep_manifest.h"

#include "core/logger.h"

#include <gio/gio.h>
#include <gst/gst.h>
#include <string.h>

struct DepsReport {
    DepsPlatform platform;
    GPtrArray *items;  /* DepsItem* */
};

/* ------------------------------------------------------------------ */
/* Piattaforma                                                         */
/* ------------------------------------------------------------------ */

DepsPlatform
deps_current_platform(void)
{
#if defined(G_OS_WIN32)
    return DEPS_PLATFORM_WINDOWS;
#elif defined(__APPLE__)
    return DEPS_PLATFORM_MACOS;
#else
    return DEPS_PLATFORM_LINUX;
#endif
}

char *
deps_default_dir(void)
{
    return g_build_filename(g_get_home_dir(), ".syncview", "deps", NULL);
}

/* ------------------------------------------------------------------ */
/* Sonde reali                                                         */
/* ------------------------------------------------------------------ */

static gboolean
real_has_gst_element(const char *name, gpointer user_data)
{
    (void)user_data;

    if (!gst_is_initialized()) {
        gst_init(NULL, NULL);
    }

    /* Lookup nel registry: non carica il plugin (evita di inizializzare driver GPU solo per controllare). */
    GstPluginFeature *feature = gst_registry_lookup_feature(gst_registry_get(), name);
    gboolean found = feature && GST_IS_ELEMENT_FACTORY(feature);

    if (feature) {
        gst_object_unref(feature);
    }
    return found;
}

static char *
program_in_dir(const char *dir, const char *name)
{
    const char *suffixes[] = { "", ".exe" };
    const char *subdirs[] = { "bin", "" };

    for (size_t d = 0; d < G_N_ELEMENTS(subdirs); d++) {
        for (size_t s = 0; s < G_N_ELEMENTS(suffixes); s++) {
            char *file = g_strconcat(name, suffixes[s], NULL);
            char *path = *subdirs[d] ? g_build_filename(dir, subdirs[d], file, NULL)
                                     : g_build_filename(dir, file, NULL);
            g_free(file);

            if (g_file_test(path, G_FILE_TEST_IS_REGULAR) && g_file_test(path, G_FILE_TEST_IS_EXECUTABLE)) {
                return path;
            }
            g_free(path);
        }
    }
    return NULL;
}

static char *
real_find_program(const char *name, const char *extra_dir, gpointer user_data)
{
    (void)user_data;

    if (extra_dir) {
        char *local = program_in_dir(extra_dir, name);
        if (local) {
            return local;
        }
    }
    return g_find_program_in_path(name);
}

typedef struct {
    GMainLoop *loop;
    char *output;
    gboolean ok;
} RunState;

static void
on_communicate_done(GObject *source, GAsyncResult *result, gpointer user_data)
{
    RunState *st = user_data;
    GError *error = NULL;

    if (g_subprocess_communicate_utf8_finish(G_SUBPROCESS(source), result, &st->output, NULL, &error)) {
        st->ok = g_subprocess_get_successful(G_SUBPROCESS(source));
    }
    g_clear_error(&error);
    g_main_loop_quit(st->loop);
}

static gboolean
on_run_timeout(gpointer user_data)
{
    g_subprocess_force_exit(G_SUBPROCESS(user_data));  /* poi on_communicate_done completa e chiude il loop */
    return G_SOURCE_REMOVE;
}

static char *
real_run_program(const char *path, const char *const *args, gpointer user_data)
{
    (void)user_data;
    GPtrArray *argv = g_ptr_array_new();

    g_ptr_array_add(argv, (gpointer)path);
    for (size_t i = 0; args && args[i]; i++) {
        g_ptr_array_add(argv, (gpointer)args[i]);
    }
    g_ptr_array_add(argv, NULL);

    GError *error = NULL;
    GSubprocess *proc = g_subprocess_newv((const gchar *const *)argv->pdata,
                                          G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE,
                                          &error);
    g_ptr_array_free(argv, TRUE);
    if (!proc) {
        g_clear_error(&error);
        return NULL;
    }

    /* Main context privato: non interferisce con quello dell'applicazione. */
    GMainContext *context = g_main_context_new();
    g_main_context_push_thread_default(context);
    RunState st = { g_main_loop_new(context, FALSE), NULL, FALSE };

    g_subprocess_communicate_utf8_async(proc, NULL, NULL, on_communicate_done, &st);
    GSource *timeout = g_timeout_source_new_seconds(5);
    g_source_set_callback(timeout, on_run_timeout, proc, NULL);
    g_source_attach(timeout, context);

    g_main_loop_run(st.loop);

    g_source_destroy(timeout);
    g_source_unref(timeout);
    g_main_loop_unref(st.loop);
    g_main_context_pop_thread_default(context);
    g_main_context_unref(context);
    g_object_unref(proc);

    if (!st.ok) {
        g_free(st.output);
        return NULL;
    }
    return st.output;
}

void
deps_probes_default(DepsProbes *probes)
{
    probes->has_gst_element = real_has_gst_element;
    probes->find_program = real_find_program;
    probes->run_program = real_run_program;
    probes->platform = deps_current_platform();
    probes->user_data = NULL;
}

/* ------------------------------------------------------------------ */
/* Report                                                              */
/* ------------------------------------------------------------------ */

static void
item_free(DepsItem *item)
{
    g_free(item->id);
    g_free(item->title);
    g_free(item->detail);
    g_free(item->instructions);
    g_free(item->package_manager);
    g_strfreev(item->packages);
    g_free(item);
}

/* Come risolvere un componente mancante: risoluzione, testo per l'installazione a mano e, se installabile, pacchetti. */
typedef struct {
    DepsResolution resolution;
    char *instructions;      /* owned */
    char *package_manager;   /* owned, solo SYSTEM_PACKAGES */
    char **packages;         /* owned, solo SYSTEM_PACKAGES */
} InstallPlan;

static InstallPlan
plan_none(void)
{
    InstallPlan plan = { DEPS_RESOLUTION_NONE, NULL, NULL, NULL };
    return plan;
}

/* Un piano "manuale" con un testo e una risoluzione espliciti (es. DOWNLOADABLE per ffmpeg su Windows/macOS). */
static InstallPlan
plan_text(DepsResolution resolution, char *instructions)
{
    InstallPlan plan = { resolution, instructions, NULL, NULL };
    return plan;
}

static void
report_add(DepsReport *report, const char *id, const char *title, DepsFeature feature, DepsStatus status,
           char *detail, InstallPlan plan)
{
    DepsItem *item = g_new0(DepsItem, 1);

    item->id = g_strdup(id);
    item->title = g_strdup(title);
    item->feature = feature;
    item->status = status;
    item->resolution = plan.resolution;
    item->detail = detail ? detail : g_strdup("");
    item->instructions = plan.instructions ? plan.instructions : g_strdup("");
    item->package_manager = plan.package_manager;
    item->packages = plan.packages;
    g_ptr_array_add(report->items, item);
}

void
deps_report_free(DepsReport *report)
{
    if (!report) {
        return;
    }

    g_ptr_array_free(report->items, TRUE);
    g_free(report);
}

DepsPlatform
deps_report_get_platform(const DepsReport *report)
{
    return report->platform;
}

size_t
deps_report_count(const DepsReport *report)
{
    return report->items->len;
}

const DepsItem *
deps_report_get(const DepsReport *report, size_t index)
{
    return index < report->items->len ? g_ptr_array_index(report->items, index) : NULL;
}

const DepsItem *
deps_report_find(const DepsReport *report, const char *id)
{
    for (guint i = 0; i < report->items->len; i++) {
        const DepsItem *item = g_ptr_array_index(report->items, i);
        if (strcmp(item->id, id) == 0) {
            return item;
        }
    }
    return NULL;
}

static gboolean
no_missing_for(const DepsReport *report, DepsFeature feature)
{
    for (guint i = 0; i < report->items->len; i++) {
        const DepsItem *item = g_ptr_array_index(report->items, i);
        if (item->feature == feature && item->status == DEPS_STATUS_MISSING) {
            return FALSE;
        }
    }
    return TRUE;
}

gboolean
deps_report_can_play(const DepsReport *report)
{
    return no_missing_for(report, DEPS_FEATURE_PLAYBACK);
}

gboolean
deps_report_can_export(const DepsReport *report)
{
    return no_missing_for(report, DEPS_FEATURE_EXPORT);
}

gboolean
deps_report_is_complete(const DepsReport *report)
{
    for (guint i = 0; i < report->items->len; i++) {
        const DepsItem *item = g_ptr_array_index(report->items, i);
        if (item->status != DEPS_STATUS_OK) {
            return FALSE;
        }
    }
    return TRUE;
}

static const char *
feature_label(DepsFeature feature)
{
    switch (feature) {
    case DEPS_FEATURE_PLAYBACK: return "riproduzione";
    case DEPS_FEATURE_EXPORT:   return "export";
    default:                    return "opzionale";
    }
}

char *
deps_report_to_text(const DepsReport *report)
{
    GString *text = g_string_new(NULL);

    for (guint i = 0; i < report->items->len; i++) {
        const DepsItem *item = g_ptr_array_index(report->items, i);
        const char *tag = item->status == DEPS_STATUS_OK ? "[OK]"
                          : item->status == DEPS_STATUS_MISSING ? "[MANCANTE]" : "[OPZIONALE MANCANTE]";

        g_string_append_printf(text, "%-22s %s (%s)\n    %s\n", tag, item->title, feature_label(item->feature),
                               item->detail);
        if (item->status != DEPS_STATUS_OK) {
            if (item->resolution == DEPS_RESOLUTION_SYSTEM_PACKAGES && item->packages) {
                char *packages = g_strjoinv(" ", item->packages);
                g_string_append_printf(text, "    -> SyncView può installarlo (%s: %s); il sistema chiederà la password di amministratore\n",
                                       item->package_manager, packages);
                g_free(packages);
            } else if (item->resolution == DEPS_RESOLUTION_DOWNLOADABLE) {
                g_string_append(text, "    -> SyncView può scaricarlo\n");
            } else if (item->resolution == DEPS_RESOLUTION_PLATFORM_INSTALLER) {
                g_string_append(text, "    -> SyncView può scaricare e lanciare l'installer ufficiale di GStreamer; "
                                      "il sistema chiederà l'autorizzazione\n");
            }
            if (*item->instructions) {
                g_string_append_printf(text, "    %s %s\n",
                                       item->resolution == DEPS_RESOLUTION_INSTRUCTIONS ? "->" : "   a mano:",
                                       item->instructions);
            }
        }
    }

    g_string_append_printf(text, "\nRiproduzione: %s | Export: %s | Completo: %s\n",
                           deps_report_can_play(report) ? "possibile" : "NON possibile",
                           deps_report_can_export(report) ? "possibile" : "NON possibile",
                           deps_report_is_complete(report) ? "si" : "no");
    return g_string_free(text, FALSE);
}

char **
deps_report_collect_packages(const DepsReport *report, gboolean include_optional, char **package_manager)
{
    GPtrArray *names = g_ptr_array_new_with_free_func(g_free);
    char *pm = NULL;

    for (guint i = 0; i < report->items->len; i++) {
        const DepsItem *item = g_ptr_array_index(report->items, i);

        /* Un componente OK ha risoluzione NONE: qui restano solo quelli mancanti installabili. */
        if (item->resolution != DEPS_RESOLUTION_SYSTEM_PACKAGES || !item->packages) {
            continue;
        }
        if (item->status == DEPS_STATUS_OPTIONAL_MISSING && !include_optional) {
            continue;
        }

        if (!pm) {
            pm = g_strdup(item->package_manager);
        }
        for (size_t k = 0; item->packages[k]; k++) {
            gboolean dup = FALSE;
            for (guint n = 0; n < names->len; n++) {
                dup |= strcmp(g_ptr_array_index(names, n), item->packages[k]) == 0;
            }
            if (!dup) {
                g_ptr_array_add(names, g_strdup(item->packages[k]));
            }
        }
    }

    if (package_manager) {
        *package_manager = pm;
    } else {
        g_free(pm);
    }

    if (names->len == 0) {
        g_ptr_array_free(names, TRUE);
        return NULL;
    }
    g_ptr_array_add(names, NULL);
    return (char **)g_ptr_array_free(names, FALSE);
}

void
deps_report_log(const DepsReport *report)
{
    guint missing = 0, optional_missing = 0;

    for (guint i = 0; i < report->items->len; i++) {
        const DepsItem *item = g_ptr_array_index(report->items, i);

        missing += item->status == DEPS_STATUS_MISSING;
        optional_missing += item->status == DEPS_STATUS_OPTIONAL_MISSING;
        log_gst("deps: %s [%s] %s", item->id,
                item->status == DEPS_STATUS_OK ? "ok" : item->status == DEPS_STATUS_MISSING ? "MANCANTE" : "opzionale mancante",
                item->detail);
    }

    char *details = g_strdup_printf("riproduzione: %s, export: %s, mancanti: %u, opzionali mancanti: %u",
                                    deps_report_can_play(report) ? "si" : "NO",
                                    deps_report_can_export(report) ? "si" : "NO", missing, optional_missing);
    log_user_action("Verifica dipendenze", details);
    g_free(details);
}

/* ------------------------------------------------------------------ */
/* Istruzioni di installazione                                         */
/* ------------------------------------------------------------------ */

typedef enum { PM_NONE, PM_PACMAN, PM_APT, PM_DNF, PM_ZYPPER } PackageManager;

/*
 * Nomi dei pacchetti per distribuzione. Un nome presente è un vero nome di pacchetto installabile dai repository
 * predefiniti (verificato); NULL = non disponibile nei repository predefiniti o nome non verificato: in quel caso
 * l'app non tenta l'installazione automatica e mostra la nota (`manual_note`) per l'installazione a mano.
 */
typedef struct {
    const char *arch;    /* pacman */
    const char *apt;
    const char *dnf;
    const char *zypper;
    const char *manual_note;  /* dove trovarlo quando manca per qualche distribuzione */
} PackageSet;

static const PackageSet PKG_BASE = {
    "gst-plugins-base", "gstreamer1.0-plugins-base", "gstreamer1-plugins-base", "gstreamer-plugins-base", NULL,
};
static const PackageSet PKG_GOOD = {
    "gst-plugins-good", "gstreamer1.0-plugins-good", "gstreamer1-plugins-good", "gstreamer-plugins-good", NULL,
};
static const PackageSet PKG_BAD = {
    "gst-plugins-bad", "gstreamer1.0-plugins-bad", "gstreamer1-plugins-bad-free", "gstreamer-plugins-bad", NULL,
};
/* plugins-ugly: asfdemux (wmv). Su Fedora e openSUSE il nome non è verificato: niente installazione automatica. */
static const PackageSet PKG_UGLY = {
    "gst-plugins-ugly", "gstreamer1.0-plugins-ugly", NULL, NULL,
    "Fedora: gstreamer1-plugins-ugly-free o da RPM Fusion; openSUSE: gstreamer-plugins-ugly (verifica con gst-inspect-1.0 asfdemux)",
};
/* gst-libav: su Fedora è in RPM Fusion e su openSUSE in Packman (repository di terze parti, non predefiniti). */
static const PackageSet PKG_LIBAV = {
    "gst-libav", "gstreamer1.0-libav", NULL, NULL,
    "Fedora: gstreamer1-libav da RPM Fusion; openSUSE: gstreamer-plugins-libav da Packman",
};
/* Verificati: Arch gst-plugin-gtk4, Debian/Ubuntu gstreamer1.0-gtk4, Fedora gstreamer1-plugin-gtk4; openSUSE non verificato. */
static const PackageSet PKG_GTK4 = {
    "gst-plugin-gtk4", "gstreamer1.0-gtk4", "gstreamer1-plugin-gtk4", NULL,
    "openSUSE: pacchetto che fornisce il plugin GStreamer gtk4paintablesink (gst-plugins-rs)",
};
/* ffmpeg: su Fedora serve RPM Fusion (ffmpeg-free non include libx264); su openSUSE il ffmpeg completo è in Packman. */
static const PackageSet PKG_FFMPEG = {
    "ffmpeg", "ffmpeg", NULL, NULL,
    "Fedora: ffmpeg da RPM Fusion (ffmpeg-free non include libx264); openSUSE: ffmpeg da Packman",
};

static PackageManager
detect_package_manager(const DepsProbes *probes)
{
    struct { const char *program; PackageManager pm; } candidates[] = {
        { "pacman", PM_PACMAN }, { "apt-get", PM_APT }, { "dnf", PM_DNF }, { "zypper", PM_ZYPPER },
    };

    for (size_t i = 0; i < G_N_ELEMENTS(candidates); i++) {
        char *path = probes->find_program(candidates[i].program, NULL, probes->user_data);
        if (path) {
            g_free(path);
            return candidates[i].pm;
        }
    }
    return PM_NONE;
}

static const char *
package_name_for(PackageManager pm, const PackageSet *set)
{
    return pm == PM_PACMAN ? set->arch : pm == PM_APT ? set->apt : pm == PM_DNF ? set->dnf : set->zypper;
}

static const char *
package_manager_program(PackageManager pm)
{
    return pm == PM_PACMAN ? "pacman" : pm == PM_APT ? "apt-get" : pm == PM_DNF ? "dnf" : "zypper";
}

gboolean
deps_package_is_known(const char *package_manager, const char *package)
{
    const PackageSet *all[] = { &PKG_BASE, &PKG_GOOD, &PKG_BAD, &PKG_LIBAV, &PKG_UGLY, &PKG_GTK4, &PKG_FFMPEG };
    PackageManager managers[] = { PM_PACMAN, PM_APT, PM_DNF, PM_ZYPPER };

    if (!package_manager || !package) {
        return FALSE;
    }
    for (size_t m = 0; m < G_N_ELEMENTS(managers); m++) {
        if (strcmp(package_manager, package_manager_program(managers[m])) != 0) {
            continue;
        }
        for (size_t i = 0; i < G_N_ELEMENTS(all); i++) {
            const char *name = package_name_for(managers[m], all[i]);

            if (name && strcmp(name, package) == 0) {
                return TRUE;
            }
        }
    }
    return FALSE;
}

static const char *
manual_install_verb(PackageManager pm)
{
    return pm == PM_PACMAN ? "sudo pacman -S" : pm == PM_APT ? "sudo apt install"
           : pm == PM_DNF ? "sudo dnf install" : "sudo zypper install";
}

/* Pacchetti che forniscono ciascun componente (le stesse tabelle usate per i piani d'installazione). */
static size_t
sets_for_component(const char *id, const PackageSet **out)
{
    if (strcmp(id, "gst-playbin3") == 0) {
        out[0] = &PKG_BASE;
        return 1;
    }
    if (strcmp(id, "gst-gtk4sink") == 0) {
        out[0] = &PKG_GTK4;
        return 1;
    }
    if (strcmp(id, "gst-demuxers") == 0 || strcmp(id, "gst-decoder-vp9") == 0 || strcmp(id, "gst-decoder-av1") == 0) {
        out[0] = &PKG_GOOD;
        out[1] = &PKG_LIBAV;
        if (strcmp(id, "gst-demuxers") == 0) {
            out[2] = &PKG_UGLY;  /* asfdemux (wmv) */
            return 3;
        }
        return 2;
    }
    if (strcmp(id, "gst-decoder-h264") == 0 || strcmp(id, "gst-decoder-hevc") == 0) {
        out[0] = &PKG_LIBAV;
        return 1;
    }
    if (strcmp(id, "gst-hw-decoders") == 0) {
        out[0] = &PKG_BAD;
        return 1;
    }
    if (strcmp(id, "ffmpeg") == 0) {
        out[0] = &PKG_FFMPEG;
        return 1;
    }
    return 0;
}

char *
deps_report_manual_command(const DepsReport *report, const char *package_manager, gboolean include_optional,
                           char **note)
{
    PackageManager managers[] = { PM_PACMAN, PM_APT, PM_DNF, PM_ZYPPER };
    PackageManager pm = PM_NONE;
    GPtrArray *names = g_ptr_array_new();
    GPtrArray *notes = g_ptr_array_new();

    if (note) {
        *note = NULL;
    }
    for (size_t m = 0; package_manager && m < G_N_ELEMENTS(managers); m++) {
        if (strcmp(package_manager, package_manager_program(managers[m])) == 0) {
            pm = managers[m];
        }
    }

    for (size_t i = 0; pm != PM_NONE && i < deps_report_count(report); i++) {
        const DepsItem *it = deps_report_get(report, i);

        if (it->status == DEPS_STATUS_OK || (it->status == DEPS_STATUS_OPTIONAL_MISSING && !include_optional)) {
            continue;
        }

        const PackageSet *sets[3];
        size_t n = sets_for_component(it->id, sets);

        for (size_t s = 0; s < n; s++) {
            const char *name = package_name_for(pm, sets[s]);
            GPtrArray *target = name ? names : notes;
            const char *value = name ? name : sets[s]->manual_note;
            gboolean seen = FALSE;

            for (guint k = 0; value && k < target->len; k++) {
                seen |= strcmp(g_ptr_array_index(target, k), value) == 0;
            }
            if (value && !seen) {
                g_ptr_array_add(target, (gpointer)value);
            }
        }
    }

    char *command = NULL;

    if (names->len > 0) {
        g_ptr_array_add(names, NULL);
        char *joined = g_strjoinv(" ", (char **)names->pdata);

        command = g_strdup_printf("%s %s", manual_install_verb(pm), joined);
        g_free(joined);
    }
    if (note && notes->len > 0) {
        g_ptr_array_add(notes, NULL);
        *note = g_strjoinv("; ", (char **)notes->pdata);
    }
    g_ptr_array_free(names, TRUE);
    g_ptr_array_free(notes, TRUE);
    return command;
}

/* Piano per installare `sets` (i pacchetti che forniscono il componente) sulla piattaforma corrente. */
static InstallPlan
build_plan(const DepsProbes *probes, const PackageSet *const *sets, size_t n, const char *what)
{
    switch (probes->platform) {
    case DEPS_PLATFORM_LINUX: {
        PackageManager pm = detect_package_manager(probes);
        gboolean all_named = pm != PM_NONE;

        for (size_t i = 0; all_named && i < n; i++) {
            all_named = package_name_for(pm, sets[i]) != NULL;
        }

        if (all_named) {
            /* Installabile dall'app: nomi verificati per questo gestore. */
            GPtrArray *names = g_ptr_array_new();
            for (size_t i = 0; i < n; i++) {
                g_ptr_array_add(names, g_strdup(package_name_for(pm, sets[i])));
            }
            g_ptr_array_add(names, NULL);

            char *joined = g_strjoinv(" ", (char **)names->pdata);
            InstallPlan plan = {
                DEPS_RESOLUTION_SYSTEM_PACKAGES,
                g_strdup_printf("Installa %s: %s %s", what, manual_install_verb(pm), joined),
                g_strdup(package_manager_program(pm)),
                (char **)g_ptr_array_free(names, FALSE),
            };
            g_free(joined);
            return plan;
        }

        /* Nessun gestore riconosciuto, o nome non disponibile nei repository predefiniti: solo istruzioni. */
        GString *text = g_string_new(NULL);
        g_string_append_printf(text, "Installa %s dal gestore di pacchetti della distribuzione (", what);
        for (size_t i = 0; i < n; i++) {
            g_string_append_printf(text, "%sArch: %s; Debian/Ubuntu: %s; Fedora: %s", i ? " | " : "",
                                   sets[i]->arch, sets[i]->apt, sets[i]->dnf ? sets[i]->dnf : "non nei repository predefiniti");
            if (sets[i]->manual_note) {
                g_string_append_printf(text, "; %s", sets[i]->manual_note);
            }
        }
        g_string_append(text, ")");
        return plan_text(DEPS_RESOLUTION_INSTRUCTIONS, g_string_free(text, FALSE));
    }
    case DEPS_PLATFORM_WINDOWS:
        if (dep_manifest_gstreamer_installer(DEPS_PLATFORM_WINDOWS)) {
            return plan_text(DEPS_RESOLUTION_PLATFORM_INSTALLER,
                             g_strdup_printf("Installa %s con l'installer ufficiale di GStreamer: SyncView lo scarica, ne verifica "
                                   "l'SHA-256 e lo lancia chiedendo i permessi di amministratore a Windows (UAC). "
                                   "A mano: gstreamer.freedesktop.org, sezione Download. In una build da sorgente con MSYS2: "
                                   "pacman -S mingw-w64-ucrt-x86_64-gst-plugins-base mingw-w64-ucrt-x86_64-gst-plugins-good mingw-w64-ucrt-x86_64-gst-plugins-ugly "
                                   "mingw-w64-ucrt-x86_64-gst-plugins-bad mingw-w64-ucrt-x86_64-gst-libav "
                                   "mingw-w64-ucrt-x86_64-gst-plugins-rs", what));
        }
        return plan_text(DEPS_RESOLUTION_INSTRUCTIONS,
                         g_strdup_printf("Reinstalla SyncView: il pacchetto per Windows include %s. "
                               "In una build da sorgente con MSYS2: pacman -S mingw-w64-ucrt-x86_64-gst-plugins-base "
                               "mingw-w64-ucrt-x86_64-gst-plugins-good mingw-w64-ucrt-x86_64-gst-plugins-ugly "
                               "mingw-w64-ucrt-x86_64-gst-plugins-bad mingw-w64-ucrt-x86_64-gst-libav mingw-w64-ucrt-x86_64-gst-plugins-rs", what));
    case DEPS_PLATFORM_MACOS:
    default:
        if (dep_manifest_gstreamer_installer(DEPS_PLATFORM_MACOS)) {
            return plan_text(DEPS_RESOLUTION_PLATFORM_INSTALLER,
                             g_strdup_printf("Installa %s con l'installer ufficiale di GStreamer: SyncView lo scarica, ne verifica "
                                   "l'SHA-256 e lo apre nell'Installer di macOS, che chiede lui l'autorizzazione. "
                                   "A mano: gstreamer.freedesktop.org, sezione Download (il runtime include gtk4paintablesink). "
                                   "Verifica con: gst-inspect-1.0 <elemento>", what));
        }
        return plan_text(DEPS_RESOLUTION_INSTRUCTIONS,
                         g_strdup_printf("Reinstalla SyncView (il bundle per macOS include %s) oppure installa GStreamer 1.28 o "
                               "successivo dal sito ufficiale (il suo installer include gtk4paintablesink); "
                               "verifica con: gst-inspect-1.0 <elemento>", what));
    }
}

/* ------------------------------------------------------------------ */
/* Controlli                                                           */
/* ------------------------------------------------------------------ */

static const char *
first_found(const DepsProbes *probes, const char *const *names)
{
    for (size_t i = 0; names[i]; i++) {
        if (probes->has_gst_element(names[i], probes->user_data)) {
            return names[i];
        }
    }
    return NULL;
}

/* Elenco (separato da virgole) degli elementi presenti tra `names`; NULL se nessuno. */
static char *
found_list(const DepsProbes *probes, const char *const *names)
{
    GPtrArray *found = g_ptr_array_new();

    for (size_t i = 0; names[i]; i++) {
        if (probes->has_gst_element(names[i], probes->user_data)) {
            g_ptr_array_add(found, (gpointer)names[i]);
        }
    }

    char *joined = NULL;
    if (found->len > 0) {
        g_ptr_array_add(found, NULL);
        joined = g_strjoinv(", ", (char **)found->pdata);
    }
    g_ptr_array_free(found, TRUE);
    return joined;
}

/* Componente soddisfatto se ALMENO UNO degli elementi in `names` è presente. */
static void
check_any_of(DepsReport *report, const DepsProbes *probes, const char *id, const char *title, DepsFeature feature,
             const char *const *names, const char *missing_text, const PackageSet *const *sets, size_t n_sets,
             const char *what)
{
    char *found = found_list(probes, names);

    if (found) {
        report_add(report, id, title, feature, DEPS_STATUS_OK, g_strdup_printf("Trovato: %s", found), plan_none());
        g_free(found);
        return;
    }

    char *names_joined = g_strjoinv(", ", (char **)names);
    report_add(report, id, title, feature,
               feature == DEPS_FEATURE_OPTIONAL ? DEPS_STATUS_OPTIONAL_MISSING : DEPS_STATUS_MISSING,
               g_strdup_printf("%s (cercati: %s)", missing_text, names_joined),
               build_plan(probes, sets, n_sets, what));
    g_free(names_joined);
}

/*
 * Un demuxer per ciascun formato supportato (estensioni di core/settings). I formati `core` sono quelli comuni: se
 * ne manca uno la riproduzione è compromessa; se mancano solo gli altri (wmv, flv) il componente è opzionale.
 * `ugly`: l'elemento sta in plugins-ugly (asfdemux), non in good.
 */
typedef struct {
    const char *formats;
    const char *elements[3];
    gboolean core;
    gboolean ugly;
} ContainerRule;

static const ContainerRule CONTAINER_RULES[] = {
    { "mp4, mov", { "qtdemux", NULL }, TRUE, FALSE },
    { "avi", { "avidemux", NULL }, TRUE, FALSE },
    { "mkv", { "matroskademux", NULL }, TRUE, FALSE },
    { "wmv", { "asfdemux", "avdemux_asf", NULL }, FALSE, TRUE },  /* asfdemux è in plugins-ugly; avdemux_asf in gst-libav */
    { "flv", { "flvdemux", "avdemux_flv", NULL }, FALSE, FALSE },
};

static void
check_demuxers(DepsReport *report, const DepsProbes *probes)
{
    GString *missing = g_string_new(NULL);
    GString *ok = g_string_new(NULL);
    gboolean core_missing = FALSE, need_good = FALSE, need_ugly = FALSE;

    for (size_t i = 0; i < G_N_ELEMENTS(CONTAINER_RULES); i++) {
        const char *found = first_found(probes, CONTAINER_RULES[i].elements);
        GString *target = found ? ok : missing;

        g_string_append_printf(target, "%s%s%s%s%s", target->len ? "; " : "", CONTAINER_RULES[i].formats,
                               found ? " (" : "", found ? found : "", found ? ")" : "");
        if (!found) {
            core_missing |= CONTAINER_RULES[i].core;
            need_ugly |= CONTAINER_RULES[i].ugly;
            need_good |= !CONTAINER_RULES[i].ugly;
        }
    }

    if (missing->len == 0) {
        report_add(report, "gst-demuxers", "Demuxer dei formati supportati", DEPS_FEATURE_PLAYBACK,
                   DEPS_STATUS_OK, g_strdup_printf("Tutti presenti: %s", ok->str), plan_none());
    } else {
        const PackageSet *sets[3];
        size_t n = 0;

        if (need_good) {
            sets[n++] = &PKG_GOOD;
            sets[n++] = &PKG_LIBAV;
        }
        if (need_ugly) {
            sets[n++] = &PKG_UGLY;
        }
        report_add(report, "gst-demuxers", "Demuxer dei formati supportati",
                   core_missing ? DEPS_FEATURE_PLAYBACK : DEPS_FEATURE_OPTIONAL,
                   core_missing ? DEPS_STATUS_MISSING : DEPS_STATUS_OPTIONAL_MISSING,
                   g_strdup_printf("Formati senza demuxer: %s%s%s", missing->str, ok->len ? " — presenti: " : "", ok->str),
                   build_plan(probes, sets, n, need_good && need_ugly ? "i plugin GStreamer good, libav e ugly"
                                               : need_ugly ? "il plugin GStreamer ugly" : "i plugin GStreamer good e libav"));
    }

    g_string_free(missing, TRUE);
    g_string_free(ok, TRUE);
}

/* Decoder: software (libav e simili) e hardware per piattaforma, accettati indifferentemente. */
static const char *const DECODERS_H264[] = {
    "avdec_h264", "openh264dec", "vah264dec", "vaapih264dec", "nvh264dec", "v4l2slh264dec", "d3d11h264dec",
    "d3d12h264dec", "qsvh264dec", "msdkh264dec", "vtdec", "vtdec_hw", NULL,
};
static const char *const DECODERS_HEVC[] = {
    "avdec_h265", "vah265dec", "vaapih265dec", "nvh265dec", "v4l2slh265dec", "d3d11h265dec", "d3d12h265dec",
    "qsvh265dec", "msdkh265dec", "vtdec", "vtdec_hw", NULL,
};
static const char *const DECODERS_VP9[] = {
    "vp9dec", "avdec_vp9", "vavp9dec", "nvvp9dec", "d3d11vp9dec", "d3d12vp9dec", "vtdec", "vtdec_hw", NULL,
};
static const char *const DECODERS_AV1[] = {
    "av1dec", "dav1d", "avdec_av1", "vaav1dec", "nvav1dec", "d3d11av1dec", "d3d12av1dec", "qsvav1dec", NULL,
};

/* Decoder hardware per piattaforma (elenco a scopo informativo: il playback li usa da solo se presenti). */
static const char *const HW_LINUX[] = {
    "vah264dec", "vah265dec", "vavp9dec", "vaav1dec", "vaapih264dec", "vaapih265dec", "nvh264dec", "nvh265dec",
    "nvav1dec", "v4l2slh264dec", "v4l2slh265dec", NULL,
};
static const char *const HW_WINDOWS[] = {
    "d3d11h264dec", "d3d11h265dec", "d3d12h264dec", "d3d12h265dec", "nvh264dec", "nvh265dec", "nvav1dec",
    "qsvh264dec", "qsvh265dec", NULL,
};
static const char *const HW_MACOS[] = { "vtdec_hw", "vtdec", NULL };

static void
check_hw_decoders(DepsReport *report, const DepsProbes *probes)
{
    const char *const *list = probes->platform == DEPS_PLATFORM_WINDOWS ? HW_WINDOWS
                              : probes->platform == DEPS_PLATFORM_MACOS ? HW_MACOS : HW_LINUX;
    char *found = found_list(probes, list);

    if (found) {
        report_add(report, "gst-hw-decoders", "Decoder video hardware", DEPS_FEATURE_OPTIONAL, DEPS_STATUS_OK,
                   g_strdup_printf("Disponibili: %s", found), plan_none());
        g_free(found);
        return;
    }

    const PackageSet *sets[] = { &PKG_BAD };
    report_add(report, "gst-hw-decoders", "Decoder video hardware", DEPS_FEATURE_OPTIONAL,
               DEPS_STATUS_OPTIONAL_MISSING,
               g_strdup("Nessun decoder hardware trovato: la decodifica sarà software (più carico sulla CPU "
                        "con più video contemporanei)"),
               build_plan(probes, sets, G_N_ELEMENTS(sets), "i plugin GStreamer bad (decoder va/nvcodec) e i driver della GPU"));
}

/* ffmpeg ------------------------------------------------------------ */

static char *
parse_ffmpeg_version(const char *output)
{
    /* Prima riga: "ffmpeg version 7.1.1 Copyright (c) ..." */
    const char *marker = strstr(output, "version ");
    if (!marker) {
        return NULL;
    }
    marker += strlen("version ");

    const char *end = marker;
    while (*end && !g_ascii_isspace(*end)) {
        end++;
    }
    return end > marker ? g_strndup(marker, end - marker) : NULL;
}

static char *
find_h264_encoder(const char *encoders_output)
{
    static const char *const names[] = {
        "libx264", "libopenh264", "h264_nvenc", "h264_qsv", "h264_vaapi", "h264_videotoolbox", "h264_amf", "h264_mf", NULL,
    };

    /* Righe del tipo " V....D libx264  libx264 H.264 / AVC ..." */
    char **lines = g_strsplit(encoders_output, "\n", -1);
    GPtrArray *found = g_ptr_array_new_with_free_func(g_free);

    for (size_t n = 0; names[n]; n++) {
        for (int i = 0; lines[i]; i++) {
            char **cols = g_strsplit_set(g_strstrip(lines[i]), " \t", 3);
            if (cols[0] && cols[1] && strcmp(cols[1], names[n]) == 0 && cols[0][0] == 'V') {
                g_ptr_array_add(found, g_strdup(names[n]));
                g_strfreev(cols);
                break;
            }
            g_strfreev(cols);
        }
    }
    g_strfreev(lines);

    char *joined = NULL;
    if (found->len > 0) {
        g_ptr_array_add(found, NULL);
        joined = g_strjoinv(", ", (char **)found->pdata);
    }
    g_ptr_array_free(found, TRUE);
    return joined;
}

/*
 * Come ottenere ffmpeg: su Linux si installa dai pacchetti della distribuzione (con elevazione gestita dal sistema) se
 * il nome è verificato, altrimenti istruzioni. Su Windows/macOS NON c'è ancora un artefatto nel manifest incorporato
 * (dove ospitare gli artefatti non ufficiali è una decisione aperta, vedi PLAN.md): per ora solo istruzioni; l'installer
 * di GStreamer non contiene ffmpeg.
 */
static InstallPlan
ffmpeg_plan(const DepsProbes *probes, const PackageSet *const *sets, const char *what)
{
    if (probes->platform == DEPS_PLATFORM_WINDOWS) {
        return plan_text(DEPS_RESOLUTION_INSTRUCTIONS,
                         g_strdup_printf("Installa %s: scarica un build di ffmpeg (per esempio da gyan.dev/ffmpeg/builds) e copia "
                               "ffmpeg.exe nella cartella ~/.syncview/deps/bin (oppure mettilo nel PATH)", what));
    }
    if (probes->platform == DEPS_PLATFORM_MACOS) {
        return plan_text(DEPS_RESOLUTION_INSTRUCTIONS,
                         g_strdup_printf("Installa %s con Homebrew (brew install ffmpeg) oppure copia ffmpeg nella cartella "
                               "~/.syncview/deps/bin", what));
    }
    return build_plan(probes, sets, 1, what);
}

static void
check_ffmpeg(DepsReport *report, const DepsProbes *probes, const char *deps_dir)
{
    const char *title = "ffmpeg";
    const PackageSet *sets[] = { &PKG_FFMPEG };
    char *path = probes->find_program("ffmpeg", deps_dir, probes->user_data);

    if (!path) {
        report_add(report, "ffmpeg", title, DEPS_FEATURE_EXPORT, DEPS_STATUS_MISSING,
                   g_strdup_printf("ffmpeg non trovato (cercato in %s e nel PATH)", deps_dir ? deps_dir : "-"),
                   ffmpeg_plan(probes, sets, "ffmpeg (con encoder H.264, es. libx264)"));
        return;
    }

    const char *version_args[] = { "-version", NULL };
    char *version_out = probes->run_program(path, version_args, probes->user_data);
    char *version = version_out ? parse_ffmpeg_version(version_out) : NULL;

    if (!version_out) {
        report_add(report, "ffmpeg", title, DEPS_FEATURE_EXPORT, DEPS_STATUS_MISSING,
                   g_strdup_printf("Trovato %s ma non eseguibile (o senza risposta entro 5 s)", path),
                   ffmpeg_plan(probes, sets, "una versione funzionante di ffmpeg"));
        g_free(path);
        return;
    }

    const char *encoder_args[] = { "-hide_banner", "-encoders", NULL };
    char *encoders_out = probes->run_program(path, encoder_args, probes->user_data);
    char *h264 = encoders_out ? find_h264_encoder(encoders_out) : NULL;

    if (h264) {
        report_add(report, "ffmpeg", title, DEPS_FEATURE_EXPORT, DEPS_STATUS_OK,
                   g_strdup_printf("%s, versione %s, encoder H.264: %s", path, version ? version : "sconosciuta", h264),
                   plan_none());
    } else {
        report_add(report, "ffmpeg", title, DEPS_FEATURE_EXPORT, DEPS_STATUS_MISSING,
                   g_strdup_printf("%s (versione %s) non ha nessun encoder H.264 (libx264 o hardware): l'export non è possibile",
                                   path, version ? version : "sconosciuta"),
                   ffmpeg_plan(probes, sets, "ffmpeg con encoder H.264 (es. libx264)"));
    }

    g_free(h264);
    g_free(encoders_out);
    g_free(version);
    g_free(version_out);
    g_free(path);
}

/* ------------------------------------------------------------------ */
/* Simulazione di mancanze (debug/test): SYNCVIEW_DEPS_FAKE_MISSING      */
/* ------------------------------------------------------------------ */

typedef struct {
    DepsProbes base;
} FakeCtx;

static gboolean
fake_has_no_element(const char *name, gpointer user_data)
{
    (void)name;
    (void)user_data;
    return FALSE;
}

static char *
fake_find_program(const char *name, const char *extra_dir, gpointer user_data)
{
    FakeCtx *ctx = user_data;

    if (strcmp(name, "ffmpeg") == 0) {
        return NULL;
    }
    return ctx->base.find_program(name, extra_dir, ctx->base.user_data);
}

/* TRUE se `id` compare nella lista `fake` (separata da virgole; "all" = tutti). */
static gboolean
fake_lists(const char *fake, const char *id)
{
    if (!fake || !*fake) {
        return FALSE;
    }

    gboolean found = FALSE;
    char **ids = g_strsplit(fake, ",", -1);

    for (int i = 0; ids[i]; i++) {
        g_strstrip(ids[i]);
        found |= strcmp(ids[i], id) == 0 || strcmp(ids[i], "all") == 0;
    }
    g_strfreev(ids);
    return found;
}

/*
 * Sonde per il componente `id`: se è nella lista delle mancanze simulate, il registry non conosce nessun elemento
 * (e ffmpeg non si trova); il resto del controllo gira normalmente, così anche i piani d'installazione sono quelli veri.
 */
static DepsProbes
probes_for(const DepsProbes *base, FakeCtx *ctx, const char *fake, const char *id)
{
    DepsProbes probes = *base;

    if (fake_lists(fake, id)) {
        ctx->base = *base;
        probes.has_gst_element = fake_has_no_element;
        probes.find_program = fake_find_program;
        probes.user_data = ctx;
    }
    return probes;
}

/* ------------------------------------------------------------------ */
/* Esecuzione                                                          */
/* ------------------------------------------------------------------ */

DepsReport *
deps_check_run(const DepsProbes *probes_in, const char *deps_dir_in)
{
    DepsProbes probes;

    if (probes_in) {
        probes = *probes_in;
    } else {
        deps_probes_default(&probes);
    }

    char *default_dir = deps_dir_in ? NULL : deps_default_dir();
    const char *deps_dir = deps_dir_in ? deps_dir_in : default_dir;

    DepsReport *report = g_new0(DepsReport, 1);
    report->platform = probes.platform;
    report->items = g_ptr_array_new_with_free_func((GDestroyNotify)item_free);

    /* Solo debug/test: componenti da dichiarare mancanti (vedi probes_for). */
    const char *fake = g_getenv("SYNCVIEW_DEPS_FAKE_MISSING");
    FakeCtx fake_ctx;

    if (fake && *fake) {
        log_user_action("Mancanze simulate (SYNCVIEW_DEPS_FAKE_MISSING)", fake);
    }

    /* Riproduzione: core di GStreamer e sink GTK. */
    {
        const char *const names[] = { "playbin3", NULL };
        const PackageSet *sets[] = { &PKG_BASE };
        DepsProbes p = probes_for(&probes, &fake_ctx, fake, "gst-playbin3");
        check_any_of(report, &p, "gst-playbin3", "Pipeline di riproduzione (playbin3)", DEPS_FEATURE_PLAYBACK,
                     names, "playbin3 non trovato", sets, 1, "GStreamer base");
    }
    {
        const char *const names[] = { "gtk4paintablesink", NULL };
        const PackageSet *sets[] = { &PKG_GTK4 };
        DepsProbes p = probes_for(&probes, &fake_ctx, fake, "gst-gtk4sink");
        check_any_of(report, &p, "gst-gtk4sink", "Sink video per GTK4 (gtk4paintablesink)", DEPS_FEATURE_PLAYBACK,
                     names, "gtk4paintablesink non trovato", sets, 1, "il plugin GStreamer gtk4 (gst-plugins-rs)");
    }

    {
        DepsProbes p = probes_for(&probes, &fake_ctx, fake, "gst-demuxers");
        check_demuxers(report, &p);
    }

    /* Decoder: H.264 richiesto, gli altri codec opzionali. */
    {
        const PackageSet *sets[] = { &PKG_LIBAV };
        DepsProbes p = probes_for(&probes, &fake_ctx, fake, "gst-decoder-h264");
        check_any_of(report, &p, "gst-decoder-h264", "Decoder H.264", DEPS_FEATURE_PLAYBACK, DECODERS_H264,
                     "Nessun decoder H.264", sets, 1, "i plugin GStreamer libav");
        p = probes_for(&probes, &fake_ctx, fake, "gst-decoder-hevc");
        check_any_of(report, &p, "gst-decoder-hevc", "Decoder H.265 / HEVC", DEPS_FEATURE_OPTIONAL, DECODERS_HEVC,
                     "Nessun decoder HEVC: i video H.265 (es. da iPhone) non saranno riproducibili", sets, 1,
                     "i plugin GStreamer libav");
    }
    {
        const PackageSet *sets[] = { &PKG_GOOD, &PKG_LIBAV };
        DepsProbes p = probes_for(&probes, &fake_ctx, fake, "gst-decoder-vp9");
        check_any_of(report, &p, "gst-decoder-vp9", "Decoder VP9", DEPS_FEATURE_OPTIONAL, DECODERS_VP9,
                     "Nessun decoder VP9", sets, 2, "i plugin GStreamer good e libav");
        p = probes_for(&probes, &fake_ctx, fake, "gst-decoder-av1");
        check_any_of(report, &p, "gst-decoder-av1", "Decoder AV1", DEPS_FEATURE_OPTIONAL, DECODERS_AV1,
                     "Nessun decoder AV1", sets, 2, "i plugin GStreamer good e libav");
    }

    {
        DepsProbes p = probes_for(&probes, &fake_ctx, fake, "gst-hw-decoders");
        check_hw_decoders(report, &p);
    }
    {
        DepsProbes p = probes_for(&probes, &fake_ctx, fake, "ffmpeg");
        check_ffmpeg(report, &p, deps_dir);
    }

    g_free(default_dir);
    return report;
}
