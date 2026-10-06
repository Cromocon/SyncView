/* M2.10: core/deps_check con sistemi finti (sonde iniettate) e qualche prova sulle sonde reali. */
#include "core/deps_check.h"
#include "core/logger.h"

#include <assert.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>

#define HAS(text, needle) (strstr((text), (needle)) != NULL)

/* ---------- sistema finto ---------- */
typedef struct {
    GHashTable *elements;    /* nome -> 1 */
    GHashTable *programs;    /* nome -> percorso (g_free) */
    char *ffmpeg_in_dir;     /* se impostato, ffmpeg viene "trovato" qui quando extra_dir coincide */
    char *last_extra_dir;    /* extra_dir ricevuto dall'ultima find_program("ffmpeg") */
    char *version_out;
    char *encoders_out;
    gboolean run_fails;
} Fake;

static const char *FFMPEG_VERSION =
    "ffmpeg version 7.1.1 Copyright (c) 2000-2025 the FFmpeg developers\nbuilt with gcc 14.2.1\n";
static const char *FFMPEG_ENCODERS =
    "Encoders:\n V..... = Video\n A..... = Audio\n ------\n"
    " V....D libx264              libx264 H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10 (codec h264)\n"
    " V....D h264_nvenc           NVIDIA NVENC H.264 encoder (codec h264)\n"
    " A....D aac                  AAC (Advanced Audio Coding)\n";

static gboolean
fake_has_element(const char *name, gpointer user_data)
{
    return g_hash_table_contains(((Fake *)user_data)->elements, name);
}

static char *
fake_find_program(const char *name, const char *extra_dir, gpointer user_data)
{
    Fake *f = user_data;

    if (strcmp(name, "ffmpeg") == 0) {
        g_free(f->last_extra_dir);
        f->last_extra_dir = g_strdup(extra_dir);
        if (f->ffmpeg_in_dir && extra_dir && strcmp(extra_dir, f->ffmpeg_in_dir) == 0) {
            return g_build_filename(extra_dir, "bin", "ffmpeg", NULL);
        }
    }
    const char *path = g_hash_table_lookup(f->programs, name);
    return path ? g_strdup(path) : NULL;
}

static char *
fake_run_program(const char *path, const char *const *args, gpointer user_data)
{
    (void)path;
    Fake *f = user_data;

    if (f->run_fails) {
        return NULL;
    }
    if (args[0] && strcmp(args[0], "-version") == 0) {
        return f->version_out ? g_strdup(f->version_out) : NULL;
    }
    if (args[0] && args[1] && strcmp(args[1], "-encoders") == 0) {
        return f->encoders_out ? g_strdup(f->encoders_out) : NULL;
    }
    return NULL;
}

static void
fake_add(Fake *f, const char *const *names)
{
    for (size_t i = 0; names[i]; i++) {
        g_hash_table_add(f->elements, g_strdup(names[i]));
    }
}

static void
fake_remove(Fake *f, const char *name)
{
    g_hash_table_remove(f->elements, name);
}

static Fake *
fake_new(DepsPlatform platform, DepsProbes *probes)
{
    Fake *f = g_new0(Fake, 1);

    f->elements = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    f->programs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    f->version_out = g_strdup(FFMPEG_VERSION);
    f->encoders_out = g_strdup(FFMPEG_ENCODERS);

    /* Sistema completo: tutto presente. */
    const char *all[] = { "playbin3", "gtk4paintablesink", "qtdemux", "avidemux", "matroskademux", "asfdemux",
                          "flvdemux", "avdec_h264", "avdec_h265", "vp9dec", "av1dec", NULL };
    fake_add(f, all);
    const char *hw_linux[] = { "vah264dec", NULL };
    const char *hw_windows[] = { "d3d11h264dec", NULL };
    const char *hw_macos[] = { "vtdec_hw", NULL };
    fake_add(f, platform == DEPS_PLATFORM_LINUX ? hw_linux : platform == DEPS_PLATFORM_WINDOWS ? hw_windows : hw_macos);
    g_hash_table_insert(f->programs, g_strdup("ffmpeg"), g_strdup("/usr/bin/ffmpeg"));

    probes->has_gst_element = fake_has_element;
    probes->find_program = fake_find_program;
    probes->run_program = fake_run_program;
    probes->platform = platform;
    probes->user_data = f;
    return f;
}

static void
fake_free(Fake *f)
{
    g_hash_table_destroy(f->elements);
    g_hash_table_destroy(f->programs);
    g_free(f->ffmpeg_in_dir);
    g_free(f->last_extra_dir);
    g_free(f->version_out);
    g_free(f->encoders_out);
    g_free(f);
}

static const DepsItem *
item(const DepsReport *r, const char *id)
{
    const DepsItem *it = deps_report_find(r, id);
    assert(it != NULL);
    return it;
}

/* ---------- test ---------- */

