# Architettura — SyncView-C

Questo documento è aggiornato progressivamente man mano che i moduli vengono implementati (vedi milestone in [../PLAN.md](../PLAN.md)).

Per il riferimento architetturale completo (non ancora implementato) vedi la sezione [Architettura](../PLAN.md#architettura) di `PLAN.md`: toolkit e librerie, struttura directory, moduli core, playback video, timeline widget, export pipeline, dialoghi/stato UI, modalità debug, scorciatoie cross-platform, finestra frameless.

## Stato implementativo

- **M0 (Scaffolding)**: ✅ completa (M0.1-M0.6). Build Meson funzionante, tutte le dipendenze collegate, CI multi-piattaforma (Linux/macOS/Windows) verde.
- **M1 (Core logic, nessuna dipendenza da GTK)**: 🚧 in corso — completate M1.1-M1.8, mancano M1.9-M1.15 ( marker_db/SQLite, migrazione JSON, user_paths, logger+debug mode, ASan finale).
- **M2-M8**: non ancora iniziate.

### Moduli implementati

| Modulo | File | Responsabilità | API pubblica principale |
|---|---|---|---|
| `util/time_format` | `src/util/time_format.{c,h}` | Formattazione durate ms → `HH:MM:SS.mmm` | `syncview_format_time_ms()` |
| `core/settings` | `src/core/settings.{c,h}` | Costanti di dominio (porting parziale di `config/settings.py`) | `SYNCVIEW_MAX_VIDEOS`, `syncview_supported_video_extensions[]`, `syncview_fps_presets[]`, `syncview_frame_step_options_ms[]`, costanti zoom/export |
| `core/sync_manager` | `src/core/sync_manager.{c,h}` | Motore di sincronizzazione offset-based (porting 1:1 di `core/sync_manager.py`) | `sync_manager_init/set_enabled/is_enabled/set_offset/get_offset/reset_offsets/set_master/get_master`, `sync_manager_calculate_sync_position`, `sync_manager_sync_all_to_master` (con `SyncPlayerOps` opachi) |
| `core/markers` | `src/core/markers.{c,h}` | Struct `Marker` (porting 1:1 della dataclass Python) + `MarkerStore` ordinato per timestamp | `marker_new()`, `marker_free()`, `marker_store_new/free/count/get/find_by_id/add/add_marker/remove/update` |

Tutti i moduli sopra sono compilati in `libsyncview_core` (static library, `src/meson.build`), linkata sia dall'eseguibile `syncview` che da ogni test in `tests/` — nessuna dipendenza da GTK, quindi testabili senza display (vedi [TESTING.md](../TESTING.md)).

### Decisioni prese durante l'implementazione (deviazioni dal piano/originale)

- **`sync_manager` è logica pura, senza logging embedded**: l'originale Python chiamava `logger.log_user_action` direttamente dentro i setter di `SyncManager`. Nel rewrite C questo è stato deliberatamente rimosso dal modulo: il logging dell'azione utente sarà responsabilità del livello UI/controller (M3+), quando esisterà `core/logger.c` (M1.14). Il modulo resta testabile in isolamento.
- **`sync_all_to_master` usa callback separati `seek`/`pause`** (non un `seek_and_pause` combinato come ipotizzato in una bozza iniziale del piano) per riprodurre esattamente le due chiamate distinte dell'originale (`seek_position()` poi `pause()`), verificabile nei test con l'ordine delle chiamate.
- **`core/markers` usa GLib (`GDateTime`)** per generare `created_at`/l'epoch usato nell'`id`, invece di API POSIX pure (`clock_gettime`) — scelta per coerenza cross-platform (comportamento identico su Linux/macOS/Windows) e perché GLib è comunque una dipendenza già presente. Prima introduzione di GLib in un modulo `core/`; `src/meson.build` è stato riorganizzato per dichiarare le `dependency()` prima della libreria statica, cui viene passato `glib_dep` — la propagazione ai target che fanno `link_with` funziona senza bisogno di ridichiarare la dipendenza nei test.
- **L'`id` dei marker non è byte-identico all'originale**: stessa forma (`marker_<timestamp_ms>_<epoch_seconds>`), ma la rappresentazione del numero in virgola mobile non è garantita identica a quella di Python — è un id opaco (mai parsato), l'unicità/forma sono ciò che conta.
- **Nessun campo `updated_at` nella struct `Marker`**: confermato leggendo `core/markers.py` per intero che la dataclass originale non ce l'ha — viene aggiunto solo a livello di schema SQLite al salvataggio (`core/marker_db.c`, M1.9+).
- **`MarkerStore` usa inserimento ordinato (upper bound) invece di append + sort**: stesso risultato dell'originale (sort stabile: a parità di timestamp vince l'ordine di inserimento). `marker_store_update` prende una struct `MarkerUpdate` con bitmask `fields` al posto dei `**kwargs` Python; 
- **`marker_store_add` ritorna `const Marker*` non owned** (vale fino a remove/free); `marker_store_add_marker` accetta un `Marker*` già costruito (ownership trasferita) per il caricamento dal DB (M1.10).
- **Query di `MarkerStore` replicano le scansioni lineari di `MarkerManager`, non `MarkerSpatialIndex`**: `get_at` ha tolleranza inclusiva e a pareggio di distanza (o timestamp duplicati) vince il marker successivo nell'ordine, come `get_marker_at` (`<=`). `find_nearest` dello spatial index (vince il precedente a pareggio) non è portato. `get_range` ritorna `(first_index, count)` perché i risultati sono contigui, senza allocare liste.

## Note per chi implementa

Man mano che ogni modulo (`core/`, `video/`, `ui/`, `util/`) viene scritto, aggiungere qui una riga nella tabella sopra e, se rilevante, una voce nella sezione "Decisioni prese durante l'implementazione". Le deviazioni note rispetto al comportamento dell'app Python originale vanno invece in `MIGRATION_NOTES.md` (non ancora creato — richiesto per la prima volta in M2.1).
