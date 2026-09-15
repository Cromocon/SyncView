# SyncView-C

Riscrittura in **C puro (C11)** di SyncView — applicazione desktop per l'analisi video tattica multi-sync (fino a 4 video sincronizzati in griglia 2x2, timeline con markers, export clip via ffmpeg).

> Questo branch (`SyncView-C`) contiene **esclusivamente** la nuova implementazione in C. Il branch `main` resta la versione originale Python/PyQt6, invariata, ed è usata come riferimento comportamentale per il porting.

## Stato del progetto

🚧 **In pianificazione — nessun codice ancora presente.**

Il piano completo (contesto, analisi del codice originale, architettura, decisioni tecniche, milestone granulari e rischi noti) è in [PLAN.md](PLAN.md). Consultalo prima di contribuire: definisce la struttura del progetto, le librerie da usare e l'ordine di implementazione.

## Perché una riscrittura in C

L'app originale è Python/PyQt6. L'obiettivo di questo branch è ricreare la stessa logica di dominio (motore di sincronizzazione, gestione markers su SQLite, pipeline di export via ffmpeg) in C nativo, con una UI ridisegnata da zero (overhaul completo, non un clone pixel-per-pixel di Qt) basata su GTK4.

## Stack tecnico previsto

- **Linguaggio**: C11
- **UI**: GTK4 (>= 4.10)
- **Playback video**: `GtkVideo`/`GtkMediaFile` (backend GStreamer)
- **Metadati video**: `ffprobe` (subprocess, come nell'originale)
- **Markers**: SQLite (schema portato 1:1 dall'originale)
- **I/O di configurazione**: GLib/GIO/json-glib
- **Export clip**: `ffmpeg` (subprocess, parallelizzato con `GThreadPool`)
- **Build system**: Meson

Dettagli completi in [PLAN.md](PLAN.md#architettura).

## Struttura prevista

```
meson.build
src/
  main.c, app.c/.h
  core/      # sync_manager, markers, marker_db, settings, user_paths, logger, ffprobe, export_queue, export_worker
  video/     # video_player (wrapper GObject su GtkVideo), zoom_pan
  ui/        # main_window, video_grid, timeline_widget, sidebar, titlebar, dialoghi, style.css, keymap
  util/      # time_format
tests/       # unit test dei moduli core, senza dipendenza da GTK
docs/        # ARCHITECTURE.md, MIGRATION_NOTES.md
```

La struttura definitiva verrà creata a partire dalla milestone M0 (scaffolding) descritta in [PLAN.md](PLAN.md#milestone-granulari).

## Riferimento visivo

Il tema/UI si ispira al linguaggio visivo di [praesidium.artysan.me](https://praesidium.artysan.me/) (palette scura "Command", tipografia editoriale/monospace, componenti come lo zoom-stepper e i pannelli informativi) — dettagli in [PLAN.md](PLAN.md#riferimento-visivo-per-loverhaul-ui).

## Come contribuire

1. Leggi [PLAN.md](PLAN.md) per intero, in particolare le sezioni **Architettura** e **Milestone (granulari)**.
2. Procedi per sotto-milestone (es. M0.1, M0.2, ...), ciascuna con un criterio di verifica esplicito.
3. Dove il piano richiede di leggere un file Python originale per intero prima di scrivere il codice C corrispondente (segnalato nel piano), fallo sul branch `main` prima di procedere: l'obiettivo è un porting comportamentale fedele, non una ricostruzione a memoria.

## Dipendenze runtime previste (a build completata)

- `gtk4` >= 4.10
- `gstreamer-1.0` (+ plugin base/good/bad a seconda dei codec target)
- `sqlite3`
- `glib-2.0`/`gio-2.0`, `json-glib-1.0`
- `ffmpeg`/`ffprobe` (binari esterni, per metadati ed export)

Istruzioni di build dettagliate verranno aggiunte a partire da M0.5/M8.3 (vedi [PLAN.md](PLAN.md)).

## Licenza

Tutti i diritti riservati.