static void
test_everything_present(void)
{
    const DepsPlatform platforms[] = { DEPS_PLATFORM_LINUX, DEPS_PLATFORM_WINDOWS, DEPS_PLATFORM_MACOS };

    for (size_t p = 0; p < G_N_ELEMENTS(platforms); p++) {
        DepsProbes probes;
        Fake *f = fake_new(platforms[p], &probes);
        DepsReport *r = deps_check_run(&probes, "/deps");

        assert(deps_report_get_platform(r) == platforms[p]);
        assert(deps_report_count(r) == 9);
        assert(deps_report_can_play(r) && deps_report_can_export(r) && deps_report_is_complete(r));

        /* Id stabili e nell'ordine documentato. */
        const char *ids[] = { "gst-playbin3", "gst-gtk4sink", "gst-demuxers", "gst-decoder-h264", "gst-decoder-hevc",
                              "gst-decoder-vp9", "gst-decoder-av1", "gst-hw-decoders", "ffmpeg" };
        for (size_t i = 0; i < G_N_ELEMENTS(ids); i++) {
            const DepsItem *it = deps_report_get(r, i);
            assert(it != NULL && strcmp(it->id, ids[i]) == 0);
            assert(it->status == DEPS_STATUS_OK && it->resolution == DEPS_RESOLUTION_NONE);
            assert(*it->title && *it->detail && strcmp(it->instructions, "") == 0);
        }

        /* Feature di ciascun componente. */
        assert(item(r, "gst-gtk4sink")->feature == DEPS_FEATURE_PLAYBACK);
        assert(item(r, "gst-decoder-h264")->feature == DEPS_FEATURE_PLAYBACK);
        assert(item(r, "gst-decoder-hevc")->feature == DEPS_FEATURE_OPTIONAL);
        assert(item(r, "gst-hw-decoders")->feature == DEPS_FEATURE_OPTIONAL);
        assert(item(r, "ffmpeg")->feature == DEPS_FEATURE_EXPORT);

        assert(deps_report_get(r, 9) == NULL && deps_report_find(r, "inesistente") == NULL);
        deps_report_free(r);
        fake_free(f);
    }
    deps_report_free(NULL);
}

static void
test_required_playback_components(void)
{
    const char *required[] = { "playbin3", "gtk4paintablesink" };
    const char *ids[] = { "gst-playbin3", "gst-gtk4sink" };

    for (size_t i = 0; i < 2; i++) {
        DepsProbes probes;
        Fake *f = fake_new(DEPS_PLATFORM_LINUX, &probes);
        fake_remove(f, required[i]);
        DepsReport *r = deps_check_run(&probes, "/deps");

        const DepsItem *it = item(r, ids[i]);
        assert(it->status == DEPS_STATUS_MISSING && it->feature == DEPS_FEATURE_PLAYBACK);
        assert(it->resolution == DEPS_RESOLUTION_INSTRUCTIONS);
        assert(HAS(it->detail, required[i]));
        assert(!deps_report_can_play(r));
        assert(deps_report_can_export(r) && !deps_report_is_complete(r));  /* l'export non dipende dal playback */

        deps_report_free(r);
        fake_free(f);
    }
}

static void
test_h264_decoder_any_of(void)
{
    DepsProbes probes;
    Fake *f = fake_new(DEPS_PLATFORM_LINUX, &probes);

    /* Solo decoder hardware (niente software): sufficiente. */
    fake_remove(f, "avdec_h264");
    DepsReport *r = deps_check_run(&probes, "/deps");
    assert(item(r, "gst-decoder-h264")->status == DEPS_STATUS_OK);
    assert(HAS(item(r, "gst-decoder-h264")->detail, "vah264dec") && !HAS(item(r, "gst-decoder-h264")->detail, "avdec_h264"));
    assert(deps_report_can_play(r));
    deps_report_free(r);

    /* Nessun decoder H.264: il playback non è possibile. */
    fake_remove(f, "vah264dec");
    r = deps_check_run(&probes, "/deps");
    assert(item(r, "gst-decoder-h264")->status == DEPS_STATUS_MISSING);
    assert(!deps_report_can_play(r));
    assert(HAS(item(r, "gst-decoder-h264")->detail, "avdec_h264"));  /* elenca cosa ha cercato */
    deps_report_free(r);
    fake_free(f);
}

static void
test_optional_codecs(void)
{
    DepsProbes probes;
    Fake *f = fake_new(DEPS_PLATFORM_LINUX, &probes);
    fake_remove(f, "avdec_h265");
    fake_remove(f, "vp9dec");
    fake_remove(f, "av1dec");
    DepsReport *r = deps_check_run(&probes, "/deps");

    const char *ids[] = { "gst-decoder-hevc", "gst-decoder-vp9", "gst-decoder-av1" };
    for (size_t i = 0; i < 3; i++) {
        const DepsItem *it = item(r, ids[i]);
        assert(it->status == DEPS_STATUS_OPTIONAL_MISSING && it->feature == DEPS_FEATURE_OPTIONAL);
        assert(*it->instructions != '\0');
    }
    assert(HAS(item(r, "gst-decoder-hevc")->detail, "H.265"));
    assert(deps_report_can_play(r) && deps_report_can_export(r));  /* opzionali: nessuna funzione persa */
    assert(!deps_report_is_complete(r));

    deps_report_free(r);
    fake_free(f);
}

