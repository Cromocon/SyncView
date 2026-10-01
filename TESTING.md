# TESTING — SyncView-C

Questo documento spiega come avviare il progetto e cosa testare manualmente allo stato attuale. Viene aggiornato ad ogni milestone con i nuovi elementi testabili — copre **M0 (Scaffolding)** completo, **M1 (Core logic)** completo (M1.1–M1.16) e **M2 (Finestra minima con 1 video)** fino a M2.3 incluso.

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
meson test -C build                 # tutti i test
meson test -C build -v discoverer   # un singolo test, con output
```

- I test sono in `tests/` e linkano `libsyncview_core` (nessuna dipendenza da GTK, nessun display richiesto).
- Un test può terminare con exit code **77** = *skip* (es. `discoverer` se mancano i plugin GStreamer per generare i file di prova): Meson lo riporta come `SKIP`, non come errore.
- Passata con sanitizer (consigliata prima di chiudere una milestone): vedi **M1.16** più sotto.
- In CI `meson test` gira con `--print-errorlogs`: se un test fallisce, nel log del job compare il suo output (asserzioni comprese). Senza, su Windows un `assert` fallito appare solo come `exit status 3221226505 or 0xc0000409`. In locale l'equivalente è `meson test -C build -v <nome>` oppure `./build/tests/<eseguibile>`.

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

- [ ] `meson test -C build` include `syncview:dummy` → **OK** (all'epoca era l'unico test; il totale attuale è nel **Riepilogo atteso** in fondo).
- [ ] Il log completo è consultabile in `build/meson-logs/testlog.txt`.

### M0.5 — Documentazione

- [ ] [README.md](README.md) è presente e descrive correttamente lo stack attuale (pipeline GStreamer manuale, `GstDiscoverer`, plugin base+good+bad+libav).
- [ ] [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) è presente (inizialmente uno stub, ora con stato implementativo, moduli e decisioni) e [docs/MIGRATION_NOTES.md](docs/MIGRATION_NOTES.md) documenta il comportamento dell'originale Python e le deviazioni.
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

## Checklist di test manuale — M1 (Core logic, completa)

Nessuna dipendenza da GTK: tutto è verificabile via `meson test -C build` (test automatici). Le uniche verifiche manuali di M1 riguardano l'avvio dell'app con la modalità debug (vedi M1.14, integrazione). Per vedere singolarmente l'output di ogni test:

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
- ⚠️ Da riconciliare prima di M2.7/M5: queste 3 opzioni riproducono `config/settings.py`, ma la UI dell'originale (`ui/main_window.py`) offre 4 voci (`40ms (25fps)`, `33ms (30fps)`, `100ms`, `200ms`) e il piano parla di 40/33/100/200. Il test attuale verifica le 3 di `settings.py`.

### M1.3 + M1.4 + M1.5 — `core/sync_manager`

- [ ] `syncview:sync_manager` → **OK**.
- [ ] Copre: stato iniziale (sync abilitato, offset a 0, master=0), getter/setter con bound-check, `calculate_sync_position` (offset uguali/diversi/clamp a 0/source==target), `sync_all_to_master` con player mock (seek prima di pause, master solo in pausa, slot non caricato o `NULL` ignorato).

### M1.6 — `core/markers`

- [ ] `syncview:markers` → **OK**.
- [ ] Copre: default `description=""`/`category="default"`, generazione `id`/`created_at`, unicità dell'`id` anche per marker con lo stesso timestamp, **500 marker creati in rapida successione con `id` e `created_at` tutti distinti e `created_at` crescente** (regressione per l'orologio a bassa risoluzione di Windows, ~15 ms: prima `id`/`created_at` potevano coincidere), `marker_free(NULL)` sicuro.

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
- [ ] Copre: **zero byte su stderr a debug disattivato** dopo 20 giri di tutte le funzioni di log (stderr catturato via `dup2`), file con intestazione di avvio e tutte le categorie dell'originale nel formato atteso (`[AZIONE UTENTE]`, `[VIDEO n]`, seek `mm:ss (Nms)`, export ✓/✗, `[EXPORT]`, errore + `GError` su seconda riga), dettagli DEBUG (`log_sync/marker/gst/ui`) assenti senza debug; con debug (flag e `SYNCVIEW_DEBUG=1`) stessi messaggi anche su stderr con timestamp/livello/categoria e righe identiche al file; `SYNCVIEW_DEBUG` = `0`/`false`/vuoto non attiva il debug; nessun output prima di `logger_init` e dopo `logger_shutdown`; file troncato ad ogni apertura e con fine riga `\n` su tutte le piattaforme (aperto in modalità binaria; su Windows `"w"` avrebbe scritto `\r\n`); propagazione di `GST_DEBUG=3` solo in debug e senza sovrascrivere un valore esistente; file non scrivibile → errore ma logger ancora attivo.
- [ ] `syncview:no_adhoc_logging` → **OK**: controllo statico (`tests/check_no_adhoc_logging.py`) che fuori da `core/logger.c` non ci siano `printf`/`fprintf`/`stderr`/`g_print*` né `SYNCVIEW_DEBUG`/`debug_enabled`. Se un modulo aggiunge logging ad-hoc, questo test fallisce indicando file e riga.

### M1.14 (integrazione) — log dei moduli M1 e `main.c`

- [ ] `syncview:module_logging` → **OK**: in modalità normale i log di azione utente/errore dei moduli finiscono sul file (database creato, batch save, migrazione JSON→SQLite, errore batch su violazione UNIQUE, `user_paths.json` salvato, percorso impostato/rimosso) e `stderr` resta vuoto; in debug, su file e stderr, compaiono anche gli eventi del piano (`[SYNC]` calculate/sync_all/setter, `[MARKER]` SQL con righe coinvolte, caricamento, store add/update/remove).
- [ ] Manuale (richiede display): `./build/src/syncview --debug` stampa su stderr `Applicazione SyncView avviata` e `[UI] Finestra principale creata`; senza `--debug` stderr vuoto e il file `~/.syncview/syncview_log.txt` contiene solo le righe INFO.

### M1.15 — Filtro per modulo e sink del logger

- [ ] `syncview:logger_filter` → **OK**.
- [ ] Copre: nomi degli 8 moduli e default tutti abilitati; filtro applicato a **file, stderr e sink** (modulo disabilitato → sparisce ovunque, riabilitato → torna); mappatura delle categorie storiche sui moduli e livello corretto per ogni funzione `log_*`; `ERROR` mai filtrato (nemmeno con tutti i moduli disabilitati, nemmeno `log_export` fallito con `EXPORT` disabilitato); `logger_init()` riporta il filtro al default; filtro attivo anche in modalità normale (sul file) senza alcun sink invocato e con stderr vuoto; più sink, rimozione e id sconosciuti; sink rientrante (il log emesso dal sink non rientra nei sink ma finisce sul file, registrare/rimuovere sink dal sink non va in deadlock); 4 thread × 200 messaggi senza perdite con toggle concorrente del filtro.
- Nota: ThreadSanitizer non è affidabile su questi test perché `libglib` di sistema non è instrumentata (segnala come race anche accessi protetti da `GMutex`); la concorrenza è verificata dal test multi-thread + ASan.

### M1.16 — Passata sanitizer completa

Da una build pulita, su tutti i test M1.1–M1.15:

```
meson setup build-asan -Db_sanitize=address -Db_lundef=false --buildtype=debug
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:abort_on_error=1 meson test -C build-asan
```

- [ ] 17/17 **OK**, zero errori ASan e **zero leak** nel codice SyncView (LeakSanitizer; per `discoverer` con le soppressioni di terze parti in `tests/lsan.supp`).
- [ ] Stessa passata con `-Db_sanitize=address,undefined` → 17/17 OK, zero `runtime error` UBSan.
- Prima di fidarsi di "zero leak" verificare che LeakSanitizer sia attivo nell'ambiente (alcuni sandbox/container lo disabilitano in silenzio): un programma di prova che perde 123 byte deve produrre `SUMMARY: AddressSanitizer: 123 byte(s) leaked`.
- ThreadSanitizer **non** è un criterio affidabile con `libglib` di sistema (non instrumentata: falsi positivi sui `GMutex`).

---

## Checklist di test manuale — M2 (Finestra minima con 1 video, in corso)

### M2.1 — Analisi di `core/video_loader.py`

- [ ] [docs/MIGRATION_NOTES.md](docs/MIGRATION_NOTES.md), sezione "Caricamento video e probing metadati", descrive: campi estratti e loro consumatori, fallback/gestione errori, sequenza di caricamento, uso di `fps` nel resto dell'app, deviazioni previste e messaggi di log da mantenere. Nessun codice né test: verifica documentale.

### M2.2 — `core/discoverer`

- [ ] `syncview:discoverer` → **OK** (termina con 77 = *skip* se mancano i plugin `videotestsrc`/`vp8enc`/`webmmux`; in CI sono nei pacchetti good).
- [ ] Copre (file generati a runtime con pipeline GStreamer): dimensioni, fps (25 e 30000/1001), durata, codec `vp8`; solo audio → valori di default; file assente/directory/`NULL` → `FILE_NOT_FOUND` ("File non trovato"), anche con fallback; file corrotto e vuoto → errore bloccante `CORRUPT`; file troncato; classificazione errori bloccanti/non bloccanti; formattazione dei plugin mancanti (installer details → "H.265 decoder, AAC decoder"); timeout fuori range limitati senza `CRITICAL` (fatali nel test); fallback su timeout; API asincrona (successo, errore propagato dal thread, annullamento → `G_IO_ERROR_CANCELLED`, due probing concorrenti indipendenti); stima fps dai timestamp (funzione pura: standard, 30 vs 29.97, non standard, outlier, intervalli ≤0, pochi dati; e da file reali generati a 25 e 29.97); percorsi "scomodi" (spazi, accenti/€, `#`; su POSIX anche backslash e virgolette) sia per `discoverer_probe_file` sia per `discoverer_estimate_fps`.
- [ ] Sanity check manuale contro `ffprobe`/`gst-discoverer-1.0` su file veri: vedi tabella in `docs/MIGRATION_NOTES.md` ("Esito di M2.2").
- **Lezione Windows (CI)**: i file di prova non vanno mai generati scrivendo il percorso dentro la stringa di `gst_parse_launch` — il backslash è un carattere di escape e `D:\a\_temp\...` diventa `D:a_temp...` (il `filesink` non riesce ad aprire il file). Il test usa `filesink name=out` e imposta `location` come proprietà. Riproducibile anche su Linux con `TMPDIR='/tmp/x\a\_temp' ./build/tests/test_discoverer` (la versione precedente del test falliva su `msg != NULL && ... EOS`).
- Sanitizer: `tests/lsan.supp` sopprime le perdite di **driver GPU di terze parti** (`libcuda`, `nvidia_drv_video`, `libEGL_nvidia`, moduli scaricati) caricati dai plugin GStreamer; nessun frame di codice SyncView è coinvolto. Vale solo per il test `discoverer` con `-Db_sanitize=address`.

