# SyncView-C

Riscrittura in **C puro (C11)** di SyncView — applicazione desktop per l'analisi video tattica multi-sync (fino a 4 video sincronizzati in griglia 2x2, timeline con markers, export clip via ffmpeg).

> Questo branch (`SyncView-C`) contiene **esclusivamente** la nuova implementazione in C. Il branch `main` resta la versione originale Python/PyQt6, invariata, ed è usata come riferimento comportamentale per il porting.

## Stato del progetto

🚧 **In sviluppo — milestone M0 (scaffolding) in corso.**

Il piano completo (contesto, analisi del codice originale, architettura, decisioni tecniche, milestone granulari e rischi noti) è in [PLAN.md](PLAN.md). Consultalo prima di contribuire: definisce la struttura del progetto, le librerie da usare e l'ordine di implementazione.

## Perché una riscrittura in C

L'app originale è Python/PyQt6. L'obiettivo di questo branch è ricreare la stessa logica di dominio (motore di sincronizzazione, gestione markers su SQLite, pipeline di export via ffmpeg) in C nativo, con una UI ridisegnata da zero (overhaul completo, non un clone pixel-per-pixel di Qt) basata su GTK4.

## Piattaforme target

L'app dovrà avere build native per **Linux, Windows e macOS**, tutte con comportamento equivalente.

- **Priorità del target di produzione**: macOS > Linux > Windows.
- **Capacità di test durante lo sviluppo**: Linux > Windows > macOS.

Questo squilibrio (il target più importante è il meno testabile in locale) è gestito con una pipeline CI multi-piattaforma (GitHub Actions, matrice Linux/Windows/macOS) introdotta fin dalle prime milestone — dettagli in [PLAN.md](PLAN.md#strategia-multi-piattaforma).

## Stack tecnico

- **Linguaggio**: C11
- **UI**: GTK4 (>= 4.10)
- **Playback video**: pipeline GStreamer manuale (`playbin3` + `gtk4paintablesink` su un `GtkPicture`) — non `GtkVideo`/`GtkMediaFile`, che non supportano il controllo del playback rate necessario per la compensazione FPS
- **Metadati video**: `GstDiscoverer` (`gstreamer-pbutils-1.0`), in-process, non più `ffprobe` come nella prima stesura del piano
- **Markers**: SQLite (schema portato 1:1 dall'originale)
- **I/O di configurazione**: GLib/GIO/json-glib
- **Export clip**: `ffmpeg` (subprocess, unica dipendenza binaria esterna rimasta, parallelizzato con `GThreadPool`)
- **Build system**: Meson

Dettagli completi, incluse le correzioni emerse dalla verifica cross-platform, in [PLAN.md](PLAN.md#architettura).

## Build

Prerequisiti (pacchetti di sviluppo, nomi validi su distro Arch-based; su altre distro i nomi package possono differire leggermente):

```bash
# Arch/CachyOS
sudo pacman -S meson ninja gtk4 gstreamer gst-plugins-base gst-plugins-good \
  gst-plugins-bad gst-libav gst-plugin-gtk4 sqlite json-glib
```

Build ed esecuzione:

```bash
meson setup build
meson compile -C build
./build/src/syncview
```

Test:

```bash
meson test -C build
```

Modalità debug (log verboso anche su terminale, vedi [PLAN.md](PLAN.md#modalità-debug-fin-dallinizio-non-rimandata-a-fine-progetto) — disponibile a partire da M1.14):

```bash
SYNCVIEW_DEBUG=1 ./build/src/syncview
```

## Struttura

```
meson.build
src/
  main.c, app.c/.h
  core/      # sync_manager, markers, marker_db, settings, user_paths, logger, discoverer, export_queue, export_worker
  video/     # video_player (GObject, pipeline playbin3+gtk4paintablesink), zoom_pan
  ui/        # main_window, video_grid, timeline_widget, sidebar, titlebar, dialoghi, style.css, keymap
  util/      # time_format
tests/       # unit test dei moduli core, senza dipendenza da GTK
docs/        # ARCHITECTURE.md, MIGRATION_NOTES.md
```

La struttura viene popolata incrementalmente milestone per milestone (vedi [PLAN.md](PLAN.md#milestone-granulari)); alcuni moduli elencati sopra non esistono ancora.

## Riferimento visivo

Il tema/UI si ispira al linguaggio visivo di [praesidium.artysan.me](https://praesidium.artysan.me/) (palette scura "Command", tipografia editoriale/monospace, componenti come lo zoom-stepper e i pannelli informativi) — dettagli in [PLAN.md](PLAN.md#riferimento-visivo-per-loverhaul-ui).

## Come contribuire

1. Leggi [PLAN.md](PLAN.md) per intero, in particolare le sezioni **Architettura** e **Milestone (granulari)**.
2. Procedi per sotto-milestone (es. M0.1, M0.2, ...), ciascuna con un criterio di verifica esplicito.
3. Dove il piano richiede di leggere un file Python originale per intero prima di scrivere il codice C corrispondente (segnalato nel piano), fallo sul branch `main` prima di procedere: l'obiettivo è un porting comportamentale fedele, non una ricostruzione a memoria.
4. Ogni nuovo modulo deve loggare tramite `core/logger.c` (categorie + modalità debug), mai con `printf`/`fprintf` diretti — vedi il vincolo architetturale in [PLAN.md](PLAN.md#modalità-debug-fin-dallinizio-non-rimandata-a-fine-progetto).

## Dipendenze runtime (a build completata)

- `gtk4` >= 4.10
- `gstreamer-1.0` + plugin **base + good + bad + libav** (i decoder H.264/HEVC e quelli hardware per piattaforma sono in `-bad`/`-libav`, non basta base+good — vedi [PLAN.md](PLAN.md#rischi-e-gap-noti))
- pacchetto `gst-plugin-gtk4` per `gtk4paintablesink` (necessario per il rendering video)
- `sqlite3`
- `glib-2.0`/`gio-2.0`, `json-glib-1.0`
- `ffmpeg` (binario esterno, solo per l'export — non più per il probing metadati)

Istruzioni di packaging per piattaforma (macOS `.app` bundle, Windows DLL bundling) verranno aggiunte in M8 (vedi [PLAN.md](PLAN.md)).

## Licenza

Tutti i diritti riservati.