static void
test_demuxers(void)
{
    DepsProbes probes;
    Fake *f = fake_new(DEPS_PLATFORM_LINUX, &probes);

    /* wmv e flv con i soli demuxer di libav: accettati. */
    fake_remove(f, "asfdemux");
    fake_remove(f, "flvdemux");
    const char *libav[] = { "avdemux_asf", "avdemux_flv", NULL };
    fake_add(f, libav);
    DepsReport *r = deps_check_run(&probes, "/deps");
    assert(item(r, "gst-demuxers")->status == DEPS_STATUS_OK);
    assert(HAS(item(r, "gst-demuxers")->detail, "avdemux_asf"));
    deps_report_free(r);

    /* Manca qtdemux: mp4 e mov non riproducibili, gli altri formati restano. */
    fake_remove(f, "qtdemux");
    r = deps_check_run(&probes, "/deps");
    const DepsItem *it = item(r, "gst-demuxers");
    assert(it->status == DEPS_STATUS_MISSING && it->feature == DEPS_FEATURE_PLAYBACK);
    assert(HAS(it->detail, "Formati senza demuxer: mp4, mov"));
    assert(HAS(it->detail, "presenti:") && HAS(it->detail, "mkv (matroskademux)"));
    assert(!deps_report_can_play(r));
    deps_report_free(r);

    /* Manca solo il demuxer wmv (asfdemux è in plugins-ugly): opzionale, si riproduce il resto; il piano è ugly. */
    const char *qt[] = { "qtdemux", NULL };
    fake_add(f, qt);
    fake_remove(f, "avdemux_asf");
    r = deps_check_run(&probes, "/deps");
    it = item(r, "gst-demuxers");
    assert(it->status == DEPS_STATUS_OPTIONAL_MISSING && it->feature == DEPS_FEATURE_OPTIONAL);
    assert(HAS(it->detail, "Formati senza demuxer: wmv"));
    assert(deps_report_can_play(r));
    deps_report_free(r);

    /* Senza nessun demuxer: elencati tutti i formati. */
    const char *gone[] = { "avidemux", "matroskademux", "avdemux_asf", "avdemux_flv" };
    for (size_t i = 0; i < G_N_ELEMENTS(gone); i++) {
        fake_remove(f, gone[i]);
    }
    r = deps_check_run(&probes, "/deps");
    it = item(r, "gst-demuxers");
    assert(HAS(it->detail, "mp4, mov") && HAS(it->detail, "avi") && HAS(it->detail, "mkv") && HAS(it->detail, "wmv") && HAS(it->detail, "flv"));
    deps_report_free(r);
    fake_free(f);
}

static void
test_hw_decoders_per_platform(void)
{
    /* Ogni piattaforma riconosce i propri decoder hardware. */
    DepsProbes probes;
    Fake *f = fake_new(DEPS_PLATFORM_MACOS, &probes);
    DepsReport *r = deps_check_run(&probes, "/deps");
    assert(item(r, "gst-hw-decoders")->status == DEPS_STATUS_OK && HAS(item(r, "gst-hw-decoders")->detail, "vtdec_hw"));
    deps_report_free(r);
    fake_free(f);

    /* Un decoder Windows su Linux non conta come hardware di quella piattaforma. */
    f = fake_new(DEPS_PLATFORM_LINUX, &probes);
    fake_remove(f, "vah264dec");
    const char *wrong[] = { "d3d11h264dec", "vtdec_hw", NULL };
    fake_add(f, wrong);
    r = deps_check_run(&probes, "/deps");
    const DepsItem *it = item(r, "gst-hw-decoders");
    assert(it->status == DEPS_STATUS_OPTIONAL_MISSING && HAS(it->detail, "software"));
    assert(deps_report_can_play(r));
    deps_report_free(r);
    fake_free(f);
}