### M2.3 — `video/video_player` (scheletro di `SyncviewVideoPlayer`)

- [ ] `syncview:video_player` → **OK**. Richiede un **display GTK** e il plugin `gtk4paintablesink` (`gst-plugin-gtk4` / gst-plugins-rs): senza uno dei due esce con 77 = *skip*. Riproducibile: `env GDK_BACKEND=x11 DISPLAY=:99 ./build/tests/test_video_player` → exit 77.
- [ ] Copre: tipo GObject `SyncviewVideoPlayer` registrato e finale; proprietà `video-index` e `paintable` coerenti con i getter; la pipeline è un `playbin3` con `gtk4paintablesink` come `video-sink`, il cui `paintable` è proprio quello esposto, **ferma in `GST_STATE_NULL`**; il paintable si assegna a un `GtkPicture` e, senza video, non ha dimensioni intrinseche (0×0: nessun frame) e il widget si misura senza crash, anche dopo aver distrutto il player; indici non validi (−1, 4, 100…) → errore `INDEX`, tutti gli indici 0..3 validi; 4 player con pipeline, paintable e nomi di elemento distinti; 25 cicli crea/distruggi; **pipeline, sink e paintable vengono finalizzati** alla distruzione del player (riferimenti deboli); elemento mancante → errore `MISSING_ELEMENT` che nomina `gtk4paintablesink` (simulato rimuovendo la factory dal registry, poi ripristinata). I `CRITICAL` di GLib/GTK sono fatali.
- [ ] Manuale (facoltativo): nessuna finestra nuova in M2.3 — il player non è ancora collegato alla UI (M2.8). Il primo frame visibile arriva con M2.4.
- **Perché il controllo con riferimenti deboli**: una prova di mutazione (rimuovere il rilascio di `video_sink`/`paintable` in `dispose`) **non** veniva rilevata da LeakSanitizer, perché gli oggetti restano raggiungibili dal main context di GLib; i riferimenti deboli la rilevano.
- **Limite della CI**: il runner Linux non ha display e nessuna CI installa ancora `gtk4paintablesink`, quindi in CI questo test è *skip*: la CI verifica solo che il codice compili e linki sulle tre piattaforme. Il test vero va eseguito in locale (vedi "Cosa NON è ancora testabile").
- `tests/lsan.supp` ora sopprime anche le perdite dei driver OpenGL/EGL (Mesa) inizializzati da GTK all'apertura del display (nessun frame di codice SyncView).

