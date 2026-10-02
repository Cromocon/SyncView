#ifndef SYNCVIEW_CORE_DEP_MANIFEST_H
#define SYNCVIEW_CORE_DEP_MANIFEST_H

#include "core/deps_check.h"

#include <glib.h>

/*
 * Manifest degli artefatti che l'app può scaricare (M2.11), INCORPORATO nel programma: URL, versione, dimensione e
 * SHA-256 sono fissati qui e non vengono mai letti da un file o da un server. L'artefatto scaricato si verifica
 * (dimensione esatta + SHA-256) PRIMA di essere estratto o eseguito.
 *
 * Oggi contiene solo gli installer ufficiali di GStreamer per Windows e macOS (decisione presa: l'app scarica e lancia
 * l'installer ufficiale con l'elevazione del sistema; nessun bundle dei plugin). Gli SHA-256 sono quelli pubblicati da
 * GStreamer accanto a ogni installer (file `.sha256sum`, 2 ottobre 2026); chi aggiorna la versione deve rileggerli da
 * lì e, per autenticità, controllare anche la firma `.asc` del file. ffmpeg per Windows/macOS NON è nel manifest: dove
 * ospitare gli artefatti non ufficiali è ancora una decisione aperta (vedi PLAN.md), quindi per ora è «istruzioni».
 */
typedef enum {
    DEP_ARTIFACT_PLATFORM_INSTALLER,  /* installer ufficiale (.exe/.pkg): scaricato, verificato e lanciato con l'elevazione di sistema */
    DEP_ARTIFACT_ARCHIVE,             /* archivio .zip da estrarre in ~/.syncview/deps (senza privilegi) */
} DepArtifactKind;

typedef struct {
    const char *id;           /* id stabile, es. "gstreamer-official-installer" */
    DepArtifactKind kind;
    DepsPlatform platform;
    const char *arch;         /* "x86_64", "arm64" o "universal" */
    const char *version;
    const char *url;          /* https */
    const char *sha256;       /* 64 cifre esadecimali minuscole */
    gint64 size_bytes;        /* dimensione esatta */
    const char *filename;     /* nome del file scaricato (senza percorso) */
    /* Solo ARCHIVE: sottodirectory di ~/.syncview/deps in cui installare e componenti iniziali del percorso da scartare. */
    const char *install_subdir;
    int strip_components;
    const char *title;        /* come lo mostra l'interfaccia */
} DepArtifact;

/* Architettura del programma in esecuzione ("x86_64", "arm64"; "universal" su macOS), compile-time. */
const char *dep_manifest_current_arch(void);

/* Artefatto per id, piattaforma e architettura; NULL se non c'è. */
const DepArtifact *dep_manifest_find(const char *id, DepsPlatform platform, const char *arch);

/* L'installer ufficiale di GStreamer per la piattaforma e l'architettura correnti, o NULL se non disponibile. */
const DepArtifact *dep_manifest_gstreamer_installer(DepsPlatform platform);

/* Tutti gli artefatti (per i test): ritorna il numero e, se `out` non è NULL, il puntatore alla tabella. */
size_t dep_manifest_all(const DepArtifact **out);

/* Controllo di forma di una voce: HTTPS, SHA-256 di 64 cifre esadecimali, nome file semplice, dimensione > 0. */
gboolean dep_artifact_is_well_formed(const DepArtifact *artifact);

#endif /* SYNCVIEW_CORE_DEP_MANIFEST_H */