static void
test_ffmpeg(void)
{
    DepsProbes probes;
    Fake *f = fake_new(DEPS_PLATFORM_LINUX, &probes);

    /* Presente: versione e encoder H.264 riportati. */
    DepsReport *r = deps_check_run(&probes, "/deps");
    const DepsItem *it = item(r, "ffmpeg");
    assert(it->status == DEPS_STATUS_OK);
    assert(HAS(it->detail, "/usr/bin/ffmpeg") && HAS(it->detail, "7.1.1") && HAS(it->detail, "libx264") && HAS(it->detail, "h264_nvenc"));
    assert(!HAS(it->detail, "aac"));  /* solo encoder video */
    deps_report_free(r);

    /* Versione in formato git/dev. */
    g_free(f->version_out);
    f->version_out = g_strdup("ffmpeg version n7.0-12-gabcdef Copyright (c) the FFmpeg developers\n");
    r = deps_check_run(&probes, "/deps");
    assert(HAS(item(r, "ffmpeg")->detail, "n7.0-12-gabcdef"));
    deps_report_free(r);

    /* Solo un encoder hardware (niente libx264): accettato. */
    g_free(f->encoders_out);
    f->encoders_out = g_strdup(" V....D h264_videotoolbox   VideoToolbox H.264 Encoder (codec h264)\n");
    r = deps_check_run(&probes, "/deps");
    assert(item(r, "ffmpeg")->status == DEPS_STATUS_OK && HAS(item(r, "ffmpeg")->detail, "h264_videotoolbox"));
    deps_report_free(r);

    /* ffmpeg senza nessun encoder H.264: l'export non è possibile ma il playback sì. */
    g_free(f->encoders_out);
    f->encoders_out = g_strdup(" V....D mpeg4   MPEG-4 part 2\n A....D aac   AAC\n");
    r = deps_check_run(&probes, "/deps");
    it = item(r, "ffmpeg");
    assert(it->status == DEPS_STATUS_MISSING && HAS(it->detail, "nessun encoder H.264"));
    assert(deps_report_can_play(r) && !deps_report_can_export(r));
    deps_report_free(r);

    /* Conta solo il tipo "V" (video): una riga di altro tipo con quel nome non è un encoder H.264. */
    g_free(f->encoders_out);
    f->encoders_out = g_strdup(" A....D libx264   (riga di tipo audio, non conta)\n S....D h264_nvenc   (sottotitoli)\n");
    r = deps_check_run(&probes, "/deps");
    assert(item(r, "ffmpeg")->status == DEPS_STATUS_MISSING);
    deps_report_free(r);

    /* ffmpeg trovato ma non eseguibile. */
    f->run_fails = TRUE;
    r = deps_check_run(&probes, "/deps");
    assert(item(r, "ffmpeg")->status == DEPS_STATUS_MISSING && HAS(item(r, "ffmpeg")->detail, "non eseguibile"));
    assert(!deps_report_can_export(r));
    deps_report_free(r);
    f->run_fails = FALSE;

    /* Assente: istruzioni su ogni piattaforma (su Windows/macOS non c'è ancora un artefatto nel manifest incorporato). */
    g_hash_table_remove(f->programs, "ffmpeg");
    r = deps_check_run(&probes, "/deps");
    it = item(r, "ffmpeg");
    assert(it->status == DEPS_STATUS_MISSING && it->resolution == DEPS_RESOLUTION_INSTRUCTIONS);
    assert(HAS(it->detail, "non trovato") && HAS(it->detail, "/deps"));
    assert(deps_report_can_play(r) && !deps_report_can_export(r));
    deps_report_free(r);

    probes.platform = DEPS_PLATFORM_WINDOWS;
    r = deps_check_run(&probes, "/deps");
    assert(item(r, "ffmpeg")->resolution == DEPS_RESOLUTION_INSTRUCTIONS);
    assert(HAS(item(r, "ffmpeg")->instructions, "deps/bin") && HAS(item(r, "ffmpeg")->instructions, "ffmpeg.exe"));
    deps_report_free(r);
    probes.platform = DEPS_PLATFORM_MACOS;
    r = deps_check_run(&probes, "/deps");
    assert(item(r, "ffmpeg")->resolution == DEPS_RESOLUTION_INSTRUCTIONS);
    assert(HAS(item(r, "ffmpeg")->instructions, "brew install ffmpeg"));
    deps_report_free(r);

    fake_free(f);
}

static void
test_ffmpeg_search_directory(void)
{
    DepsProbes probes;
    Fake *f = fake_new(DEPS_PLATFORM_WINDOWS, &probes);

    /* ffmpeg scaricato in deps_dir: viene cercato per primo lì e trovato. */
    g_hash_table_remove(f->programs, "ffmpeg");
    f->ffmpeg_in_dir = g_strdup("/home/u/.syncview/deps");
    DepsReport *r = deps_check_run(&probes, "/home/u/.syncview/deps");
    assert(strcmp(f->last_extra_dir, "/home/u/.syncview/deps") == 0);
    assert(item(r, "ffmpeg")->status == DEPS_STATUS_OK && HAS(item(r, "ffmpeg")->detail, ".syncview/deps"));
    deps_report_free(r);

    /* deps_dir NULL -> directory di default. */
    r = deps_check_run(&probes, NULL);
    char *def = deps_default_dir();
    assert(strcmp(f->last_extra_dir, def) == 0);
    assert(g_str_has_suffix(def, "deps") && strstr(def, ".syncview") != NULL);
    g_free(def);
    deps_report_free(r);
    fake_free(f);
}

static void
assert_packages(const DepsItem *it, const char *pm, const char *const *expected)
{
    assert(it->resolution == DEPS_RESOLUTION_SYSTEM_PACKAGES);
    assert(it->package_manager && strcmp(it->package_manager, pm) == 0);
    assert(it->packages != NULL);
    size_t n = 0;
    for (; expected[n]; n++) {
        assert(it->packages[n] && strcmp(it->packages[n], expected[n]) == 0);
    }
    assert(it->packages[n] == NULL);  /* né più né meno di quelli attesi */
}

static void
assert_not_auto(const DepsItem *it)
{
    assert(it->resolution != DEPS_RESOLUTION_SYSTEM_PACKAGES);
    assert(it->package_manager == NULL && it->packages == NULL);
}