### M2.4 — `load()`, bus GStreamer e primo frame

Stesso eseguibile di M2.3: `syncview:video_player` → **OK** (skip senza display/`gtk4paintablesink`; i test di `load()` richiedono inoltre `videotestsrc`/`vp8enc`/`webmmux` per generare i file di prova, altrimenti sono saltati).

- [ ] **Primo frame**: prima del load il paintable non contiene nessuna texture (al più un riempimento nero) e nessun decoder è riportato; `load()` ritorna subito (`is_loading`, senza segnali); all'`ASYNC_DONE` arriva **un solo** `load-state-changed(TRUE)`; la pipeline è in **`PAUSED`** (non in play); il paintable assume le dimensioni del video (320×240) e uno snapshot contiene una **texture** (il frame vero); un `GtkPicture` collegato riceve la larghezza naturale del video; nessun evento spurio nei 200 ms successivi.
- [ ] **Un `ASYNC_DONE` successivo non è un nuovo caricamento**: un seek con flush dopo il load non riemette `load-state-changed`.
- [ ] **Decoder in uso (O6)**: `syncview_video_player_get_decoder_description()` riporta es. `vp8dec (software)`; nel log debug (`[GST]`) compaiono il decoder e, se software, l'avviso con gli eventuali decoder hardware disponibili per quel formato.
- [ ] **Errori**: file assente / `NULL` / directory → `FILE_NOT_FOUND` ("File non trovato") immediato, senza segnali e con stato invariato; file illeggibile (byte casuali) → l'errore arriva dal bus come segnale `error` con messaggio non vuoto, pipeline di nuovo in `NULL`, `loaded`/`loading` a FALSE; un errore iniettato sul bus **dopo** un load riuscito emette `error` e `load-state-changed(FALSE)` una sola volta e riporta la pipeline a `NULL`; in entrambi i casi il player si riprende con un nuovo load.
- [ ] **Sostituzione del video**: secondo `load()` nello stesso player → nuovo `ASYNC_DONE`, nuove dimensioni (640×360), percorso aggiornato; `ASYNC_DONE` del caricamento precedente già sul bus ma non consegnato → **scartato** (un solo esito, per il file nuovo); due `load()` ravvicinati → un solo esito, per l'ultimo file.
- [ ] **Più player**: due player caricano file diversi contemporaneamente, ciascuno con i propri segnali e dimensioni.
- [ ] **Distruzione durante il caricamento**: player distrutto con un load in corso (anche dopo qualche ms), main context lasciato girare: nessun callback su oggetti distrutti, nessun `CRITICAL` (fatali nel test); pipeline, sink, paintable **e bus** vengono finalizzati (il bus resta vivo finché il suo watch è attivo: lo verifica un riferimento debole).
- **Prova di mutazione** (non parte di `meson test`): per validare i test, sono state introdotte di proposito 11 regressioni nel codice del player (watch del bus non rimosso, `ASYNC_DONE` senza guardia, pipeline non riportata a `NULL` su errore, `loaded` non azzerato su errore, path non azzerato, `loading` non impostato, stato `READY` invece di `PAUSED`, nessun controllo del file, segnali non emessi, decoder sempre `NULL`): **tutte e 11 sono state rilevate** (il test di un errore *dopo* un load riuscito e il riferimento debole sul bus sono stati aggiunti proprio perché due mutazioni inizialmente sfuggivano). Una mutazione sul solo svuotamento esplicito del bus non è rilevabile perché è ridondante: `GstPipeline` ha `auto-flush-bus` attivo.
- **Verifica su file reale** (manuale, fatta in sviluppo): un MP4 AV1 1280×720 si carica in ~240 ms, `ASYNC_DONE` arriva dopo `NULL→READY→PAUSED`, il log `[GST]` riporta `decoder video in uso: nvav1dec (hardware)` e il paintable emette `invalidate-size` con 1280×720. La UI per vederlo arriva in M2.8; fino ad allora il frame è verificato dallo snapshot del paintable nel test.

