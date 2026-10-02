#include "core/dep_manifest.h"

#include <string.h>

/* Versione 1.28.7 (2 ottobre 2026): include gtk4paintablesink. SHA-256 e dimensioni da gstreamer.freedesktop.org/data/pkg. */
static const DepArtifact MANIFEST[] = {
    {
        "gstreamer-official-installer", DEP_ARTIFACT_PLATFORM_INSTALLER, DEPS_PLATFORM_WINDOWS, "x86_64", "1.28.7",
        "https://gstreamer.freedesktop.org/data/pkg/windows/1.28.7/msvc/gstreamer-1.0-msvc-x86_64-1.28.7.exe",
        "032fc6062b8539838fc8da22589cb9b24c5d820baa7f8cc160af9ea08395badf", 526852553,
        "gstreamer-1.0-msvc-x86_64-1.28.7.exe", NULL, 0, "Installer ufficiale di GStreamer 1.28.7 (Windows, x86_64)",
    },
    {
        "gstreamer-official-installer", DEP_ARTIFACT_PLATFORM_INSTALLER, DEPS_PLATFORM_WINDOWS, "arm64", "1.28.7",
        "https://gstreamer.freedesktop.org/data/pkg/windows/1.28.7/msvc/gstreamer-1.0-msvc-arm64-1.28.7.exe",
        "eb8bd2547d52a96c570f82b0c581fa5a8992f672aba7fb9668370536f203d1f8", 314397421,
        "gstreamer-1.0-msvc-arm64-1.28.7.exe", NULL, 0, "Installer ufficiale di GStreamer 1.28.7 (Windows, ARM64)",
    },
    {
        "gstreamer-official-installer", DEP_ARTIFACT_PLATFORM_INSTALLER, DEPS_PLATFORM_MACOS, "universal", "1.28.7",
        "https://gstreamer.freedesktop.org/data/pkg/osx/1.28.7/gstreamer-1.0-1.28.7-universal.pkg",
        "529fdf4a4027d942e59b5b3564f6400adaa008f63ce5f3fed4ffe35d73911994", 153594157,
        "gstreamer-1.0-1.28.7-universal.pkg", NULL, 0, "Installer ufficiale di GStreamer 1.28.7 (macOS)",
    },
};

const char *
dep_manifest_current_arch(void)
{
#if defined(__APPLE__)
    return "universal";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#else
    return "x86_64";
#endif
}

size_t
dep_manifest_all(const DepArtifact **out)
{
    if (out) {
        *out = MANIFEST;
    }
    return G_N_ELEMENTS(MANIFEST);
}

const DepArtifact *
dep_manifest_find(const char *id, DepsPlatform platform, const char *arch)
{
    for (size_t i = 0; i < G_N_ELEMENTS(MANIFEST); i++) {
        if (strcmp(MANIFEST[i].id, id) == 0 && MANIFEST[i].platform == platform && strcmp(MANIFEST[i].arch, arch) == 0) {
            return &MANIFEST[i];
        }
    }
    return NULL;
}

const DepArtifact *
dep_manifest_gstreamer_installer(DepsPlatform platform)
{
    /* Su macOS l'installer è universale; su Windows conta l'architettura del programma (x86_64 se sconosciuta). */
    const char *arch = platform == DEPS_PLATFORM_MACOS ? "universal" : dep_manifest_current_arch();

    if (platform == DEPS_PLATFORM_WINDOWS && strcmp(arch, "universal") == 0) {
        arch = "x86_64";
    }
    return dep_manifest_find("gstreamer-official-installer", platform, arch);
}

static gboolean
is_hex_sha256(const char *s)
{
    if (!s || strlen(s) != 64) {
        return FALSE;
    }
    for (int i = 0; i < 64; i++) {
        if (!g_ascii_isxdigit(s[i]) || g_ascii_isupper(s[i])) {
            return FALSE;
        }
    }
    return TRUE;
}

gboolean
dep_artifact_is_well_formed(const DepArtifact *a)
{
    if (!a || !a->id || !a->url || !g_str_has_prefix(a->url, "https://") || !is_hex_sha256(a->sha256) ||
        a->size_bytes <= 0 || !a->filename || !*a->filename || strchr(a->filename, '/') || strchr(a->filename, '\\') ||
        strstr(a->filename, "..")) {
        return FALSE;
    }
    if (a->kind == DEP_ARTIFACT_ARCHIVE &&
        (!a->install_subdir || !*a->install_subdir || a->install_subdir[0] == '/' || strstr(a->install_subdir, "..") ||
         strchr(a->install_subdir, '\\') || a->strip_components < 0)) {
        return FALSE;
    }
    return TRUE;
}