static void
test_system_packages_plan(void)
{
    /* Arch / Debian-Ubuntu / Fedora: il plugin gtk4 è installabile dall'app con i nomi verificati. */
    struct { const char *pm_program; const char *pm; const char *gtk4_pkg; const char *manual; } cases[] = {
        { "pacman", "pacman", "gst-plugin-gtk4", "sudo pacman -S gst-plugin-gtk4" },
        { "apt-get", "apt-get", "gstreamer1.0-gtk4", "sudo apt install gstreamer1.0-gtk4" },
        { "dnf", "dnf", "gstreamer1-plugin-gtk4", "sudo dnf install gstreamer1-plugin-gtk4" },
    };

    for (size_t i = 0; i < G_N_ELEMENTS(cases); i++) {
        DepsProbes probes;
        Fake *f = fake_new(DEPS_PLATFORM_LINUX, &probes);
        g_hash_table_insert(f->programs, g_strdup(cases[i].pm_program), g_strdup("/usr/bin/pm"));
        fake_remove(f, "gtk4paintablesink");

        DepsReport *r = deps_check_run(&probes, "/deps");
        const char *expected[] = { cases[i].gtk4_pkg, NULL };
        assert_packages(item(r, "gst-gtk4sink"), cases[i].pm, expected);
        assert(HAS(item(r, "gst-gtk4sink")->instructions, cases[i].manual));  /* ripiego a mano sempre presente */
        assert(item(r, "gst-gtk4sink")->status == DEPS_STATUS_MISSING);
        deps_report_free(r);
        fake_free(f);
    }

    /* Componente presente: nessun piano. */
    DepsProbes probes;
    Fake *f = fake_new(DEPS_PLATFORM_LINUX, &probes);
    g_hash_table_insert(f->programs, g_strdup("pacman"), g_strdup("/usr/bin/pacman"));
    DepsReport *r = deps_check_run(&probes, "/deps");
    for (size_t k = 0; k < deps_report_count(r); k++) {
        assert_not_auto(deps_report_get(r, k));
    }
    deps_report_free(r);

    /* Più set per lo stesso componente (demuxer: good + libav) e nessun duplicato. */
    fake_remove(f, "qtdemux");
    r = deps_check_run(&probes, "/deps");
    const char *good_libav[] = { "gst-plugins-good", "gst-libav", NULL };
    assert_packages(item(r, "gst-demuxers"), "pacman", good_libav);
    deps_report_free(r);

    /* ffmpeg assente su Arch/Debian: installabile; su Fedora e openSUSE no (RPM Fusion/Packman). */
    g_hash_table_remove(f->programs, "ffmpeg");
    r = deps_check_run(&probes, "/deps");
    const char *ff[] = { "ffmpeg", NULL };
    assert_packages(item(r, "ffmpeg"), "pacman", ff);
    deps_report_free(r);

    g_hash_table_remove(f->programs, "pacman");
    g_hash_table_insert(f->programs, g_strdup("apt-get"), g_strdup("/usr/bin/apt-get"));
    r = deps_check_run(&probes, "/deps");
    assert_packages(item(r, "ffmpeg"), "apt-get", ff);
    deps_report_free(r);

    g_hash_table_remove(f->programs, "apt-get");
    g_hash_table_insert(f->programs, g_strdup("dnf"), g_strdup("/usr/bin/dnf"));
    r = deps_check_run(&probes, "/deps");
    assert_not_auto(item(r, "ffmpeg"));
    assert(item(r, "ffmpeg")->resolution == DEPS_RESOLUTION_INSTRUCTIONS);
    assert(HAS(item(r, "ffmpeg")->instructions, "RPM Fusion"));
    deps_report_free(r);

    /* Fedora: anche gst-libav (decoder H.264 senza libav) è solo a mano; il plugin gtk4 invece è automatico. */
    fake_remove(f, "avdec_h264");
    fake_remove(f, "vah264dec");
    fake_remove(f, "gtk4paintablesink");
    r = deps_check_run(&probes, "/deps");
    assert_not_auto(item(r, "gst-decoder-h264"));
    assert(HAS(item(r, "gst-decoder-h264")->instructions, "RPM Fusion"));
    const char *gtk4_dnf[] = { "gstreamer1-plugin-gtk4", NULL };
    assert_packages(item(r, "gst-gtk4sink"), "dnf", gtk4_dnf);
    deps_report_free(r);

    /* openSUSE: nome del pacchetto gtk4 non verificato -> niente installazione automatica. */
    g_hash_table_remove(f->programs, "dnf");
    g_hash_table_insert(f->programs, g_strdup("zypper"), g_strdup("/usr/bin/zypper"));
    r = deps_check_run(&probes, "/deps");
    assert_not_auto(item(r, "gst-gtk4sink"));
    assert(item(r, "gst-gtk4sink")->resolution == DEPS_RESOLUTION_INSTRUCTIONS);
    assert(!HAS(item(r, "gst-gtk4sink")->instructions, "zypper install"));
    assert(HAS(item(r, "gst-gtk4sink")->instructions, "gst-plugin-gtk4") && HAS(item(r, "gst-gtk4sink")->instructions, "gstreamer1.0-gtk4"));
    deps_report_free(r);

    /* Nessun gestore di pacchetti riconosciuto: solo istruzioni generiche. */
    g_hash_table_remove(f->programs, "zypper");
    r = deps_check_run(&probes, "/deps");
    assert_not_auto(item(r, "gst-gtk4sink"));
    assert(HAS(item(r, "gst-gtk4sink")->instructions, "Fedora: gstreamer1-plugin-gtk4"));
    deps_report_free(r);
    fake_free(f);

    /* Windows e macOS: nessun pacchetto di sistema e nessun comando Linux. */
    f = fake_new(DEPS_PLATFORM_WINDOWS, &probes);
    fake_remove(f, "gtk4paintablesink");
    r = deps_check_run(&probes, "/deps");
    assert_not_auto(item(r, "gst-gtk4sink"));
    /* Windows: l'app scarica e lancia l'installer ufficiale (UAC); a mano restano il sito e i pacchetti MSYS2. */
    assert(item(r, "gst-gtk4sink")->resolution == DEPS_RESOLUTION_PLATFORM_INSTALLER);
    assert(HAS(item(r, "gst-gtk4sink")->instructions, "installer ufficiale di GStreamer") && HAS(item(r, "gst-gtk4sink")->instructions, "UAC"));
    assert(HAS(item(r, "gst-gtk4sink")->instructions, "gst-plugins-rs"));
    assert(!HAS(item(r, "gst-gtk4sink")->instructions, "sudo"));
    deps_report_free(r);
    fake_free(f);

    f = fake_new(DEPS_PLATFORM_MACOS, &probes);
    fake_remove(f, "gtk4paintablesink");
    r = deps_check_run(&probes, "/deps");
    assert_not_auto(item(r, "gst-gtk4sink"));
    assert(item(r, "gst-gtk4sink")->resolution == DEPS_RESOLUTION_PLATFORM_INSTALLER);
    assert(HAS(item(r, "gst-gtk4sink")->instructions, "installer ufficiale di GStreamer") && !HAS(item(r, "gst-gtk4sink")->instructions, "sudo"));
    assert(HAS(item(r, "gst-gtk4sink")->instructions, "Installer di macOS") && HAS(item(r, "gst-gtk4sink")->instructions, "gtk4paintablesink"));
    deps_report_free(r);
    fake_free(f);
}