### M2.10 — `core/deps_check` (verifica dipendenze) e `--check-deps`

- [ ] `syncview:deps_check` → **OK** (nessun requisito di display o plugin: usa sistemi finti; le prove con sonde reali usano script finti su POSIX).
- [ ] **Sistema completo** su Linux/Windows/macOS: 9 componenti nell'ordine documentato, tutti OK, `can_play`/`can_export`/`is_complete` veri.
- [ ] **Componenti obbligatori** (`playbin3`, `gtk4paintablesink`): se mancano → `MISSING` (funzione *riproduzione*), `can_play` falso ma `can_export` vero; il dettaglio nomina l'elemento.
- [ ] **Decoder H.264 (any-of)**: basta uno tra software e hardware (solo `vah264dec`/`nvh264dec` vale); nessuno → playback non possibile. HEVC/VP9/AV1 sono **opzionali** (mancanza = avviso, playback e export restano possibili, `is_complete` falso).
- [ ] **Demuxer**: un demuxer per formato supportato; wmv e flv accettano anche quelli di libav; se manca `qtdemux` il dettaglio dice «mp4, mov» e i formati ancora presenti.
- [ ] **Decoder hardware per piattaforma**: ogni piattaforma riconosce i propri (`vtdec_hw` su macOS, `d3d11h264dec` su Windows, `va*`/`nv*` su Linux); uno di un'altra piattaforma non conta.
- [ ] **ffmpeg**: versione (`7.1.1`, `n7.0-12-g…`) ed encoder H.264 (`libx264` o hardware) letti dall'output; nessun encoder H.264 (o tipo diverso da `V`), non eseguibile → export non possibile; assente → `INSTRUCTIONS` su Linux e `DOWNLOADABLE` su Windows/macOS; `deps_dir` cercata per prima (e default `~/.syncview/deps`).
- [ ] **Piano di installazione** (cosa potrà installare l'app in M2.11): Arch/Debian-Ubuntu/Fedora con il plugin gtk4 mancante → `SYSTEM_PACKAGES` con gestore (`pacman`/`apt-get`/`dnf`) e pacchetto esatti (`gst-plugin-gtk4` / `gstreamer1.0-gtk4` / `gstreamer1-plugin-gtk4`) e, in più, il comando a mano come ripiego; componente presente → nessun piano; più set per un componente (demuxer: good + libav) senza duplicati; ffmpeg assente → installabile su Arch/Debian, **solo istruzioni su Fedora e openSUSE** (RPM Fusion/Packman) e sullo stesso per `gst-libav` su Fedora; openSUSE per gtk4 e gestore non riconosciuto → istruzioni generiche, nessun pacchetto; Windows/macOS → nessun pacchetto di sistema né comandi Linux (ffmpeg `DOWNLOADABLE`).
- [ ] **Raccolta dei pacchetti**: `deps_report_collect_packages` — sistema completo → `NULL`; solo richiesti → `gst-plugin-gtk4, gst-libav, ffmpeg` nell'ordine dei componenti senza duplicati; con gli opzionali si aggiunge `gst-plugins-bad` (decoder hardware) e `gst-libav` per HEVC non si duplica; Windows/macOS → `NULL`.
- [ ] **Report**: testo con `[OK]`/`[MANCANTE]`/`[OPZIONALE MANCANTE]`, per ciò che l'app può installare la riga «SyncView può installarlo (pacman: …); il sistema chiederà la password di amministratore» seguita dal ripiego «a mano: …», e la riga finale «Riproduzione: … | Export: … | Completo: …»; `deps_report_log` scrive il riepilogo (azione utente) e un dettaglio `[GST]` per componente.
- [ ] **Sonde reali** (POSIX): `run_program` legge lo stdout, gestisce esito ≠ 0 e programma inesistente, **termina dopo ~5 s** un processo che non finisce; `find_program` dà precedenza a `<dir>/bin` e ignora file non eseguibili; un falso `ffmpeg` eseguibile in `deps_dir` è trovato e letto da `deps_check_run` con le sonde reali.
- [ ] **Manuale** — `./build/src/syncview --check-deps` stampa il report sul sistema reale e termina senza aprire finestre (exit 0 = riproduzione ed export possibili, 1 = manca qualcosa di richiesto); con `--debug` compaiono anche i dettagli `[GST]`. Per vedere il caso negativo: `env PATH=<dir con solo pacman> GST_PLUGIN_SYSTEM_PATH_1_0=/nonexistent GST_REGISTRY_1_0=/tmp/r.bin ./build/src/syncview --check-deps`.
- **Prova di mutazione** (13 regressioni introdotte di proposito nella logica, es. H.264 reso opzionale, `can_play` sempre vero, `extra_dir` ignorata, risoluzione Windows/macOS uguale a Linux, encoder audio contati come video, versione non parsata): i test le rilevano **tutte**; una (tipo `V` degli encoder) sfuggiva e ha richiesto un caso in più. Una seconda tornata sul piano di installazione (12 regressioni: Fedora/openSUSE considerati installabili, `apt` al posto di `apt-get`, pacchetti duplicati o opzionali inclusi per errore, pacchetti su Windows/macOS, ripiego a mano assente, ffmpeg non scaricabile su Windows/macOS…) è rilevata per intero; due mutazioni erano *equivalenti* (codice ridondante, poi rimosso).
- Controllo statico `no_adhoc_logging`: ora `main.c` (e solo `main.c`) può scrivere su **stdout** per l'output voluto dei comandi CLI; restano vietati stderr e i flag di debug ovunque.

### M2.5 — play / pausa / stop

Stesso eseguibile di M2.3/M2.4: `syncview:video_player` → **OK** (skip senza display/`gtk4paintablesink`; richiede anche i plugin di prova vp8enc/webmmux). Dura circa 20 s perché riproduce davvero video di 1–4 s in tempo reale.

- [ ] **Play/pausa in tempo reale**: dopo il load il player è in `PAUSED` sul primo frame, **fermo** (posizione invariata); `play()` → `PLAYING`, pipeline in `PLAYING`, dopo ~600 ms la posizione è avanzata di ~600 ms e il paintable ha ricevuto ≥5 frame; `pause()` → la posizione e i frame si fermano; `play()` **riprende da dove si era fermato**, non da capo.
- [ ] **Segnale `playback-state-changed`**: sequenza `PAUSED` (load), `PLAYING`, `PAUSED`, `PLAYING`; chiamate ripetute (`play()` in play, `pause()` due volte) **non** emettono segnali doppi.
- [ ] **`toggle_play_pause`**: PAUSED→PLAYING→PAUSED→PLAYING; da `STOPPED` avvia.
- [ ] **`stop()`**: `STOPPED`, posizione a ~0, **video ancora caricato**, pipeline in `PAUSED` (non `NULL`), decoder ancora montato, primo frame visibile nel paintable (snapshot con texture); da fermo non avanza; `play()` dopo `stop()` riparte da 0; due `stop()` di fila senza segnali doppi.
- [ ] **Fine del video** (clip di 1 s): da solo `STOPPED` con un solo segnale, video caricato e fermo in fondo (posizione ≥ 800 ms, invariata); `play()` riparte dall'inizio e arriva di nuovo in fondo; `stop()` a fine video torna a 0 senza segnali in più.
- [ ] **Player non caricato**: `play`/`pause`/`stop`/`toggle` → `FALSE` con errore `NOT_LOADED` (e con `error == NULL`), nessun segnale, stato `STOPPED`, pipeline in `NULL`; lo stesso **durante il caricamento** («in caricamento» nel messaggio) e dopo un errore di caricamento; a caricamento finito funzionano.
- [ ] **Nuovo `load()` in riproduzione**: il vecchio video viene scartato (`STOPPED`), il nuovo arriva in `PAUSED` e **non parte da solo**; `play()` poi funziona.
- [ ] **Errore in riproduzione** (iniettato sul bus): `STOPPED` (emesso prima di `error`), video non più caricato, pipeline a `NULL`, `play()` non ha effetto.
- [ ] **Due player** riproducono indipendentemente; mettere in pausa uno non tocca l'altro. Player distrutto in `PLAYING` (8 volte, a tempi diversi) senza crash né `CRITICAL`.
- [ ] **Log**: `[VIDEO 3] Stato riproduzione: PLAY/PAUSA/STOP` (slot 1-based), una riga per ogni chiamata anche quando lo stato non cambia.
- **Nota di design verificata dai test**: `stop()` **non** scarica il video (deviazione dal piano, che diceva `GST_STATE_NULL`): vedi `docs/MIGRATION_NOTES.md`.
- **Prova di mutazione**: 19 regressioni introdotte di proposito (play/pause/stop che non cambiano la pipeline o lo stato, `stop` che scarica il video, EOS ignorato, riavvio da 0 a fine video, segnali doppi, play ammesso durante il caricamento, log mancanti, toggle invertito, seek senza flush…): **tutte rilevate**. Due sfuggivano inizialmente e hanno richiesto casi in più: la pipeline deve scendere in `PAUSED` all'EOS e `pause()` a fine video deve lasciare lo stato `STOPPED`.

### M2.6 — posizione, durata e polling col frame clock

Stesso eseguibile: `syncview:video_player` → **OK** (ora ~30 s; alcuni test aprono una piccola finestra GTK per avere un frame clock reale, quindi servono un display e la possibilità di mapparla).

- [ ] **Durata al caricamento**: `duration-changed` arriva **prima** di `load-state-changed(TRUE)` (ordine «DL»); `get_duration()` coincide con quella misurata indipendentemente da `core/discoverer` (±60 ms); la posizione iniziale è 0 e non genera segnali; prima del load e durante il caricamento `get_duration()`/`get_position()` valgono 0; da fermo dopo il load nessun aggiornamento periodico.
- [ ] **Posizione in riproduzione**, sia col **timer di ripiego** (nessun widget) sia col **frame clock** (finestra mappata): almeno 8 e al massimo 60 aggiornamenti in 800 ms; valori in **millisecondi**, compresi tra 0 e la durata, **strettamente crescenti** (mai duplicati), distanziati di almeno ~15 ms (throttle a ~50 Hz); l'ultimo coincide con `get_position()` entro 120 ms; `is_ticking()` è vero solo in `PLAYING`.
- [ ] **Da fermo nessun wakeup (O5)**: dopo `pause()` il polling è spento; un ultimo aggiornamento con la posizione finale, al massimo 3 aggiornamenti di assestamento (l'`ASYNC_DONE` ripubblica la posizione definitiva), poi **silenzio assoluto** per 500 ms; un secondo `pause()` non emette nulla.
- [ ] **`stop()` e fine video**: `stop()` pubblica la posizione 0 a seek concluso, una volta, poi silenzio; a fine video la posizione finale ≈ durata (≥ durata − 120 ms), polling spento; `play()` a fine video riparte da ~0 e riprende a emettere; la durata non cambia mai durante play/pause/stop.
- [ ] **Nuovo `load()` ed errore**: il video scartato azzera subito posizione e durata (ordine «PD», sincrono col `load()`), poi durata del nuovo video e «caricato» (ordine «PDDL»); un errore con video caricato a posizione > 0 azzera posizione e durata prima del segnale `error`; `is_ticking()` falso in tutti questi casi.
- [ ] **Il polling segue il frame clock di GTK, non un timer**: staccando il widget dalla finestra (niente frame clock) gli aggiornamenti **si fermano** (≤ 2 in 500 ms) pur restando il callback registrato e lo stato `PLAYING`; riagganciandolo **riprendono**. Un timer non si comporterebbe così.
- [ ] **Widget del tick**: impostarlo o toglierlo **durante la riproduzione** non interrompe né duplica il polling; se il widget viene **distrutto in riproduzione** il polling prosegue col timer di ripiego (≥ 5 aggiornamenti in 400 ms) e si ferma normalmente alla pausa; player distrutto in `PLAYING` con ticker attivo (con e senza finestra) senza callback residui né `CRITICAL`.
- [ ] **Nessun cambio di stato ridondante**: `pause()` su una pipeline già in `PAUSED` (e `play()` in `PLAYING`) non invia di nuovo `set_state` — lo rendeva visibile un test, perché un secondo `pause()` spostava la posizione di 1 ms.
- **Verifica su file reale** (manuale, in sviluppo): un MP4 1280×720 in riproduzione emette la posizione ogni ~33 ms (067, 100, 134, 167 ms…) con `ticking=1`, un ultimo valore alla pausa (401) e poi nessun aggiornamento per oltre un secondo con `ticking=0`.
- [ ] **Distruzione durante il caricamento (teardown differito)**: `dispose` con la pipeline ancora in salita verso `PAUSED` non si blocca mai (40/40 esecuzioni del test mirato, 8 suite complete consecutive; prima ~1 volta su 12). Il `set_state(NULL)` resta sul main thread perché `gtk4paintablesink` (Rust, `ThreadGuard`) va in panic se toccato da un altro thread; se la pipeline ha un cambio di stato in corso si attende, col main loop attivo, che si assesti (tetto 10 s, poi forzato). `syncview_video_player_pending_teardowns()` deve tornare a 0 a fine test (asserito in coda al test).
- **Mutation testing M2.6** (script ad hoc, non in repo): ogni mutazione del player (bus watch non rimosso, ASYNC_DONE senza guardia, errore senza reset, throttle assente, polling anche in pausa, ecc.) deve far fallire almeno un test. Unica equivalente: «`pause()` non pubblica la posizione finale», coperta comunque dall'`ASYNC_DONE`. Il throttle si vede solo col test a finestra.
- **Sanitizer**: la suite completa passa con `-Db_sanitize=address` (con `tests/lsan.supp`: driver GPU/EGL/Mesa e proxy Wayland di GTK, nessuno stack nostro) e con `-Db_sanitize=undefined`.
- **Diagnostica**: `SYNCVIEW_TEST_TRACE=1` stampa ogni test all'avvio; `SYNCVIEW_TEST_ONLY=<sottostringa>` esegue solo i test il cui nome la contiene (per escludere le finestre: evitare `frame_clock`, `ticker`, `tick_widget`, `ticking`).
- **Frame clock irregolare (CI)**: sul runner macOS una finestra senza display attivo riceve pochissimi tick (3 aggiornamenti in 800 ms), quindi i test del frame clock misurano prima i tick di una finestra di prova e, se sono meno di 10 in 500 ms, saltano le sole verifiche che lo richiedono (messaggio «frame clock irregolare»). Quelle col timer di ripiego restano sempre attive. In locale, con display, girano tutte.
- **Nota sul test del frame clock**: una finestra *nascosta* (`set_visible(FALSE)`) NON ferma i tick in GTK4 — il frame clock continua finché il widget è in una finestra; per questo il test stacca il widget dalla finestra.

### M2.7 — seek, step in ms, frame-step esatto, velocità

Stesso eseguibile: `syncview:video_player` → **OK** (ora ~70 s; i test di M2.7 non aprono finestre). Il frame mostrato si legge con `syncview_video_player_get_frame_end_ns()` (fine del frame).

- [ ] **Senza video**: `seek`/`step_ms`/`step_frames`/`set_playback_rate` → `NOT_LOADED`, nessun segnale; `get_frame_rate()` = 0 e `get_frame_end_ns()` = −1.
- [ ] **`seek`**: il frame mostrato è quello che contiene l'istante richiesto (2000 → frame 2000–2040; 1290 → frame 1280–1320), la posizione è riportata a seek concluso, lo stato non cambia; fuori intervallo limitato a 0..durata (il log riporta la destinazione **effettiva**); in `PLAYING` il video continua dal nuovo punto; dopo la fine del video un `seek` seguito da `play()` riprende da lì, non da 0; un nuovo `load()` azzera subito frame e framerate.
- [ ] **`step_ms`**: +200 → 200±5 ms, −200, limiti 0 e durata, **mette in pausa** se in `PLAYING` (polling spento); subito dopo un `seek` ancora in corso e in 5 passi ravvicinati parte dalla destinazione (440, poi 640 ms), non dalla posizione vecchia.
- [ ] **Frame-step esatto (O1), 25 e 30 fps**: riferimento = i frame ottenuti avanzando di uno alla volta; poi indietro uno alla volta, salti di N (+5, −3, +10, −12), 6 passi indietro e 4 avanti **ravvicinati**, seek **esattamente sull'inizio** di un frame seguito subito da uno step indietro (a 30 fps i pts in ms distano 33/34 ms: senza il margine sull'ancora cade un frame troppo indietro), 100 passi avanti che si fermano sull'ultimo frame, indietro di uno da lì, step dopo un seek arbitrario, step durante la riproduzione (pausa + un solo frame). Ogni volta il frame è **esattamente** quello atteso.
- [ ] **Framerate sconosciuto** (file con framerate variabile, `capssetter` a 0/1): `get_frame_rate()` = 0 e `step_frames(n)` ricade su n × 40 ms.
- [ ] **Velocità**: 1.0 iniziale; ≤ 0, NaN e ∞ → `INVALID_ARGUMENT` senza cambiare nulla; da fermo vale al `play()`; misurata sulla posizione: 2.0 → 1.5–2.6x, 0.5 → 0.3–0.75x, 1.0 → 0.75–1.3x (tolleranze larghe per i runner lenti); cambiandola in riproduzione il video non salta (al più un frame indietro) e non si ferma; **sopravvive** a `seek()` e `stop()`; un nuovo `load()` la riporta a 1.0.
- [ ] **Log**: `Timeline seek`, `Step (ms)`, `Step Frame`, `Velocità` nel modulo VIDEO; numeri con il punto decimale anche con la locale italiana.
- **Mutation testing M2.7** (script ad hoc): 20 mutazioni (clamp, `at_end`, pausa dello step, seek pendente, ancora dalla fine del frame, step avanti sempre con evento, velocità ignorata/non azzerata/non validata, ripiego senza fps, margine di 1 ms, reset di `frame_info`, clamp ultimo/primo frame, log…): tutte rilevate tranne due equivalenti — «velocità uguale alla corrente non evita il seek» (solo lavoro risparmiato) e «`play()` non azzera il seek pendente» (l'`ASYNC_DONE` lo azzera comunque). Le lacune emerse (clamp superiore, margine, reset, ultimo frame) sono diventate test.
- **Scoperte verificate**: (1) dopo un seek accurato il pts del buffer arrivato al sink è il punto di arrivo, non l'inizio del frame; (2) più `GST_EVENT_STEP` ravvicinati si sostituiscono (3 passi su 4 persi); (3) `INSTANT_RATE_CHANGE` con `matroskademux` dà un CRITICAL di GStreamer; (4) `step_ms` subito dopo un `seek` leggeva la posizione vecchia.

### Design system — token (`design/`)

`syncview:design_tokens` (`design/tokens_tool.py check`) → **OK**.

- [ ] **Contrasti AA in entrambi i temi** (chiaro e scuro): testo ≥ 4,5:1 su sfondo, superficie e superficie 2; secondario ≥ 4,5:1; testo sull'accento (Play), sul successo, sull'errore, sull'avviso; testo tenue su tinta morbida; etichette dei 4 canali; elementi grafici ≥ 3:1 (anello di focus, bordi, playhead, marker).
- [ ] **File generati aggiornati**: `design/generated/syncview-{light,dark}.css` coincidono con quanto produce `tokens_tool.py build` da `design/tokens.json`. Se cambi un token, lancia `python3 design/tokens_tool.py build` e committa anche i CSS.
- [ ] **Mutazione**: portando `mute` del tema chiaro a `#9AA6B8` il controllo fallisce (1,99:1); ripristinato torna OK.
- Fonte unica: `design/tokens.json` (solo valori di base; tinte morbide, testo sui colori e testo tenue si derivano nello script con le stesse regole del prototipo di Claude Design).

### M2.8 — finestra minima

`syncview:main_window` → **OK** (~7 s; richiede display e `gtk4paintablesink`, altrimenti `SKIP`). Le finestre di test si aprono brevemente: sul desktop dell'utente vanno eseguite sul «Desktop 9» (vedi la nota in fondo).

- [ ] **Tema**: i due fogli di stile generati (chiaro e scuro) sono accettati da GTK senza **nessun errore di analisi**, e il cambio di tema applica quello giusto (`syncview_theme_is_dark`). Nessun avviso `Gtk-WARNING` (larghezze negative, ecc.) all'apertura della finestra.
- [ ] **Finestra vuota**: stato `EMPTY`, scheda «Nessun video», Play/passi/barra **disattivati**, il pulsante «Carica video» attivo.
- [ ] **Apri → carica**: `LOADING` con «Analisi del file…», poi `LOADED`; comandi attivi, tempo `00:00.000`, durata mostrata. Il percorso è nel file dei percorsi **solo ora**.
- [ ] **Play, pausa, seek, passo**: il pulsante passa da «Play» a «Pausa» e torna; il seek sposta barra e tempo (`00:02.000`); +1 frame → `00:02.040`, −1 frame → `00:02.000` (**lettura sull'inizio del frame**, uguale avanti e indietro). Il tempo che avanza durante il play si verifica solo se il frame clock batte (su finestra non visibile il test lo segnala e salta quel solo controllo).
- [ ] **O3**: dopo un caricamento riuscito, un file non riproducibile (testo con estensione `.mp4`) → `ERROR` «Impossibile riprodurre il video» e un file inesistente → `ERROR` «File spostato o non trovato»; **in entrambi i casi il file dei percorsi è identico byte per byte**. Un video valido successivo lo aggiorna.
- [ ] **Ricarico all'avvio**: una nuova finestra ricarica da sola il video salvato (`LOADING` → `LOADED`); se il file non c'è più la finestra è vuota e il percorso è tolto dal file.
- [ ] **Chiusura**: finestra chiusa a metà caricamento e in riproduzione (6 volte) → nessuno smontaggio rimasto in sospeso; ASan/UBSan 16/16 (nuove soppressioni di terze parti: cache dei font fontconfig/pango).
- [ ] **Uscita ordinata dell'app** (manuale, o `kill -TERM` / `kill -INT` all'app in esecuzione): termina in meno di un secondo, il log riporta «Finestra principale chiusa» e «Applicazione chiusa», nessun processo `syncview` residuo.
- [ ] **Prova visiva** (manuale): con `SYNCVIEW_THEME=light` e `=dark` la finestra mostra barra del titolo, riquadro video con chip «● A · FEED-1», fps e tempo, tempo grande, barra, pulsanti e barra delle scorciatoie come nei mockup della direzione 1b.
- **Prove su macOS e Windows reali**: elencate in [docs/TEST_MACOS.md](docs/TEST_MACOS.md) e [docs/TEST_WINDOWS.md](docs/TEST_WINDOWS.md) (file vivi, da consegnare a fine M2). Copertura della CI: Linux salta tutti i test con finestra (nessun display); macOS esegue `video_player` e `debug_windows` e salta `main_window`; Windows esegue `debug_windows` ma salta `video_player` e `main_window` perché nella CI manca `gst-plugins-rs` (`gtk4paintablesink`).
- **macOS in CI**: il test `main_window` è saltato (`SKIP`) perché sul runner il caricamento col video nella finestra non termina: problema aperto, da provare su un Mac reale (vedi PLAN.md, M2.8).
- **Desktop 9** (solo sviluppo su KDE/Wayland): uno script KWin temporaneo sposta sul «Desktop 9» le finestre dei test e di `syncview`; su un desktop non attivo il compositor non invia frame callback, quindi le verifiche sul frame clock si saltano da sole.

### M2.9 — finestre di debug

`syncview:debug_windows` → **OK** (~12 s; richiede display, non GStreamer; `SKIP` senza display). Si esegue sul «Desktop 9» (vedi nota in fondo).

- [ ] **Solo in debug**: senza `--debug` `syncview_debug_windows_open()` apre 0 finestre e `logger_is_debug_mode()` è falso (anche prima di `logger_init` e dopo `logger_shutdown`); con `--debug` o `SYNCVIEW_DEBUG=1` ne apre 2 (3 con la principale: verificato sull'app vera, 3 finestre contro 1).
- [ ] **Righe e livelli**: azione utente → `INFO`/`USER`, `log_sync`/`log_ui` → `DEBUG`/`SYNC`/`UI`, `log_error` → `ERROR`/`APP`; l'etichetta `[UI] ` non si ripete nel testo; riga di stato «N righe · 8 moduli attivi», «In diretta», scorrimento automatico ON.
- [ ] **Interruttori dei moduli**: spento SYNC le sue righe non compaiono più nella finestra Log (il filtro è del logger, quindi vale anche per file e stderr: coperto da `logger_filter`); gli **errori compaiono sempre**, anche con il modulo spento; «Tutti» riaccende; la riga di stato conta i moduli attivi.
- [ ] **Filtri di vista**: livello minimo Info/Errore e chip del modulo nascondono righe nella vista senza toglierle dal modello (la riga di stato dice «N di M righe»).
- [ ] **Pausa/Riprendi/Svuota**: in pausa le righe restano in coda («5 in attesa», scorrimento OFF) e alla ripresa compaiono nell'ordine di arrivo; Svuota azzera modello e coda.
- [ ] **Tetto**: 6000 righe emesse → ne restano 5000, le più recenti, in ordine.
- [ ] **Thread**: 4 thread × 800 righe (più messaggi GST) mentre il main loop gira → nessuna riga persa né scartata, e per ogni thread l'ordine di emissione è rispettato (40 esecuzioni consecutive senza errori; prima della correzione del risveglio perso falliva ~1 volta su 5).
- [ ] **Chiusura**: chiudere la principale chiude Log e Moduli (anche se una è già chiusa a mano); log emessi da altri thread mentre la finestra viene chiusa e dopo la chiusura non toccano memoria liberata (ASan 17/17).
- [ ] **Prova visiva** (manuale): con `SYNCVIEW_THEME=light|dark` e `--debug` le finestre sono leggibili (testo scuro su chiaro e viceversa, livelli con simbolo, interruttori, chip) come nella direzione 1b; nessun `Gtk-WARNING` all'apertura.

### Riepilogo atteso

```
meson test -C build
```
deve riportare **17/17** allo stato attuale (`dummy`, `time_format`, `settings`, `sync_manager`, `markers`, `marker_db`, `user_paths`, `logger`, `no_adhoc_logging`, `module_logging`, `logger_filter`, `discoverer`, `video_player`, `deps_check`, `design_tokens`, `main_window`, `debug_windows`); in un ambiente senza display o senza `gtk4paintablesink` `video_player` e `main_window` risultano `SKIP` (e `debug_windows` senza display) (e `discoverer` se mancano i plugin di prova): è normale.

---

## Cosa NON è ancora testabile

- **Muto e volume**: da M2.8 in poi. Il player carica, riproduce, mette in pausa e ferma, riporta posizione e durata, e si sposta con seek, step e velocità (M2.4–M2.7).
- **Interfaccia oltre la finestra vuota di M0.2**: griglia 2x2, timeline, marker a schermo, dialoghi, scorciatoie, tema, zoom/pan, titlebar custom (M3–M7). Le finestre di debug "Log" e "Moduli" sono previste in M2.9 (oggi esistono solo il filtro e il sink del logger, testati da `logger_filter`).
- **Download/installazione delle dipendenze e dialog del primo avvio**: M2.11–M2.12. La sola *verifica* (M2.10) c'è: `syncview --check-deps`.
- **Export**: M6.
- **Sync e marker con video reali**: la logica è testata (`sync_manager`, `MarkerStore`, `marker_db`) ma non è ancora collegata a nessun player o widget (M3/M4).
- **`video_player` in CI**: il test viene saltato (nessun display sul runner Linux, nessun `gtk4paintablesink` installato in nessuna delle tre CI). Per verificarlo anche in CI servirebbero i pacchetti del plugin (gst-plugins-rs) su ogni piattaforma e, su Linux, un display virtuale (`xvfb`): da decidere.
- **Esecuzione su macOS e Windows in locale**: solo via CI (`gh run list --branch SyncView-C`). Il primo push di M2.2 è passato su Linux e macOS ma è fallito su Windows per un difetto del *test* (percorsi con backslash in `gst_parse_launch`, vedi M2.2), non del modulo; il fix è committato e va confermato da un nuovo run.

Questo file viene esteso con una nuova sezione ad ogni milestone completata.
