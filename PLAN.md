# Piano: Riscrittura di SyncView in C (branch `SyncView-C`)

## Contesto

SyncView è un'app desktop Python/PyQt6 per l'analisi video tattica multi-sync (fino a 4 video sincronizzati in griglia 2x2, timeline con markers, export clip via ffmpeg). L'obiettivo è ricreare l'applicazione da zero in **C puro (C11)**, esclusivamente nel branch `SyncView-C`, che conterrà **solo** la struttura C (il branch `main` resta Python/Qt invariato come riferimento e per l'uso corrente). La UI avrà un **overhaul completo** — non si clona il look Qt, ma si preserva tutta la logica di dominio e le funzionalità (sync, markers, export, shortcut, zoom/pan, ecc.).

Il repo attuale, dentro `SyncView-C`, andrà quindi ripulito dai file Python e ripopolato con la nuova struttura C (l'analisi del codice Python è stata fatta leggendo i file su `main` prima dello switch, così da avere un riferimento comportamentale preciso da riportare pedissequamente dove richiesto).

## Analisi del codice originale (sintesi)

**Sync/playback**: decodifica via Qt QtMultimedia (QMediaPlayer, backend GStreamer su Linux) — nessun OpenCV/PyAV. Probing metadati (fps/durata/dimensioni/codec) via subprocess `ffprobe` esterno, mai per il decode. Sync (`core/sync_manager.py`) è **offset-based statico, senza correzione di drift continua**: `sync_position = source_position - offset[source] + offset[target]` (clamp ≥0), applicata solo su eventi discreti (seek/resync/marker-click), mai durante playback continuo. FPS mismatch gestito con `playback_rate = detected_fps/target_fps`. Frame stepping è **ms-based** (40ms fisso per-player, 40/33/100/200ms configurabile a livello globale) — non esiste vero seek frame-accurate. `core/frame_cache.py` è vestigiale (nessun caching reale di pixel, solo telemetria) — **da non portare**. `core/spatial_index.py` è un indice ordinato di marker (bisect) per query O(log n) sulla timeline — in C si sostituisce con binary search su array ordinato, fuso direttamente nel marker store.

**Markers/persistenza**: `Marker` = id, timestamp_ms, color, description, category, video_index (None/-1 = marker globale su tutti i video), created_at/updated_at. **SQLite è lo storage primario/autoritativo**; JSON legacy è solo migrazione one-shot (rinominato in `.json.backup` dopo la migrazione). Salvataggi event-driven (non più autosave a 30s). Schema SQL esatto:
```sql
CREATE TABLE IF NOT EXISTS metadata (key TEXT PRIMARY KEY, value TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS markers (
    id TEXT PRIMARY KEY, timestamp INTEGER NOT NULL, color TEXT NOT NULL,
    description TEXT DEFAULT '', category TEXT DEFAULT 'default',
    video_index INTEGER, created_at TEXT NOT NULL, updated_at TEXT NOT NULL,
    is_deleted INTEGER DEFAULT 0,
    UNIQUE(timestamp, video_index, created_at)
);
CREATE INDEX idx_timestamp ON markers(timestamp);
CREATE INDEX idx_category ON markers(category);
CREATE INDEX idx_video_index ON markers(video_index);
CREATE INDEX idx_deleted ON markers(is_deleted);
```
Delete = soft delete (`is_deleted=1`), upsert via `INSERT ... ON CONFLICT(id) DO UPDATE`, connessione aperta/chiusa per ogni operazione (nessuna connessione persistente/WAL nell'originale).

**Export**: **non usa moviepy** (nonostante sia nei requirements) — usa `ffmpeg` via subprocess: `ffmpeg -y -ss <start_sec> -i <video> -t <duration_sec> -c:v <encoder> ... -c:a aac -b:a 192k <output>`. Encoder auto-rilevato (NVENC/QSV/VAAPI/VideoToolbox, fallback libx264+CRF). Un clip per combinazione marker×video applicabile. Finestra: `start_ms = max(0, marker.timestamp - sec_before*1000)`, `end_ms = marker.timestamp + sec_after*1000`. Parallelizzato con `ProcessPoolExecutor` (in Python, per bypassare il GIL); timeout 5 min/clip, 3 retry default. `ExportQueue` persistente in `~/.syncview/export_queue.json`, salvataggio sincrono ad ogni mutazione. `config/user_paths.py`: 4 slot path video + last_export_dir in `~/.syncview/user_paths.json`, salvataggio sincrono immediato.

**UI (da ridisegnare, funzionalità da preservare)**: finestra senza decorazioni OS, titlebar draggable custom, resize via drag **mai implementato correttamente nell'originale** (gap noto). Layout: titlebar + sidebar + griglia video 2x2 + timeline globale. Shortcut: Space, Ctrl+O, F1, Ctrl+S, Ctrl+F, Ctrl+R, Home/End, M, ←/→ (frame-mode), Shift+←/→, Ctrl+M, P/N, Ctrl+E, Ctrl+0. Drag&drop video sui placeholder. Timeline custom (ruler, tick adattivi, marker-triangoli con hit-testing, playhead con badge, **niente drag-to-move marker**, rimosso in origine). Zoom/pan video 1.0–5.0x con Ctrl+wheel, formula "zoom verso il cursore": `pan' = mouse - (mouse - pan) * (new_zoom/old_zoom)`. Dialoghi: gestione marker (filtro per categoria, editing inline descrizione, export CSV, **niente creazione da dialog** — solo Ctrl+M), FPS personalizzato, export (cartella + qualità + slider sec before/after).

## Riferimento visivo per l'overhaul UI

L'utente ha indicato come riferimento di design il sito https://praesidium.artysan.me/ (progetto "PRÆSIDIUM — Field Operations", tema tattico/outdoor). Analizzato (home, pagina "Style tiles", pagina "UI states", pagina "Field" con mappa/zoom): fornisce un linguaggio visivo coerente da adottare per il tema GTK4 CSS al posto della vecchia palette Qt "Night Ops":

- **Palette "Command / Default"** (la direzione attiva di default sul sito): sfondo quasi nero `#11130f`, colore secondario/testo attenuato oliva `#a9b271`, accento primario terracotta `#c47c4c`. Esistono due varianti alternative sul sito (Terrain: `#182019`/`#d8d1bd`/`#91523d`; Range: `#242522`/`#c9bda4`/`#d39b3d`) — da tenere come possibili temi alternativi in CSS (es. selezionabili), ma "Command" come default.
- **Tipografia**: heading grandi in serif editoriale (elegante, non tattico-militare stretto) per titoli di sezione; nav/label/microcopy in maiuscolo con letter-spacing ampio e monospace/condensed per elementi tipo "FIELD CORE // INIT", "01 // AIRSOFT FIELD", numerazioni "01/02/03" — pattern molto adatto a un'app di analisi tattica.
- **Componenti riusabili identificati, mappabili 1:1 su controlli SyncView**:
  - *Zoom stepper* (`−` / `1.0×` / `+` in un gruppo pillole con bordo sottile) osservato nella pagina "Field" — pattern diretto da riusare per i controlli zoom del video (sostituisce lo slider/percentuale dell'originale Qt).
  - *Pannello laterale informativo* (titolo, descrizione, righe di statistiche con separatore hairline, CTA in basso) — pattern da riusare per la sidebar dei controlli player (offset, FPS, stato sync).
  - *Card numerate* con bordo superiore accent sottile (`01`, `02`, `03`) e bordo 1px hairline sul resto — pattern da riusare per liste tipo marker dialog o pannelli di stato.
  - *Stati vuoti/loading/errore* documentati esplicitamente nella pagina "UI states" (skeleton con barre sfumate/blur, empty state con CTA, error state con "Try again", stato "sold out"/non disponibile) — da replicare come stati coerenti per: caricamento video, nessun marker presente, errore caricamento video, errore export.
  - *Bottone azione primaria*: rettangolo pieno colore accento, testo maiuscolo, icona freccia a destra.
- **Layout generale**: header scuro fisso con logo+claim a sinistra, nav orizzontale maiuscola al centro/destra, bordo inferiore sottile accent-colored sotto la voce attiva — pattern riusabile per la titlebar/toolbar custom di SyncView (adattato: niente vero "nav di sito", ma stessa grammatica visiva per i bottoni di modalità sync/frame-mode).

Questo riferimento sostituisce integralmente la vecchia palette `THEME_COLORS`/QSS Qt come base del nuovo `ui/style.css` — va applicato a partire da M0/M7 (CSS base già in M0 come scheletro, rifinito in M7).

## Decisioni confermate con l'utente

- **Struttura repo**: il branch `SyncView-C` conterrà **esclusivamente** la struttura C (niente sottodirectory `c/`, niente file Python residui) — il branch `main` resta il riferimento Python/Qt invariato.
- **GTK ≥ 4.10** come dipendenza minima (necessaria per `GtkFileDialog` moderno e soprattutto `gdk_toplevel_begin_resize()`, che finalmente risolve in modo pulito il resize frameless mai implementato correttamente nell'originale Qt).
- **Packaging**: solo build da sorgente con Meson per ora; niente Flatpak/AppImage in questa fase.

## Strategia multi-piattaforma

L'app finale deve avere **build native per Linux, Windows e macOS, con comportamento identico su tutte e tre**. Priorità:

- **Importanza del target di produzione**: macOS > Linux > Windows.
- **Capacità di test in fase di sviluppo**: Linux > Windows > macOS (lo sviluppo avviene principalmente su Linux; macOS, il target più importante, è quello meno testabile localmente).

Questa asimmetria (il target più importante è quello meno verificabile in corso d'opera) è il rischio trasversale più rilevante del progetto e guida diverse scelte:

- **Scelta del toolkit confermata**: GTK4 + GStreamer restano la scelta giusta perché entrambi hanno backend nativi ufficiali su tutte e tre le piattaforme (Linux: X11/Wayland; Windows: Win32; macOS: Quartz) e binari GStreamer precompilati forniti direttamente da gstreamer.freedesktop.org per Windows/macOS — non serve cambiare architettura, ma va trattata la portabilità come vincolo esplicito fin da subito, non come rifinitura finale in M8.
- **API cross-platform, non Linux-specific**: `gdk_toplevel_begin_resize()`/`begin_move()` sono API GDK4 astratte dal backend (non X11-specifiche), quindi in teoria funzionano su Win32/Quartz allo stesso modo — ma vanno **verificate esplicitamente su Windows e macOS appena possibile** (CI, vedi sotto), non assunte funzionanti solo perché compilano su Linux.
- **Scorciatoie da tastiera**: l'originale usa sempre `Ctrl+`. Su macOS la convenzione è `Cmd+`. **Correzione dopo verifica**: in GTK4 `"<Primary>"` NON si rimappa più automaticamente a Cmd su macOS (era così in GTK3, è stato rimosso) — serve un trigger esplicito doppio (`"<Control>o|<Meta>o"`) o una macro `#ifdef __APPLE__`, vedi dettaglio nella sezione "Scorciatoie da tastiera cross-platform" più sotto. Va applicato esplicitamente per ogni shortcut in M5.2.
- **CI multi-piattaforma da subito**: dato che lo sviluppatore non può testare macOS localmente in modo continuo, va introdotta una pipeline CI (GitHub Actions, matrice `ubuntu-latest` / `windows-latest` / `macos-latest`) che compili il progetto su tutte e tre le piattaforme ad ogni push, fin dalla milestone M0 (non solo a fine progetto in M8). Questo è l'unico modo per intercettare rotture su macOS/Windows in tempo utile, dato lo squilibrio testabilità/importanza. Aggiunta come nuova sotto-milestone **M0.6**.
- **Gestione dipendenze per piattaforma**:
  - Linux: pacchetti di sistema (apt/dnf/pacman) — già coperto.
  - macOS: Homebrew (`brew install gtk4 gstreamer gst-plugins-base gst-plugins-good sqlite ffmpeg meson ninja`), build con i Command Line Tools di Xcode (clang).
  - Windows: MSYS2/MinGW-w64 (`pacman -S mingw-w64-x86_64-gtk4 mingw-w64-x86_64-gstreamer ...`) come percorso più semplice per compatibilità con Meson/GLib; vcpkg come alternativa se emergono problemi con MSYS2.
- **Packaging equivalente per piattaforma** (da bilanciare con "solo Meson per ora", vedi Decisioni confermate): il pacchetto finale sarà comunque diverso per forma (AppImage/pacchetto distro su Linux, `.app` bundle con dylib bundling su macOS, eseguibile+DLL o installer su Windows), ma deve installare le stesse dipendenze runtime e produrre lo stesso comportamento applicativo — questo è oggetto della milestone M8, ora esplicitamente suddivisa per piattaforma (vedi sotto).
- **Parità dei plugin GStreamer — CORRETTO dopo verifica**: **base+good NON basta**. I decoder/demuxer per H.264/HEVC (i codec più comuni nei file video reali) sono in `gst-plugins-bad`/`gst-libav`, e anche i decoder hardware per piattaforma (`applemedia`/VideoToolbox su macOS, `d3d11`/Media Foundation su Windows, `va`/VAAPI su Linux) risiedono tutti in `gst-plugins-bad`. Il set minimo richiesto per riprodurre normalmente i file `.mp4`/`.mkv` H.264 è quindi **base + good + bad + libav**, su tutte e tre le piattaforme (gli installer ufficiali Windows/macOS di gstreamer.freedesktop.org li includono già tutti). Nota per M8: verificare eventuali implicazioni di licenza nella distribuzione di `gst-libav` (basato su FFmpeg, codec con brevetti in alcune giurisdizioni) se in futuro si passa a una distribuzione pubblica del pacchetto.

## Architettura

### Toolkit e librerie

**Aggiornato dopo verifica cross-platform (vedi risposta al "Prompt per un'altra IA" in fondo al documento) — cambia in modo sostanziale l'approccio al playback rispetto alla prima stesura.**

- **GTK4 (>= 4.10)** in C puro, con CSS nativo per il tema (overhaul UI, nessun vincolo di parità visiva con Qt).
- **Playback video: pipeline GStreamer manuale (`playbin3` + `gtk4paintablesink`), NON `GtkVideo`/`GtkMediaFile`.** Motivo: `GtkMediaStream` non espone alcun controllo di `playback-rate` (nessuna API esiste: verificato, `gtk_media_stream_set_playback_rate()` non esiste in GTK4), che è invece necessario per il meccanismo di compensazione FPS mismatch dell'originale (`playback_rate = detected_fps/target_fps`). Serve quindi gestire direttamente una pipeline `playbin3` per ogni video, con `gst_element_send_event(pipeline, gst_event_new_seek(rate, ...))` per il controllo rate, e renderizzare il frame corrente su un `GtkPicture` tramite il sink `gtk4paintablesink` (richiede `gstreamer-plugins-gtk4`, disponibile da GStreamer 1.20+). `GtkMediaStream` (property `timestamp`/`duration` in **microsecondi**, non millisecondi — conversione esplicita richiesta ai bordi dell'API verso il resto del codice che lavora in ms come l'originale) resta eventualmente utile solo come riferimento di interfaccia per i nomi dei segnali, non come implementazione.
- **Probing metadati: `GstDiscoverer` (`gst-pbutils-1.0`) al posto di `ffprobe` via subprocess.** Cambio deciso rispetto alla prima stesura del piano (che manteneva ffprobe per fedeltà all'originale): `GstDiscoverer` è in-process, thread-safe, identico su Linux/Windows/macOS, ed evita i problemi di path-lookup dell'eseguibile esterno e di parsing fragile dello stdout che si presentano su Windows/macOS. `ffmpeg` (CLI) resta comunque una dipendenza obbligatoria, ma solo per l'export (non per il probing).
- **sqlite3** collegata direttamente per i markers, schema copiato 1:1.
- **GLib/GIO/json-glib** per subprocess async, thread pool, file I/O di configurazione (`user_paths.json`, `export_queue.json`), niente parser JSON scritto a mano.
- **Meson** come build system.

### Struttura directory (root del branch `SyncView-C`)
```
meson.build
meson_options.txt
README.md
src/
  main.c
  app.c / app.h                    # bootstrap GtkApplication
  core/
    sync_manager.c / .h            # porting 1:1 di core/sync_manager.py
    markers.c / .h                 # struct Marker + MarkerStore (ordinato, binary search interna)
    marker_db.c / .h               # sqlite3, schema, upsert/soft-delete, migrazione JSON legacy
    settings.c / .h                # costanti/tunable (step ms, zoom range, ecc.)
    user_paths.c / .h              # ~/.syncview/user_paths.json (istanza iniettata, no singleton globale)
    logger.c / .h                  # log categorizzato, file troncato ad ogni avvio (come originale)
    discoverer.c / .h              # probing metadati via GstDiscoverer (in-process, cross-platform)
    export_queue.c / .h            # coda persistente crash-resume
    export_worker.c / .h           # subprocess ffmpeg, encoder detection, retry/timeout
  video/
    video_player.c / .h            # GObject SyncviewVideoPlayer: pipeline playbin3 + gtk4paintablesink
    zoom_pan.c / .h                 # matematica zoom/pan (no widget)
  ui/
    main_window.c / .h
    video_grid.c / .h               # 2x2, drag&drop, placeholder
    timeline_widget.c / .h          # GtkDrawingArea custom, ruler/marker/playhead
    sidebar.c / .h
    titlebar.c / .h                 # decorazioni custom + resize via gdk_toplevel_begin_resize
    dialog_markers.c / .h
    dialog_fps.c / .h
    dialog_export.c / .h
    style.css
    keymap.c / .h
  util/
    time_format.c / .h              # HH:MM:SS.mmm
tests/
  meson.build
  test_sync_manager.c
  test_markers.c
  test_marker_db.c
  test_export_window.c              # contratto da test_marker_validation.py
docs/
  ARCHITECTURE.md
  MIGRATION_NOTES.md                 # deviazioni note vs Python (vedi Rischi)
```
Libreria interna `libsyncview_core.a` (static) per `core/` + `util/`, linkata sia dall'eseguibile che dai test — così i moduli core restano disaccoppiati dalla UI anche a livello di build, e i test girano senza display (no Xvfb necessario per M1).

### Moduli core da portare 1:1 (basso rischio, alta priorità)
`sync_manager` (API: `sync_manager_calculate_sync_position`, `sync_manager_sync_all_to_master` con callback opachi per non dipendere dal vero player), `markers`+binary search interna (fonde `spatial_index.py` nel marker store, niente modulo separato), `marker_db` (schema identico, connessione apri/chiudi per operazione + `sqlite3_busy_timeout()` come unica micro-deviazione giustificata), `user_paths`, `logger`, `settings`.

**Non portare**: `frame_cache.py` (vestigiale, la pipeline GStreamer gestisce già il buffering interno) e `debounce.py` come modulo standalone (pattern banale con `g_timeout_add` implementato localmente dove serve, es. nel timeline widget).

### Video playback (GObject `SyncviewVideoPlayer`)
**Rivisto dopo verifica cross-platform** (vedi Toolkit sopra): non un wrapper su `GtkVideo`, ma un GObject che possiede direttamente una pipeline `playbin3` con `gtk4paintablesink` collegato a un `GtkPicture` per il rendering. Segnali GLib equivalenti a quelli Qt originali: `position-changed`, `duration-changed`, `user-seeked`, `video-focused`, `add-marker-requested`, `load-state-changed` — ottenuti ascoltando i bus message della pipeline (`GST_MESSAGE_STATE_CHANGED`, `GST_MESSAGE_DURATION_CHANGED`, ecc.) invece delle `notify::` property di `GtkMediaStream`. API: load/play/pause/seek/get_position/get_duration/set_playback_rate (via `gst_event_new_seek` con rate, l'unico modo per farlo in GStreamer)/step_ms/set_muted/unload (idempotente, per contratto equivalente a `test_memory_leak_fix.py` — timer fermati, pipeline riportata a `GST_STATE_NULL` e reinizializzata senza distruggere il widget/GObject). **Tutte le posizioni interne restano in millisecondi** (coerenti con lo schema SQLite e col resto del codice C, come nell'originale) — la conversione da/verso i nanosecondi nativi di GStreamer (`GST_TIME_AS_MSECONDS`/`GST_MSECOND * ms`) avviene solo ai bordi del modulo `video_player.c`. Un solo timer di polling condiviso a livello finestra (unificazione naturale rispetto ai timer sparsi dell'originale) per aggiornare la timeline globale.

### Timeline widget
`GtkDrawingArea` + Cairo (non GskRenderNode — sufficiente e più semplice per rendering immediate-mode 2D). Matematica dei tick adattivi come funzione pura testabile separatamente dal draw. Marker rendering con hit-testing basato sul marker store (binary search). **Conferma: nessun drag-to-move marker.**

### Export pipeline
`export_queue.c` (porting diretto di `ExportQueue`, salvataggio sincrono ad ogni mutazione) + `export_worker.c` (encoder detection via `ffmpeg -encoders`, comando ffmpeg identico all'originale, validazione tempo disponibile con confronto stretto `<` come da `test_marker_validation.py`). Parallelizzazione con **`GThreadPool`** (non serve emulare `ProcessPoolExecutor`: in C non c'è GIL da bypassare, e il lavoro pesante è comunque nel processo `ffmpeg` esterno) con `max_workers = num_processori - 1`. Comunicazione worker→UI obbligatoriamente via `g_idle_add`/`GTask` (regola rigida GTK4: mai toccare widget fuori dal main thread).

### Dialoghi e stato UI
Overhaul basato sul linguaggio visivo di praesidium.artysan.me (vedi sezione dedicata sopra): palette "Command" (`#11130f`/`#a9b271`/`#c47c4c`), microcopy maiuscolo monospace, hairline border 1px, card numerate. Marker dialog con `GtkColumnView`/`GtkFilterListModel` per filtro categoria + editing inline + export CSV (no creazione da dialog, solo Ctrl+M), righe marker in stile card numerata. FPS dialog con `GtkSpinButton` + preset, stile pannello laterale informativo. Export dialog con `GtkFileDialog` (API GTK4 moderna) + `GtkDropDown` qualità + `GtkScale` per sec before/after — **i `GtkFileFilter` vanno configurati con `gtk_file_filter_add_suffix()` (es. `"mp4"`, `"mkv"`) e non con MIME type**, altrimenti su Windows GTK può ripiegare sul dialog non-nativo invece di mostrare quello di sistema (verificato). Controlli zoom video nello stile "zoom stepper" (`−` / valore / `+`) osservato sul sito di riferimento, al posto di slider. Stati vuoti/loading/errore (video non caricato, nessun marker, errore probing/export) disegnati secondo i pattern della pagina "UI states" del riferimento (skeleton shimmer, empty state con CTA, error state con retry). Stato UI centralizzato (`ui_state.c`) con visibilità pannelli guidata da `sync_enabled`/`frame_mode_enabled`, sfruttando `g_object_bind_property` dove possibile.

### Modalità debug (fin dall'inizio, non rimandata a fine progetto)

Richiesta esplicita: avere da subito un modo per vedere **tutto quello che succede internamente**, per poter diagnosticare i problemi che emergeranno durante lo sviluppo (specialmente su Windows/macOS, dove il debug interattivo è più scomodo per chi sviluppa principalmente su Linux — vedi Strategia multi-piattaforma).

- **Attivazione**: variabile d'ambiente `SYNCVIEW_DEBUG=1` (letta in `main()` prima di ogni altra inizializzazione) e/o flag CLI `--debug`/`-v`. Quando attiva, imposta un livello di verbosità globale in `core/logger.c` usato da tutti i moduli.
- **Output**: a differenza del log applicativo originale (file troncato ad ogni avvio, silenzioso), la modalità debug stampa **anche su stderr/terminale** in tempo reale, con timestamp e categoria (`[SYNC] [MARKER] [VIDEO] [EXPORT] [UI] [GST]`), oltre a scrivere comunque sul file di log. Non sostituisce le categorie esistenti (`log_user_action`/`log_video_action`/ecc., vedi M1.14) ma le rende anche verbose-e-a-schermo quando la modalità è attiva, aggiungendo dettagli che normalmente non verrebbero loggati (es. ogni singolo bus message GStreamer, ogni valore di posizione durante il polling, ogni query SQL eseguita).
- **Eventi da tracciare esplicitamente in debug mode** (da rispettare in ogni milestone successiva, non solo M1): transizioni di stato della pipeline GStreamer (`GST_STATE_CHANGED`, `ASYNC_DONE`, errori/warning del bus) per player; ogni chiamata a `sync_manager_calculate_sync_position`/`sync_all_to_master` con i valori di input/output; ogni operazione SQLite su `marker_db` (query eseguita, righe affette); ogni job di export (creazione, stato, retry, comando ffmpeg esatto eseguito); ogni shortcut attivato con l'azione risolta; cambi di stato UI centralizzati (`ui_state.c`).
- **Integrazione con GStreamer**: la pipeline supporta nativamente la variabile `GST_DEBUG` (es. `GST_DEBUG=3` o `GST_DEBUG=playbin3:5`) per il logging interno di GStreamer stesso — in modalità debug SyncView può impostarla programmaticamente (se non già impostata dall'utente) così da avere anche i log interni della pipeline senza bisogno di conoscerne la sintassi.
- **Nessun impatto in produzione**: a modalità debug disattivata (default), il comportamento resta identico a quanto già pianificato (log solo su file, nessun overhead di stampa a terminale) — la verbosità extra è un costo aggiuntivo solo quando esplicitamente richiesta.
- **Requisito architetturale esplicito — punto di controllo unico, non sparso per file**: il flag debug va letto/valutato **una sola volta**, dentro `core/logger.c` (uno stato globale interno al modulo, es. `static bool debug_enabled`, impostato una volta in un `logger_init()` chiamato da `main()`). Ogni altro modulo (`sync_manager`, `markers`, `video_player`, `export_worker`, UI, ecc.) chiama sempre e comunque le funzioni di log categorizzato (`log_sync(...)`, `log_gst(...)`, ecc.) **senza mai avvolgerle in un proprio `if (debug_enabled)`** — è la funzione di logging stessa, internamente, a decidere se scrivere anche su stderr oppure no. Questo per garantire che disattivare la modalità debug sia **un solo interruttore in un solo file** (`core/logger.c`, o la variabile d'ambiente a monte), senza dover ricontrollare/ripulire condizionali sparsi in ogni file che logga. Corollario pratico: durante il code review di ogni milestone, un `grep -rn "SYNCVIEW_DEBUG\|debug_enabled"` fuori da `core/logger.c` deve dare risultato vuoto — se un modulo introduce un proprio controllo locale della modalità debug, è una violazione da correggere subito, non da lasciare per dopo.
- **Verifica automatica di questo vincolo**: in M1.14, oltre al test funzionale (stderr popolato solo con debug attivo), aggiungere un test/check che invoca le funzioni di log da un modulo di test con debug disattivato e verifica che **zero byte** vengano scritti su stderr, indipendentemente da quante chiamate di log siano state fatte — così un futuro modulo che "dimentica" di passare dal logger centrale (es. usa `fprintf(stderr, ...)` diretto) viene scoperto dal test, non da un controllo manuale file per file.

Questa modalità va implementata **in M1.14 insieme al resto di `core/logger.c`**, non aggiunta in seguito: ogni modulo scritto dopo M1 deve usare fin da subito le macro/funzioni di logging categorizzato invece di `printf`/`fprintf` ad-hoc, così che la modalità debug sia utile davvero su tutta la codebase e non solo su una parte.

### Scorciatoie da tastiera cross-platform
**Correzione importante dopo verifica**: in GTK4 (a differenza di GTK3) la sintassi accelerator `"<Primary>"` **non** si rimappa più automaticamente a Cmd su macOS — è ormai un semplice sinonimo hardcoded di `"<Control>"` su tutte le piattaforme (breaking change GTK3→4, confermato nella migration guide ufficiale). Per ottenere `Ctrl+` su Linux/Windows e `Cmd+` su macOS per la stessa azione, ogni shortcut in `keymap.c` deve usare un trigger doppio esplicito, es. `gtk_shortcut_trigger_parse_string("<Control>o|<Meta>o")`, oppure un prefisso scelto a compile-time (`#ifdef __APPLE__` → `<Meta>`, altrimenti `<Control>`). Va applicato a tutta la checklist di M5.2, non lasciato implicito come nella prima stesura del piano.

### Finestra frameless e resize
`gtk_window_set_decorated(FALSE)`, titlebar custom draggable, resize con `gdk_toplevel_begin_resize()` + edge-hit-testing (8-10px) — API GDK4 pensata apposta per questo caso e confermata funzionante su Win32 (mappa su `WM_SYSCOMMAND`/`SC_SIZE`, anche se disabilita l'ombra DWM di default su finestre non decorate) e su Wayland/X11. **Caveat macOS verificato**: Cocoa non ha un'API pubblica equivalente a `performWindowResizeWithEvent:` per finestre `NSWindowStyleMaskBorderless`; GDK implementa un tracking interno degli eventi mouse che può risultare meno fluido rispetto a Linux/Windows. Da trattare come rischio noto (non bloccante, ma da verificare per primo su CI/runner macOS in M7.7, dato che macOS è la piattaforma prioritaria) — fallback esplicito già previsto (solo maximize/restore) se il resize custom risulta inaccettabile su macOS.

## Milestone (granulari)

Ogni sotto-milestone è pensata per essere completabile e verificabile in una singola sessione di lavoro breve, con un criterio di verifica concreto ed eseguibile. Le fasi M0-M8 restano i checkpoint macro, ma il lavoro reale procede per sotto-step.

**Nota trasversale**: a partire da M1.14 (logger + modalità debug), ogni sotto-milestone successiva che introduce un nuovo componente (pipeline video, sync, markers, export, UI) deve usare le funzioni di logging categorizzato di `core/logger.c` per i propri eventi significativi, non `printf`/`fprintf` ad-hoc — è quanto rende utile la modalità debug end-to-end invece che solo su una parte della codebase.

### M0 — Scaffolding
- **M0.1** Struttura directory vuota + `meson.build` minimale (`project('syncview','c', default_options:['c_std=c11'])`), `main.c` che ritorna 0. *Verifica*: `meson setup build && meson compile -C build && ./build/src/syncview` (nessuna dipendenza ancora).
- **M0.2** Aggiungere dipendenza GTK4 (>=4.10), `GtkApplication` minimale che apre una finestra vuota. *Verifica*: la finestra si apre e si chiude senza crash/warning.
- **M0.3** Aggiungere le restanti dipendenze una alla volta nel `meson.build` (`gstreamer-1.0`, `gstreamer-pbutils-1.0` per `GstDiscoverer`, `gstreamer-video-1.0`, `gstreamer-plugins-bad-1.0` se serve l'header di `gtk4paintablesink`, sqlite3, glib-2.0/gio-2.0, json-glib-1.0), per ciascuna una chiamata di init/smoke-test in `main.c` poi rimossa. *Verifica*: `meson setup build` risolve tutte le dipendenze senza errori su ciascuna aggiunta incrementale; verificare anche che il plugin `gtk4paintablesink` sia effettivamente caricabile (`gst-inspect-1.0 gtk4paintablesink`, richiede il pacchetto `gst-plugins-gtk4`/`gstreamer-plugins-good` a seconda della distro).
- **M0.4** `tests/meson.build` con un singolo test fittizio (`assert(1==1)`) collegato a `meson test`. *Verifica*: `meson test -C build` esegue e passa.
- **M0.5** `README.md` iniziale (dipendenze, comandi build) + `docs/ARCHITECTURE.md` stub + `.gitignore` per `build/`. *Verifica*: file presenti, `git status` pulito dopo build.
- **M0.6** CI multi-piattaforma (GitHub Actions, matrice `ubuntu-latest`/`windows-latest`/`macos-latest`) che esegue `meson setup && meson compile` su tutte e tre. *Verifica*: workflow verde su tutte e tre le piattaforme per lo scaffolding minimo di M0.1-M0.4 — è l'unico modo di sapere presto se qualcosa non compila su macOS/Windows, dato che lo sviluppo locale è su Linux.

### M1 — Core logic + unit test (nessuna dipendenza da GTK)
- **M1.1** `util/time_format.c` (`HH:MM:SS.mmm`) + test unitario con casi limite (0ms, >1h, ms singoli). *Verifica*: test passa.
- **M1.2** `core/settings.c/.h` con le costanti (step ms 40/33/100/200, zoom 1.0–5.0, MAX_VIDEOS=4, ecc. — da confermare leggendo `config/settings.py` per intero). *Verifica*: header compila, incluso da un test smoke.
- **M1.3** `core/sync_manager.c`: struct + init/set-get offset/enabled/master. *Verifica*: test unitario su getter/setter e stato iniziale.
- **M1.4** `sync_manager_calculate_sync_position` + test unitario (inclusi i casi di clamp a 0 e offset uguali/diversi).
- **M1.5** `sync_manager_sync_all_to_master` con `SyncPlayerOps` mock (finti player in test) + test unitario che verifica seek+pause chiamati con i valori attesi su ogni player mock.
- **M1.6** `core/markers.c`: struct `Marker` + `marker_new`/`marker_free` (gestione stringhe owned) + test su generazione id e default category.
- **M1.7** `MarkerStore`: add/remove/update con mantenimento ordine per timestamp (inserimento ordinato) + test su CRUD base.
- **M1.8** Binary search interna (query `get_at`/`get_next`/`get_previous`/range) + test che confronta risultati binary search vs linear scan su un dataset generato casualmente (es. 200 marker).
- **M1.9** `core/marker_db.c`: apertura DB + creazione schema (le 2 tabelle + 4 indici, verbatim). *Verifica*: apertura su file temporaneo, ispezione schema via `sqlite3_table_info` in test.
- **M1.10** `marker_db_save_batch` (upsert `ON CONFLICT DO UPDATE`) + `marker_db_load_all` (esclude `is_deleted=1`) + test round-trip (salva N marker, ricarica, confronta).
- **M1.11** `marker_db_delete` (soft delete) + test che verifica `is_deleted=1` e assenza da `load_all`.
- **M1.12** Migrazione JSON legacy → SQLite (fixture JSON di test) + rinomina in `.json.backup` + test che verifica sia il contenuto migrato sia l'esistenza del backup.
- **M1.13** `core/user_paths.c`: 4 slot + last_export_dir, persistenza json-glib in path iniettato (non hardcoded, per testabilità) + test di round-trip su file temporaneo.
- **M1.14** `core/logger.c`: categorie `log_user_action/log_video_action/log_playback/log_timeline_seek/log_error/log_export` (+ `log_sync`/`log_gst` nuove rispetto all'originale, per coprire gli eventi extra della modalità debug), file troncato ad ogni apertura + **modalità debug** (`SYNCVIEW_DEBUG=1`/`--debug`: log anche su stderr in tempo reale con timestamp+categoria, verbosità estesa, propagazione opzionale di `GST_DEBUG`) fin da subito — vedi sezione "Modalità debug" in Architettura. *Verifica*: test smoke che verifica righe scritte nel file in entrambe le modalità, e che stderr riceva output solo quando la modalità debug è attiva (leggere prima il path esatto da `core/logger.py`).
- **M1.15** Passata completa `meson test -C build` con `-Db_sanitize=address` su tutti i test M1.1–M1.14. *Verifica*: zero errori ASan, zero leak.

### M2 — Finestra minima con 1 video
**Rivista dopo verifica cross-platform**: niente più `GtkVideo`/`GtkMediaFile`/`ffprobe`, si passa direttamente a `GstDiscoverer` per il probing e a una pipeline `playbin3`+`gtk4paintablesink` per il playback (vedi Architettura).
- **M2.1** Leggere per intero `core/video_loader.py` (per capire quali campi/euristiche di fallback FPS replicare, anche se l'implementazione cambia da ffprobe a GstDiscoverer) e annotare in `docs/MIGRATION_NOTES.md` il comportamento atteso.
- **M2.2** `core/discoverer.c`: `gst_discoverer_new()`/`gst_discoverer_discover_uri()` sincrono, estrazione fps (num/denom), durata, width/height, codec da `GstDiscovererVideoInfo`. *Verifica*: test manuale su un file video reale, valori confrontati con `ffprobe`/`gst-discoverer-1.0` da riga di comando (solo come sanity check esterno, non come dipendenza runtime).
- **M2.3** `video/video_player.c`: scheletro `SyncviewVideoPlayer` GObject (`G_DECLARE_FINAL_TYPE`, `_new()`, pipeline `gst_parse_launch("playbin3 video-sink=gtk4paintablesink")` creata ma non ancora avviata). *Verifica*: compila, tipo registrato, `GtkPicture` collegato al paintable del sink mostra un frame nero/vuoto.
- **M2.4** `load()`: `g_object_set(playbin, "uri", uri, NULL)` + `gst_element_set_state(GST_STATE_PAUSED)`, gestione bus message asincrona (`GST_MESSAGE_ASYNC_DONE`) per sapere quando la pipeline è pronta. *Verifica*: caricare un file, primo frame visibile nel `GtkPicture`, nessun crash.
- **M2.5** `play()`/`pause()`/`stop()` come transizioni di stato GStreamer (`GST_STATE_PLAYING`/`PAUSED`/`NULL`). *Verifica*: test manuale, il video parte/si ferma visivamente.
- **M2.6** Segnali `position-changed`/`duration-changed`: timer di polling (`gst_element_query_position`/`query_duration` in nanosecondi, convertiti subito in ms) dato che GStreamer non notifica il position change come property `notify::` — la property `position`/`duration` di `playbin` va interrogata attivamente. *Verifica*: log stampa i valori in ms che cambiano durante la riproduzione, coerenti con la durata reale del file.
- **M2.7** `seek()` (`gst_element_seek_simple`) + `step_ms()` (seek relativo) + `set_playback_rate()` (`gst_event_new_seek` con rate ≠ 1.0, come da ricerca). *Verifica*: test manuale, seek a metà video, frame-step avanti/indietro visibili, cambio rate udibile/visibile (video accelerato/rallentato).
- **M2.8** Finestra minima (`main_window.c` v0): bottone "apri file" + un solo `SyncviewVideoPlayer` embeddato. *Verifica*: flusso completo apri→play→pausa→seek→chiudi senza crash, log coerente, nessun processo/thread GStreamer orfano dopo la chiusura (verificare con `gst_deinit()` in cleanup).

### M3 — Grid 4 video + sync
- **M3.1** `ui/video_grid.c`: 4 placeholder statici in griglia 2x2 (nessun player reale ancora). *Verifica*: layout visivo corretto a diverse dimensioni finestra.
- **M3.2** Drag&drop file su un placeholder (`GtkDropTarget`) con verifica estensione supportata. *Verifica*: trascinare un file video reale carica lo slot corretto.
- **M3.3** Click su placeholder vuoto apre `GtkFileDialog` per selezione file. *Verifica*: selezione manuale carica lo slot.
- **M3.4** Istanziare 4x `SyncviewVideoPlayer` reali nella griglia, ognuno indipendente. *Verifica*: caricare 4 video diversi, ognuno gioca autonomamente.
- **M3.5** Controlli UI minimi per offset per-video (es. spin button temporanei, non definitivi) collegati a `sync_manager_set_offset`. *Verifica*: cambiare offset e leggerlo via log.
- **M3.6** Selezione master video (click per focus) → `sync_manager_set_master`. *Verifica*: focus visivo cambia, log conferma master index.
- **M3.7** Bottone/shortcut "resync" → `sync_manager_sync_all_to_master` reale sui 4 player. *Verifica*: con offset diversi impostati, dopo resync tutti i video sono in posizione coerente e in pausa (verifica a occhio + confronto timestamp in log).
- **M3.8** `unload()`/reload nello stesso slot senza distruggere il widget. *Verifica*: ASan pulito su un ciclo carica→scarica→ricarica ripetuto 10 volte (contratto equivalente a `test_memory_leak_fix.py`).
- **M3.9** Step globale (`Left/Right`, `Shift+Left/Right` provvisori) che applica lo stesso delta ms a tutti i player caricati. *Verifica*: tutti i video avanzano/indietreggiano dello stesso delta visibile.

### M4 — Timeline + markers
- **M4.1** Leggere per intero `ui/timeline_widget.py`, annotare in `docs/MIGRATION_NOTES.md` eventuali dettagli non coperti dal piano (es. zoom della timeline stessa, formati esatti dei tick).
- **M4.2** `ui/timeline_widget.c`: `GtkDrawingArea` con solo sfondo ruler + fill di progresso. *Verifica*: visivamente coerente, si aggiorna seguendo la posizione del player.
- **M4.3** `timeline_compute_ticks()` come funzione pura (intervallo adattivo 10/20/30/60s) + unit test isolato (no GTK). *Verifica*: test passa per più durate campione.
- **M4.4** Disegno tick + etichette durata (Pango). *Verifica*: confronto visivo con originale su un video di durata nota.
- **M4.5** Disegno playhead + badge tempo. *Verifica*: badge segue la posizione corrente senza glitch ai bordi.
- **M4.6** Rendering marker (triangoli) leggendo dal `MarkerStore` reale. *Verifica*: marker aggiunti via test/fixture appaiono nella posizione x corretta.
- **M4.7** Hit-testing marker su click (binary search + tolleranza px→ms) → segnale `marker-clicked`. *Verifica*: click su un marker lo seleziona, click altrove esegue seek.
- **M4.8** `Ctrl+M` aggiunge marker reale, persistito subito su `marker_db` (event-driven, no timer). *Verifica*: marker visibile immediatamente sulla timeline e presente nel file `.db` (ispezionabile con `sqlite3` CLI).
- **M4.9** `P`/`N` navigazione marker precedente/successivo con seek sincronizzato (passa da `sync_manager_calculate_sync_position` sui player). *Verifica*: navigazione corretta anche con offset diversi tra i video.
- **M4.10** Distinzione marker globali (tutti i video) vs per-video (solo un player) nel rendering/associazione. *Verifica*: un marker globale appare su tutte le timeline coinvolte, uno specifico solo sulla sua.
- **M4.11** Roundtrip persistenza: chiudere e riaprire l'app, verificare che i marker vengano ricaricati dal DB nello stato corretto (categoria, colore, descrizione, video_index). *Verifica*: test manuale end-to-end.

### M5 — Dialoghi e shortcut completi
- **M5.1** `ui/keymap.c`: tabella centrale shortcut→azione, wiring scheletro (azioni vuote/log-only inizialmente).
- **M5.2** Wiring reale shortcut per shortcut, uno alla volta, spuntando la checklist completa: Space, Ctrl+O, F1, Ctrl+S, Ctrl+F, Ctrl+R, Home, End, M, ←, →, Shift+←, Shift+→, Ctrl+M, P, N, Ctrl+E, Ctrl+0 — ciascuno con trigger doppio `<Control>X|<Meta>X` (non `<Primary>`, vedi correzione in Architettura) per funzionare come Cmd+ su macOS e Ctrl+ su Linux/Windows. *Verifica*: checklist manuale su Linux (Ctrl) e, appena possibile via CI/accesso macOS, conferma che lo stesso binding risponda a Cmd.
- **M5.3** `ui/dialog_markers.c`: lista marker (`GtkColumnView`) con colonne timestamp/categoria/colore/descrizione. *Verifica*: apre e mostra i marker reali del progetto corrente.
- **M5.4** Filtro per categoria (`GtkFilterListModel`). *Verifica*: filtro riduce/ripristina la lista correttamente.
- **M5.5** Editing inline descrizione (popover o cella editabile) → `marker_store_update` + persist. *Verifica*: modifica visibile subito e sopravvive a riavvio app.
- **M5.6** Export CSV dalla dialog (riusa `marker_store_export_csv`). *Verifica*: file CSV generato, diffabile (stesse colonne/ordine) contro un export equivalente della versione Python su marker identici.
- **M5.7** `ui/dialog_fps.c`: spinbutton + preset (24/25/29.97/30/50/59.94/60). *Verifica*: selezione preset aggiorna lo spinbutton, valore applicato a `playback_rate`.
- **M5.8** `ui/dialog_export.c`: selezione cartella (`GtkFileDialog`), qualità (`GtkDropDown`), slider sec_before/sec_after 0–60s — solo raccolta parametri, nessuna esecuzione reale ancora. *Verifica*: parametri raccolti correttamente loggati.

### M6 — Export pipeline reale
- **M6.1** `core/export_worker.c`: encoder detection (`ffmpeg -hide_banner -encoders`, parsing NVENC/QSV/VAAPI/VideoToolbox/libx264). *Verifica*: test manuale su macchina con/senza GPU, fallback software corretto.
- **M6.2** Costruzione comando ffmpeg identico all'originale (leggere per intero `core/advanced_exporter.py` righe della funzione `export_clip_ffmpeg` prima di scrivere) + unit test che verifica la stringa/argv generato per input noti.
- **M6.3** Validazione finestra export (available_before/after, confronto stretto `<`) come funzione pura + unit test che replica esattamente i casi di `test_marker_validation.py`.
- **M6.4** Esecuzione singolo job via `GSubprocess` sincrono dentro un worker. *Verifica*: manuale, un marker → un clip corretto (durata/qualità verificate con `ffprobe` sull'output).
- **M6.5** Generazione job multipli (un job per combinazione marker×video applicabile: globale→tutti i video caricati, specifico→solo quel video). *Verifica*: numero di job generato corretto su un caso con marker misti.
- **M6.6** `GThreadPool` con `max_workers = num_processori - 1`, esecuzione parallela reale. *Verifica*: più clip prodotte in parallelo, tempo totale inferiore alla somma sequenziale.
- **M6.7** Retry logic (timeout 5 min, 3 retry default) nel loop di polling, non nel worker. *Verifica*: simulare un fallimento (path invalido) e osservare i retry in log fino a fallimento definitivo.
- **M6.8** Cancellazione cooperativa (flag atomico + `g_subprocess_force_exit` sul job attivo). *Verifica*: annullare un export in corso ferma i job in modo pulito, nessun processo ffmpeg orfano (`ps aux | grep ffmpeg` dopo cancel).
- **M6.9** `core/export_queue.c`: persistenza `~/.syncview/export_queue.json`, salvataggio sincrono ad ogni mutazione. *Verifica*: ispezione manuale del file dopo ogni operazione (add/update/remove).
- **M6.10** Test crash-resume: killare il processo a metà export, riavviare l'app, verificare che la coda venga ripresa/segnalata correttamente dallo stato persistito.
- **M6.11** Wiring UI: `dialog_export.c` avvia la pipeline reale, progress/completamento/errori marshalled verso il main thread via `g_idle_add`/`GTask` (mai toccare widget dal thread pool). *Verifica*: barra di progresso/stato aggiornata correttamente durante un export reale multi-clip.

### M7 — Polish (tema, zoom/pan, finestra)
- **M7.1** `ui/style.css` base con palette "Command" (`#11130f`/`#a9b271`/`#c47c4c`) applicata globalmente. *Verifica*: confronto visivo con riferimento praesidium.artysan.me.
- **M7.2** `video/zoom_pan.c`: matematica zoom-verso-cursore (`pan' = mouse - (mouse - pan) * (new_zoom/old_zoom)`) come funzione pura + unit test con valori noti.
- **M7.3** UI "zoom stepper" (`−`/valore/`+`) nello stile del riferimento, collegata a `zoom_pan` + gesture Ctrl+wheel. *Verifica*: zoom fluido, punto sotto il cursore resta fermo.
- **M7.4** Pan via drag quando zoomato (`GtkGestureDrag`). *Verifica*: drag sposta la vista senza salti.
- **M7.5** Fit-to-view automatico + `Ctrl+0` reset zoom/pan. *Verifica*: reset riporta esattamente allo stato iniziale.
- **M7.6** `ui/titlebar.c`: titlebar custom draggable (`gdk_toplevel_begin_move`), bottoni min/max/close custom. *Verifica*: drag sposta la finestra, bottoni funzionano.
- **M7.7** Resize frameless con `gdk_toplevel_begin_resize` + edge-hit-testing (8-10px) su tutti gli 8 edge/corner. *Verifica*: resize manuale fluido da ogni lato/angolo, nessun glitch.
- **M7.8** Stati vuoti/loading/errore (video non caricato, nessun marker, errore probing, errore export) in stile "UI states" del riferimento. *Verifica*: ogni stato raggiungibile manualmente e visivamente coerente.
- **M7.9** QA manuale completa: ripercorrere tutta la checklist shortcut (M5.2) + tutti i flussi (carica, sync, marker, export, zoom/pan, resize) in un'unica sessione end-to-end senza restart.

### M8 — Packaging (per tutte e tre le piattaforme)
- **M8.1** Regole `meson install` (binario, risorse, icone) — comuni a tutte le piattaforme.
- **M8.2** **Linux**: file `.desktop` + icona applicazione, eventuale AppImage. *Verifica*: build pulita su container Linux "vergine" seguendo solo il README.
- **M8.3** **macOS**: `.app` bundle standalone via script Meson post-install (`meson.add_install_script()`) + **`macdylibbundler`** (fork attivo `auriamg/macdylibbundler` — `gtk-mac-bundler` è confermato obsoleto/non funzionante con GTK4) per riscrivere `@rpath`/`install_name_tool`. Asset obbligatori da includere nel bundle (pena crash all'avvio, verificati): schemi GSettings compilati (`glib-compile-schemas` → `Contents/Resources/share/glib-2.0/schemas/`), icone Adwaita/hicolor minime, plugin GStreamer in `Contents/Frameworks/gstreamer-1.0/` con `GST_PLUGIN_SYSTEM_PATH_1_0` impostata a runtime relativa al bundle path, e `lib/gtk-4.0/4.0.0/media/` se si usano moduli media GTK. Build con Homebrew su CI `macos-latest`. *Verifica*: CI produce un `.app` che si avvia su un runner macOS pulito senza Homebrew installato — priorità massima essendo la piattaforma target più importante ma meno testabile localmente.
- **M8.4** **Windows**: eseguibile + DLL via MSYS2 MinGW-w64/UCRT64, risoluzione ricorsiva delle DLL dipendenti con **`ntldd`** (pacchetto `mingw-w64-ucrt-x86_64-ntldd`) in uno script di packaging. Asset obbligatori: `share/glib-2.0/schemas/gschemas.compiled`, `lib/gstreamer-1.0/*.dll` (con `GST_PLUGIN_PATH` impostata via `g_setenv()` a un percorso relativo all'eseguibile), eventuale `lib/gtk-4.0/4.0.0/media/`, icone e `lib/gdk-pixbuf-2.0/2.10.0/loaders.cache`. Build su CI `windows-latest`. *Verifica*: CI produce un eseguibile funzionante su un runner Windows pulito.
- **M8.5** Verifica di parità comportamentale tra le tre build: stesso set di funzionalità testato manualmente (checklist M5.2/M7.9) ripetuto almeno su Linux (locale) e, tramite CI/runner remoti o macchine disponibili, su Windows e macOS — con enfasi su macOS data la priorità.
- **M8.6** `README.md` definitivo con elenco dipendenze runtime per piattaforma (gtk4 ≥4.10, plugin gstreamer **base+good+bad+libav** — corretto da "base+good", vedi Rischi — sqlite3, ffmpeg esterno solo per l'export) e istruzioni di build separate per Linux/macOS/Windows, incluse le note di licenza su `gst-libav` se distribuito pubblicamente.

## Rischi e gap noti

1. **Thread-safety SQLite**: pattern originale (apri/chiudi per operazione, nessuna connessione persistente) mantenuto 1:1, con l'unica aggiunta di `sqlite3_busy_timeout()` per ridurre errori spuri sotto contesa — deviazione minima e a rischio zero.
2. **Regola single-thread GTK4**: il thread pool di export non deve mai toccare direttamente widget UI — tutto marshalled via `g_idle_add`/`GTask`. Più rigido di quanto richiesto da Qt/PyQt, da disciplinare fin da M6.
3. **Resize frameless**: risolto deliberatamente in M7 con `gdk_toplevel_begin_resize` (gap mai chiuso nell'originale); fallback esplicito (solo maximize/restore) se emergono complicazioni HiDPI/multi-monitor impreviste.
4. **Frame-accurate seek assente**: limite ereditato dall'architettura playbin/GtkMediaFile, identico all'originale QMediaPlayer — da documentare per l'utente finale, non risolvibile senza una pipeline GStreamer manuale (fuori scope).
5. **Memory management manuale**: rischio leak/UAF sistemico nel passaggio da Python GC a C, specialmente su stringhe owned (`Marker.description`) e lifecycle di `SyncviewVideoPlayer` su unload/reload. Da verificare con ASan/valgrind ad ogni milestone, non solo a fine progetto.
6. **Squilibrio priorità/testabilità multi-piattaforma**: macOS è la piattaforma target più importante ma la meno testabile durante lo sviluppo (che avviene su Linux). Mitigazione: CI multi-piattaforma fin da M0.6 (non solo a fine progetto), evitare API/assunzioni Linux-specifiche (X11, path POSIX hardcoded, `Ctrl+` invece degli accelerator astratti `<Primary>`), e trattare ogni milestone UI (M2-M7) come "da verificare anche su CI macOS/Windows", non solo su Linux locale.
7. **Parità dei plugin GStreamer tra piattaforme**: verificato che il set minimo reale per riprodurre H.264/HEVC e usare i decoder hardware nativi è **base+good+bad+libav** (non base+good come ipotizzato inizialmente) — da fissare esplicitamente in M8, con attenzione a eventuali implicazioni di licenza di `gst-libav` in caso di distribuzione pubblica futura.
8. **`GtkMediaStream`/`GtkVideo` non idonei per il playback reale**: non supportano `playback-rate` (nessuna API esiste in GTK4). Il piano è stato rivisto per usare direttamente una pipeline `playbin3` + `gtk4paintablesink` per ogni player (vedi Architettura → Toolkit e librerie, e Video playback). Questo aumenta la complessità di M2/M3 rispetto alla prima stesura (gestione bus message, stati pipeline, rate via seek-event) ma è necessario per replicare la funzionalità dell'originale.
9. **Unità di misura GStreamer**: `GstMediaStream`/pipeline GStreamer lavorano nativamente in **nanosecondi** (non microsecondi come inizialmente scritto, né millisecondi come l'originale/schema SQLite) — ogni punto di conversione (`video_player.c`, `discoverer.c`) deve convertire esplicitamente con `GST_TIME_AS_MSECONDS`/`ms * GST_MSECOND`, altrimenti rischio sistematico di bug di scala (fattore 1.000.000) difficili da individuare a occhio nei test manuali.
10. **Resize frameless su macOS**: Cocoa non espone un'API pubblica di resize interattivo per finestre borderless; il tracking interno di GDK può risultare meno fluido che su Linux/Windows — priorità di verifica alta in M7.7 data la priorità di macOS come target.
8. **File non ancora letti integralmente** in questa fase di pianificazione — vanno letti per intero all'inizio della milestone che li tocca (non assumere il contenuto a memoria): `core/video_loader.py` (M2, flag ffprobe esatti), `ui/timeline_widget.py` (M4, eventuale zoom timeline), `config/settings.py` (M1, elenco completo costanti), path esatto del file di log in `core/logger.py` (M1).

## Prompt per un'altra IA (se serve ricerca esterna)

**Aggiornamento: la ricerca sotto è già stata eseguita e i risultati sono stati integrati nel piano** (vedi le note "dopo verifica"/"corretto dopo verifica" sparse in Architettura, Milestone M0/M2/M5/M8 e Rischi — in sintesi: playback-rate richiede pipeline GStreamer manuale invece di `GtkVideo`, probing passa a `GstDiscoverer`, plugin richiesti sono base+good+bad+libav, `<Primary>` non mappa più a Cmd in GTK4, resize frameless ha limitazioni note su macOS, packaging richiede `macdylibbundler`/`ntldd`). Il prompt resta qui per eventuali verifiche successive più mirate (es. una volta scelta la versione GTK4/GStreamer esatta da pinnare, o per dettagli implementativi più fini incontrati durante le milestone).

Se in una sessione futura serve verificare dettagli API GTK4/GStreamer aggiornati (la cui superficie può essere cambiata rispetto alla conoscenza di training) e soprattutto il loro comportamento **su Windows e macOS** (le piattaforme meno testabili in locale durante lo sviluppo, ma macOS è il target più importante), questo prompt è pronto da incollare in un'IA con accesso web:

> Sto scrivendo un'app desktop **cross-platform** (Linux, Windows, macOS — build native su tutte e tre, stesso comportamento richiesto ovunque) in C puro con GTK4 (>= 4.10) e GStreamer. Sviluppo principalmente su Linux, quindi mi servono conferme aggiornate soprattutto sul comportamento **su Windows e macOS**, dove non posso testare in continuo:
>
> 1. `GtkVideo`/`GtkMediaFile` — l'API per leggere `timestamp`/`duration` e ricevere notifiche (`notify::timestamp` esiste su `GtkMediaStream`? nomi esatti delle property GObject?) è la stessa su tutti i backend (Win32, Quartz/macOS, X11/Wayland), o alcuni backend hanno limitazioni note (es. GStreamer non disponibile/diverso su macOS via Homebrew, o backend media diverso su Windows)?
> 2. `gtk_media_stream_set_playback_rate()` — firma, range supportato, e se il comportamento è garantito identico su Win32/Quartz.
> 3. `gdk_toplevel_begin_resize()`/`begin_move()` — firma esatta, enum `GdkSurfaceEdge`; **sono realmente implementate e funzionanti sui backend Win32 e Quartz di GDK4**, o sono principalmente testate/stabili solo su X11/Wayland? Serve una finestra frameless (client-side-decorated) con resize manuale su tutte e tre le piattaforme.
> 4. `GtkFileDialog` (sostituto di `GtkFileChooserDialog` da GTK 4.10) — API async esatta, e se il dialog nativo del sistema operativo viene mostrato correttamente su Windows e macOS (non solo un fallback GTK generico).
> 5. Se `GstDiscoverer`/`playbin` tramite `GtkMediaFile` espone `fps`/framerate del video in modo equivalente su tutte le piattaforme, o se conviene comunque un probing esterno via `ffprobe` per garantire lo stesso risultato ovunque.
> 6. **Packaging**: qual è oggi (2026) il modo raccomandato per produrre un `.app` bundle macOS "standalone" (con GTK4/GStreamer/sqlite bundlati, non dipendenti da Homebrew installato sull'utente finale) da un progetto Meson — esiste un tool maturo equivalente a `linuxdeploy`/`windeployqt` per GTK4 su macOS (es. `gtk-mac-bundler`, `dylibbundler`)? E l'equivalente per produrre un eseguibile Windows standalone (DLL bundling) da un progetto MSYS2/MinGW.
> 7. Disponibilità di default dei plugin GStreamer (base/good/bad, in particolare per decoder hardware) sui pacchetti ufficiali Windows/macOS di gstreamer.freedesktop.org, per capire se il set "base+good" è davvero sufficiente per una parità di comportamento tra le tre piattaforme.
> 8. La sintassi accelerator GTK `"<Primary>"` mappa davvero automaticamente a Cmd su macOS e Ctrl su Linux/Windows nelle versioni GTK4 recenti? Conferma con riferimento alla documentazione.
>
> Per ciascun punto, citami la versione GTK4/GStreamer in cui il comportamento è stabile/documentato e un link alla documentazione ufficiale (docs.gtk.org / gstreamer.freedesktop.org). Non serve codice completo, solo conferme di esistenza/stabilità/parità cross-platform delle API.

## Verifica end-to-end del piano stesso

Non essendoci ancora codice, la "verifica" di questo piano consiste nel procedere milestone per milestone come descritto sopra, ciascuna con criteri di successo eseguibili (build, `meson test`, ASan, confronto output con la versione Python di riferimento su `main`). Prima di M0, ripulire il branch `SyncView-C` dai file Python esistenti e impostare la struttura directory C descritta sopra.