static void
test_collect_packages(void)
{
    DepsProbes probes;
    Fake *f = fake_new(DEPS_PLATFORM_LINUX, &probes);
    g_hash_table_insert(f->programs, g_strdup("pacman"), g_strdup("/usr/bin/pacman"));

    /* Sistema completo: niente da installare. */
    DepsReport *r = deps_check_run(&probes, "/deps");
    char *pm = (char *)"non impostato";
    assert(deps_report_collect_packages(r, TRUE, &pm) == NULL && pm == NULL);
    deps_report_free(r);

    /* Mancano gtk4, H.264 (libav), HEVC (opzionale), ffmpeg e i decoder hardware (opzionale). */
    fake_remove(f, "gtk4paintablesink");
    fake_remove(f, "avdec_h264");
    fake_remove(f, "vah264dec");
    fake_remove(f, "avdec_h265");
    g_hash_table_remove(f->programs, "ffmpeg");
    r = deps_check_run(&probes, "/deps");

    /* Solo i richiesti: nell'ordine dei componenti, senza duplicati. */
    char **required = deps_report_collect_packages(r, FALSE, &pm);
    assert(required != NULL && pm && strcmp(pm, "pacman") == 0);
    assert(g_strv_length(required) == 3);
    assert(strcmp(required[0], "gst-plugin-gtk4") == 0 && strcmp(required[1], "gst-libav") == 0 && strcmp(required[2], "ffmpeg") == 0);
    g_strfreev(required);
    g_free(pm);

    /* Con gli opzionali: HEVC riusa gst-libav (già presente, nessun duplicato), i decoder hardware aggiungono gst-plugins-bad. */
    char **all = deps_report_collect_packages(r, TRUE, NULL);
    assert(all != NULL && g_strv_length(all) == 4);
    assert(strcmp(all[0], "gst-plugin-gtk4") == 0 && strcmp(all[1], "gst-libav") == 0);
    assert(strcmp(all[2], "gst-plugins-bad") == 0 && strcmp(all[3], "ffmpeg") == 0);  /* ordine dei componenti: hw prima di ffmpeg */
    for (guint a = 0; all[a]; a++) {
        for (guint b = a + 1; all[b]; b++) {
            assert(strcmp(all[a], all[b]) != 0);
        }
    }
    g_strfreev(all);
    deps_report_free(r);
    fake_free(f);

    /* Windows/macOS: nessun pacchetto di sistema. */
    f = fake_new(DEPS_PLATFORM_MACOS, &probes);
    fake_remove(f, "gtk4paintablesink");
    r = deps_check_run(&probes, "/deps");
    assert(deps_report_collect_packages(r, TRUE, &pm) == NULL && pm == NULL);
    deps_report_free(r);
    fake_free(f);
}

static void
test_text_and_log(const char *dir)
{
    DepsProbes probes;
    Fake *f = fake_new(DEPS_PLATFORM_LINUX, &probes);
    g_hash_table_insert(f->programs, g_strdup("pacman"), g_strdup("/usr/bin/pacman"));
    fake_remove(f, "gtk4paintablesink");
    fake_remove(f, "avdec_h265");
    DepsReport *r = deps_check_run(&probes, "/deps");

    char *text = deps_report_to_text(r);
    assert(HAS(text, "[OK]") && HAS(text, "[MANCANTE]") && HAS(text, "[OPZIONALE MANCANTE]"));
    assert(HAS(text, "gtk4paintablesink") && HAS(text, "(riproduzione)") && HAS(text, "(export)") && HAS(text, "(opzionale)"));
    assert(HAS(text, "SyncView può installarlo (pacman: gst-plugin-gtk4)") && HAS(text, "password di amministratore"));
    assert(HAS(text, "a mano: Installa il plugin GStreamer gtk4 (gst-plugins-rs): sudo pacman -S gst-plugin-gtk4"));
    assert(HAS(text, "Riproduzione: NON possibile | Export: possibile | Completo: no"));
    g_free(text);

    /* Log: riepilogo come azione utente e dettagli [GST] in debug. */
    char *log_path = g_build_filename(dir, "deps.log", NULL);
    g_unsetenv("SYNCVIEW_DEBUG");
    assert(logger_init(log_path, TRUE, NULL));
    deps_report_log(r);
    logger_shutdown();
    char *log = NULL;
    assert(g_file_get_contents(log_path, &log, NULL, NULL));
    assert(HAS(log, "[AZIONE UTENTE] Verifica dipendenze - riproduzione: NO, export: si, mancanti: 1, opzionali mancanti: 1"));
    assert(HAS(log, "[GST] deps: gst-gtk4sink [MANCANTE]") && HAS(log, "[GST] deps: gst-playbin3 [ok]"));
    g_free(log);
    g_free(log_path);

    deps_report_free(r);
    fake_free(f);
}

