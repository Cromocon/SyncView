# TESTING — SyncView-C

Questo documento spiega come avviare il progetto e cosa testare manualmente allo stato attuale. Viene aggiornato ad ogni milestone con i nuovi elementi testabili — copre **M0 (Scaffolding)** completo e **M1 (Core logic)** fino a M1.6 incluso.

Per il contesto completo (architettura, milestone, rischi) vedi [PLAN.md](PLAN.md). Per le istruzioni di build sintetiche vedi anche [README.md](README.md#build).

## Come avviare

### 1. Prerequisiti

Su Arch/CachyOS (sistema di sviluppo di riferimento):

```bash
sudo pacman -S meson ninja gtk4 gstreamer gst-plugins-base gst-plugins-good \
  gst-plugins-bad gst-libav gst-plugin-gtk4 sqlite json-glib
```

Su altre distribuzioni Linux i nomi dei pacchetti di sviluppo cambiano leggermente (es. Debian/Ubuntu: `libgtk-4-dev`, `libgstreamer1.0-dev`, `libgstreamer-plugins-base1.0-dev`, `gstreamer1.0-plugins-good`, `gstreamer1.0-plugins-bad`, `libsqlite3-dev`, `libjson-glib-dev` — vedi `.github/workflows/ci.yml` per l'elenco esatto usato in CI).

### 2. Build

```bash
meson setup build
meson compile -C build
```

Build pulita attesa: nessun errore, nessun warning (il progetto usa `warning_level=2`).

### 3. Esecuzione

```bash
./build/src/syncview
```

### 4. Test automatici

```bash
meson test -C build
```

### 5. Reconfigure (dopo modifiche a `meson.build`)

Se si modificano le dipendenze o i file elencati in un `meson.build`, serve un reconfigure esplicito:

```bash
meson setup --reconfigure build
meson compile -C build
```

---

## Checklist di test manuale — M0 (Scaffolding)

Ogni voce corrisponde a una sotto-milestone già implementata e pushata su `SyncView-C`.

### M0.1 — Scaffolding Meson minimale

- [ ] `meson setup build` termina senza errori.
- [ ] `meson compile -C build` termina senza errori né warning.
- [ ] `./build/src/syncview` viene eseguito e termina con **exit code 0**.

### M0.2 — GTK4 + finestra vuota

- [ ] Lanciando `./build/src/syncview` si apre una finestra GTK intitolata "SyncView", dimensione iniziale 800x600.
- [ ] La finestra resta visibile e reattiva (nessun crash/freeze) per almeno qualche secondo.
- [ ] Chiudendo la finestra (bottone di chiusura del window manager) il processo termina senza errori in console.

### M0.3 — Tutte le dipendenze collegate

- [ ] `meson setup build` risolve tutte le dipendenze dichiarate in `src/meson.build` (gtk4, gstreamer-1.0, gstreamer-pbutils-1.0, gstreamer-video-1.0, sqlite3, glib-2.0, gio-2.0, json-glib-1.0) senza errori — visibile nell'output come `Run-time dependency <nome> found: YES <versione>`.
- [ ] Verifica indipendente che il plugin GStreamer per il rendering video sia installato e caricabile (necessario più avanti in M2, ma utile controllarlo già ora):
  ```bash
  gst-inspect-1.0 gtk4paintablesink
  ```
  Deve stampare i dettagli del plugin (`Plugin Details: Name gtk4 ...`), non un errore "No such element or plugin".

### M0.4 — Test scaffolding

- [ ] `meson test -C build` esegue e riporta **1/1 OK** (`syncview:dummy`).
- [ ] Il log completo è consultabile in `build/meson-logs/testlog.txt`.

### M0.5 — Documentazione

- [ ] [README.md](README.md) è presente e descrive correttamente lo stack attuale (pipeline GStreamer manuale, `GstDiscoverer`, plugin base+good+bad+libav).
- [ ] [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) è presente (stub).
- [ ] Dopo una build (`meson setup build && meson compile -C build`), `git status` non mostra la directory `build/` tra i file non tracciati (deve essere ignorata da `.gitignore`).

### M0.6 — CI multi-piattaforma

- [ ] Il workflow `.github/workflows/ci.yml` esiste e definisce tre job: `build-linux`, `build-macos`, `build-windows`.
- [ ] Ultimo run su `SyncView-C` verde su tutte e tre le piattaforme — verificabile con:
  ```bash
  gh run list --branch SyncView-C --limit 5
  gh run view <run-id>
  ```
  o direttamente dalla tab **Actions** del repository su GitHub.
- [ ] Nota: un'annotazione di GitHub sulla deprecazione di Node.js 20 nelle action JS (`actions/checkout@v4`) può comparire nei log — è un avviso infrastrutturale di GitHub Actions (i runner vengono automaticamente forzati su una versione Node più recente e supportata), non riguarda il codice C del progetto e non richiede alcuna azione da parte nostra.

---

## Checklist di test manuale — M1 (Core logic, in corso)

Nessuna dipendenza da GTK: tutto è verificabile via `meson test -C build` (test automatici) — non c'è ancora un'interfaccia grafica da usare per questi moduli. Per vedere singolarmente l'output di ogni test:

```bash
meson test -C build -v              # verboso, mostra output di ogni test
meson test -C build --suite syncview  # (equivalente, tutti i test sono nel progetto "syncview")
./build/tests/test_sync_manager     # esegue un singolo eseguibile di test direttamente
```

Un test C usa `assert()`: se passa non stampa nulla ed esce con codice 0; se una asserzione fallisce, il processo va in crash (`Aborted`) e `meson test` lo segna come `FAIL`, con lo stack nel log (`build/meson-logs/testlog.txt`).

### M1.1 — `util/time_format`

- [ ] `meson test -C build` include `syncview:time_format` → **OK**.
- [ ] Copre: 0ms → `00:00:00.000`, millisecondi singoli, minuti/ore, oltre 1h, valori negativi clampati a 0.

### M1.2 — `core/settings`

- [ ] `syncview:settings` → **OK**.
- [ ] Copre: `MAX_VIDEOS=4`, 6 formati video supportati, 7 preset FPS, 3 opzioni di frame-step (40/100/200ms), costanti zoom/export.

### M1.3 + M1.4 + M1.5 — `core/sync_manager`

- [ ] `syncview:sync_manager` → **OK**.
- [ ] Copre: stato iniziale (sync abilitato, offset a 0, master=0), getter/setter con bound-check, `calculate_sync_position` (offset uguali/diversi/clamp a 0/source==target), `sync_all_to_master` con player mock (seek prima di pause, master solo in pausa, slot non caricato o `NULL` ignorato).

### M1.6 — `core/markers`

- [ ] `syncview:markers` → **OK**.
- [ ] Copre: default `description=""`/`category="default"`, generazione `id`/`created_at`, unicità dell'`id` anche per marker con lo stesso timestamp, `marker_free(NULL)` sicuro.

### M1.7 — `MarkerStore` (in `core/markers`)

- [ ] `syncview:markers` → **OK** (stesso test di M1.6, esteso).
- [ ] Copre: store vuoto, `add`/`get`/`find_by_id`, `update` parziale per bitmask (`description` NULL → `""`), `update`/`remove` su id inesistente, ordinamento per timestamp con inserimenti sparsi, stabilità a parità di timestamp, riposizionamento dopo update del timestamp, `remove` dal mezzo.

### M1.8 — Query binary search del `MarkerStore`

- [ ] `syncview:markers` → **OK**.
- [ ] Copre: store vuoto, `get_next`/`get_previous` strettamente >/<, `get_at` con tolleranza inclusiva e pareggio di distanza (vince il successivo), `get_range` inclusivo/vuoto/invertito, e confronto contro scansioni lineari (copie di `MarkerManager`) su 4×200 marker casuali (seed fisso, con molti timestamp duplicati) × 2000 query ciascuno.

### M1.9 — `core/marker_db` (apertura + schema)

- [ ] `syncview:marker_db` → **OK**.
- [ ] Copre (su file temporaneo, ispezione diretta via sqlite3): creazione delle directory mancanti, tabelle `metadata`/`markers` con colonne/tipi/default/PK attesi (`PRAGMA table_info`), i 4 indici espliciti + vincolo `UNIQUE(timestamp, video_index, created_at)`, `db_version=1` e `created_at` ISO8601, riapertura idempotente (dati e `created_at` invariati), migrazione da `db_version=0`, errore su path non apribile.

### M1.10 — `marker_db_save_batch` / `marker_db_load_all`

- [ ] `syncview:marker_db` → **OK** (stesso test di M1.9, esteso).
- [ ] Copre: round-trip di 50 marker (timestamp duplicati, `video_index` -1/0..3, descrizioni con apici/virgolette/accenti, default di category/description) con confronto campo per campo, `video_index` globale salvato come `NULL`, upsert (stesso numero di righe, campi aggiornati, `created_at` invariato, `updated_at` ISO8601), batch su store vuoto, `load_all` che esclude `is_deleted=1` (e lo include con `include_deleted`), rollback dell'intero batch su violazione `UNIQUE`.

### M1.11 — `marker_db_delete` (soft delete)

- [ ] `syncview:marker_db` → **OK**.
- [ ] Copre: riga ancora presente con `is_deleted=1` e dati intatti, `updated_at` aggiornato, altri marker non toccati, assenza da `load_all` (presenza con `include_deleted`), idempotenza, id inesistente = successo senza modifiche (come l'originale).

### M1.12 — Migrazione JSON legacy → SQLite

- [ ] `syncview:marker_db` → **OK**.
- [ ] Copre (fixture JSON scritte in test): migrazione di 4 marker (chiave `label` scartata, `id`/`created_at` preservati o generati se mancanti/null, `video_index` null/assente = globale, timestamp float, unicode), JSON rinominato in `.json.backup` con contenuto identico e backup precedente sovrascritto, contenuto migrato verificato via `load_all`; casi no-op (file assente, `markers` vuoto o assente → nessun backup); 9 input non validi (JSON malformato, radice non oggetto, `markers` non lista, chiave sconosciuta nel 2° marker, timestamp/color mancanti, tipi errati, elemento non oggetto) → errore, JSON intatto, nessun backup, nessun marker salvato; `marker_db_open_migrating` (migra solo se il DB non esisteva, non blocca l'apertura se il JSON è invalido).

### M1.13 — `core/user_paths`

- [ ] `syncview:user_paths` → **OK**.
- [ ] Copre: valori vuoti su file assente e creazione della directory padre, indici non validi (get → `NULL`, set/clear → errore senza scrivere), salvataggio immediato ad ogni set e round-trip su file temporaneo (4 slot + `last_export_dir`, unicode, clear, export dir `NULL`), lettura di un file nel formato Python (stringhe vuote/null → slot vuoto, lista corta/lunga), file corrotto o di struttura inattesa → valori vuoti, `get_valid_video_paths` (rimuove file inesistenti e directory da memoria e file, nessuna riscrittura se nulla cambia), errore di salvataggio (valore resta in memoria), path di default.

### M1.14 — `core/logger` + modalità debug

- [ ] `syncview:logger` → **OK**.
- [ ] Copre: **zero byte su stderr a debug disattivato** dopo 20 giri di tutte le funzioni di log (stderr catturato via `dup2`), file con intestazione di avvio e tutte le categorie dell'originale nel formato atteso (`[AZIONE UTENTE]`, `[VIDEO n]`, seek `mm:ss (Nms)`, export ✓/✗, `[EXPORT]`, errore + `GError` su seconda riga), dettagli DEBUG (`log_sync/marker/gst/ui`) assenti senza debug; con debug (flag e `SYNCVIEW_DEBUG=1`) stessi messaggi anche su stderr con timestamp/livello/categoria e righe identiche al file; `SYNCVIEW_DEBUG` = `0`/`false`/vuoto non attiva il debug; nessun output prima di `logger_init` e dopo `logger_shutdown`; file troncato ad ogni apertura; propagazione di `GST_DEBUG=3` solo in debug e senza sovrascrivere un valore esistente; file non scrivibile → errore ma logger ancora attivo.
- [ ] `syncview:no_adhoc_logging` → **OK**: controllo statico (`tests/check_no_adhoc_logging.py`) che fuori da `core/logger.c` non ci siano `printf`/`fprintf`/`stderr`/`g_print*` né `SYNCVIEW_DEBUG`/`debug_enabled`. Se un modulo aggiunge logging ad-hoc, questo test fallisce indicando file e riga.

### M1.14 (integrazione) — log dei moduli M1 e `main.c`

- [ ] `syncview:module_logging` → **OK**: in modalità normale i log di azione utente/errore dei moduli finiscono sul file (database creato, batch save, migrazione JSON→SQLite, errore batch su violazione UNIQUE, `user_paths.json` salvato, percorso impostato/rimosso) e `stderr` resta vuoto; in debug, su file e stderr, compaiono anche gli eventi del piano (`[SYNC]` calculate/sync_all/setter, `[MARKER]` SQL con righe coinvolte, caricamento, store add/update/remove).
- [ ] Manuale (richiede display): `./build/src/syncview --debug` stampa su stderr `Applicazione SyncView avviata` e `[UI] Finestra principale creata`; senza `--debug` stderr vuoto e il file `~/.syncview/syncview_log.txt` contiene solo le righe INFO.

### M1.15 — Filtro per modulo e sink del logger

- [ ] `syncview:logger_filter` → **OK**.
- [ ] Copre: nomi degli 8 moduli e default tutti abilitati; filtro applicato a **file, stderr e sink** (modulo disabilitato → sparisce ovunque, riabilitato → torna); mappatura delle categorie storiche sui moduli e livello corretto per ogni funzione `log_*`; `ERROR` mai filtrato (nemmeno con tutti i moduli disabilitati, nemmeno `log_export` fallito con `EXPORT` disabilitato); `logger_init()` riporta il filtro al default; filtro attivo anche in modalità normale (sul file) senza alcun sink invocato e con stderr vuoto; più sink, rimozione e id sconosciuti; sink rientrante (il log emesso dal sink non rientra nei sink ma finisce sul file, registrare/rimuovere sink dal sink non va in deadlock); 4 thread × 200 messaggi senza perdite con toggle concorrente del filtro.
- Nota: ThreadSanitizer non è affidabile su questi test perché `libglib` di sistema non è instrumentata (segnala come race anche accessi protetti da `GMutex`); la concorrenza è verificata dal test multi-thread + ASan.

### Riepilogo atteso

```
meson test -C build
```
deve riportare **11/11 OK** allo stato attuale (`dummy`, `time_format`, `settings`, `sync_manager`, `markers`, `marker_db`, `user_paths`, `logger`, `no_adhoc_logging`, `module_logging`, `logger_filter`).

---

## Cosa NON è ancora testabile

Tutto ciò che riguarda persistenza (SQLite, JSON), playback video reale, export, UI oltre la finestra vuota di M0.2, e la modalità debug appartiene a M1.7 e successive — non ancora implementato. Questo file verrà esteso con una nuova sezione ad ogni milestone completata.