/* ---------- sonde reali ---------- */

static void
test_real_probes_invariants(void)
{
    DepsReport *r = deps_check_run(NULL, NULL);

    assert(r != NULL && deps_report_count(r) == 9);
    assert(deps_report_get_platform(r) == deps_current_platform());

    gboolean play = TRUE, export_ok = TRUE, complete = TRUE;
    for (size_t i = 0; i < deps_report_count(r); i++) {
        const DepsItem *it = deps_report_get(r, i);

        assert(*it->id && *it->title && *it->detail);
        if (it->status == DEPS_STATUS_OK) {
            assert(it->resolution == DEPS_RESOLUTION_NONE && *it->instructions == '\0');
        } else {
            assert(it->resolution != DEPS_RESOLUTION_NONE && *it->instructions != '\0');
            complete = FALSE;
        }
        if (it->status == DEPS_STATUS_MISSING) {
            play &= it->feature != DEPS_FEATURE_PLAYBACK;
            export_ok &= it->feature != DEPS_FEATURE_EXPORT;
        }
        /* Un componente opzionale non è mai "MISSING", uno richiesto mai "OPTIONAL_MISSING". */
        assert(it->feature == DEPS_FEATURE_OPTIONAL ? it->status != DEPS_STATUS_MISSING
                                                    : it->status != DEPS_STATUS_OPTIONAL_MISSING);
    }
    assert(deps_report_can_play(r) == play && deps_report_can_export(r) == export_ok);
    assert(deps_report_is_complete(r) == complete);

    char *text = deps_report_to_text(r);
    assert(HAS(text, "Riproduzione:"));
    g_free(text);
    deps_report_free(r);
}

#ifndef G_OS_WIN32
static void
write_script(const char *path, const char *body)
{
    char *content = g_strdup_printf("#!/bin/sh\n%s\n", body);
    assert(g_file_set_contents(path, content, -1, NULL));
    assert(g_chmod(path, 0755) == 0);
    g_free(content);
}

static void
test_real_probes_with_scripts(const char *dir)
{
    DepsProbes probes;
    deps_probes_default(&probes);
    assert(probes.has_gst_element && probes.find_program && probes.run_program);

    /* run_program: stdout, esito non zero, argomenti. */
    const char *hello_args[] = { "hello", "mondo", NULL };
    char *out = probes.run_program("/bin/echo", hello_args, NULL);
    assert(out && strcmp(out, "hello mondo\n") == 0);
    g_free(out);
    assert(probes.run_program("/bin/false", (const char *[]){ NULL }, NULL) == NULL);
    assert(probes.run_program("/percorso/che/non/esiste", (const char *[]){ NULL }, NULL) == NULL);

    /* Un processo che non termina viene terminato dopo ~5 s. */
    gint64 start = g_get_monotonic_time();
    const char *sleep_args[] = { "30", NULL };
    assert(probes.run_program("/bin/sleep", sleep_args, NULL) == NULL);
    gint64 elapsed_ms = (g_get_monotonic_time() - start) / 1000;
    assert(elapsed_ms >= 4500 && elapsed_ms < 10000);

    /* find_program: la directory extra ha la precedenza sul PATH, in <dir>/bin e in <dir>. */
    char *bin = g_build_filename(dir, "bin", NULL);
    assert(g_mkdir_with_parents(bin, 0755) == 0);
    char *fake_ffmpeg = g_build_filename(bin, "ffmpeg", NULL);
    write_script(fake_ffmpeg,
                 "case \"$1\" in\n"
                 "  -version) echo 'ffmpeg version 9.9-test Copyright'; echo 'built with test';;\n"
                 "  -hide_banner) echo 'Encoders:'; echo ' V....D libx264   libx264 H.264'; echo ' A....D aac  AAC';;\n"
                 "esac");
    char *found = probes.find_program("ffmpeg", dir, NULL);
    assert(found && strcmp(found, fake_ffmpeg) == 0);
    g_free(found);

    /* Un file non eseguibile non viene considerato. */
    assert(g_chmod(fake_ffmpeg, 0644) == 0);
    found = probes.find_program("ffmpeg-inesistente-xyz", dir, NULL);
    assert(found == NULL);
    assert(g_chmod(fake_ffmpeg, 0755) == 0);

    /* Sistema reale + ffmpeg finto in deps_dir: il controllo completo lo trova, ne legge versione ed encoder. */
    DepsReport *r = deps_check_run(NULL, dir);
    const DepsItem *it = deps_report_find(r, "ffmpeg");
    assert(it->status == DEPS_STATUS_OK);
    assert(HAS(it->detail, fake_ffmpeg) && HAS(it->detail, "9.9-test") && HAS(it->detail, "libx264"));
    deps_report_free(r);

    g_free(fake_ffmpeg);
    g_free(bin);
}
#endif

/* M2.12: SYNCVIEW_DEPS_FAKE_MISSING dichiara mancanti i componenti elencati; il resto del controllo resta vero. */
static void
test_fake_missing(void)
{
    DepsProbes probes;
    Fake *f = fake_new(DEPS_PLATFORM_LINUX, &probes);
    g_hash_table_insert(f->programs, g_strdup("pacman"), g_strdup("/usr/bin/pacman"));

    g_unsetenv("SYNCVIEW_DEPS_FAKE_MISSING");
    DepsReport *r = deps_check_run(&probes, "/deps");
    assert(deps_report_is_complete(r));
    deps_report_free(r);

    g_setenv("SYNCVIEW_DEPS_FAKE_MISSING", "gst-gtk4sink, ffmpeg,gst-decoder-hevc", TRUE);
    r = deps_check_run(&probes, "/deps");
    assert(item(r, "gst-gtk4sink")->status == DEPS_STATUS_MISSING);
    assert(item(r, "gst-gtk4sink")->resolution == DEPS_RESOLUTION_SYSTEM_PACKAGES);  /* piano vero, non finto */
    assert(item(r, "gst-gtk4sink")->packages && strcmp(item(r, "gst-gtk4sink")->packages[0], "gst-plugin-gtk4") == 0);
    assert(item(r, "ffmpeg")->status == DEPS_STATUS_MISSING);
    assert(item(r, "gst-decoder-hevc")->status == DEPS_STATUS_OPTIONAL_MISSING);
    assert(item(r, "gst-playbin3")->status == DEPS_STATUS_OK && item(r, "gst-decoder-h264")->status == DEPS_STATUS_OK);
    assert(!deps_report_can_play(r) && !deps_report_can_export(r));
    deps_report_free(r);

    /* «all» e id sconosciuti. */
    g_setenv("SYNCVIEW_DEPS_FAKE_MISSING", "all", TRUE);
    r = deps_check_run(&probes, "/deps");
    for (size_t i = 0; i < deps_report_count(r); i++) {
        assert(deps_report_get(r, i)->status != DEPS_STATUS_OK);
    }
    deps_report_free(r);
    g_setenv("SYNCVIEW_DEPS_FAKE_MISSING", "inesistente,,", TRUE);
    r = deps_check_run(&probes, "/deps");
    assert(deps_report_is_complete(r));
    deps_report_free(r);

    /* Comando per l'installazione a mano, per qualunque gestore (anche diverso da quello del sistema). */
    g_setenv("SYNCVIEW_DEPS_FAKE_MISSING", "gst-gtk4sink,gst-decoder-h264,ffmpeg,gst-decoder-vp9", TRUE);
    r = deps_check_run(&probes, "/deps");
    char *note = NULL;
    char *cmd = deps_report_manual_command(r, "apt-get", FALSE, &note);
    assert(cmd && strcmp(cmd, "sudo apt install gstreamer1.0-gtk4 gstreamer1.0-libav ffmpeg") == 0 && note == NULL);
    g_free(cmd);
    cmd = deps_report_manual_command(r, "apt-get", TRUE, NULL);  /* con gli opzionali: + good (per VP9) */
    assert(cmd && strstr(cmd, "gstreamer1.0-plugins-good") != NULL);
    g_free(cmd);
    cmd = deps_report_manual_command(r, "pacman", FALSE, NULL);
    assert(cmd && strcmp(cmd, "sudo pacman -S gst-plugin-gtk4 gst-libav ffmpeg") == 0);
    g_free(cmd);
    cmd = deps_report_manual_command(r, "dnf", FALSE, &note);  /* libav e ffmpeg non sono nei repository predefiniti */
    assert(cmd && strcmp(cmd, "sudo dnf install gstreamer1-plugin-gtk4") == 0);
    assert(note && strstr(note, "RPM Fusion") != NULL);
    g_free(cmd);
    g_free(note);
    assert(deps_report_manual_command(r, "brew", FALSE, NULL) == NULL && deps_report_manual_command(r, NULL, FALSE, NULL) == NULL);
    deps_report_free(r);

    g_unsetenv("SYNCVIEW_DEPS_FAKE_MISSING");
    r = deps_check_run(&probes, "/deps");
    assert(deps_report_manual_command(r, "apt-get", TRUE, &note) == NULL && note == NULL);
    deps_report_free(r);
    fake_free(f);
}

int
main(void)
{
    char *dir = g_dir_make_tmp("syncview-deps-XXXXXX", NULL);
    assert(dir != NULL);

    test_everything_present();
    test_required_playback_components();
    test_h264_decoder_any_of();
    test_optional_codecs();
    test_demuxers();
    test_hw_decoders_per_platform();
    test_ffmpeg();
    test_ffmpeg_search_directory();
    test_system_packages_plan();
    test_collect_packages();
    test_text_and_log(dir);
    test_fake_missing();
    test_real_probes_invariants();
#ifndef G_OS_WIN32
    test_real_probes_with_scripts(dir);
#endif

    char *cmd = g_strdup_printf("rm -rf '%s'", dir);
    assert(system(cmd) == 0);
    g_free(cmd);
    g_free(dir);
    return 0;
}
