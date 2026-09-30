# Note di migrazione — comportamento dell'originale Python e deviazioni in SyncView-C

Raccoglie (1) il comportamento dell'app Python originale che il porting C deve replicare, letto dal codice sul branch `main`, e (2) le deviazioni deliberate. Per le decisioni interne ai moduli già scritti vedi [ARCHITECTURE.md](ARCHITECTURE.md).

Ogni sezione indica i file Python di origine e la milestone che la usa.

---

## Caricamento video e probing metadati (M2.1 → usata da M2.2, M2.4, M2.8)

Sorgenti lette per intero: `core/video_loader.py`; dei consumatori `ui/video_player.py` (`load_video`, `_load_video_sync`, `on_video_info_ready`, `on_video_load_error`, `detect_video_fps`, reset a riga ~1255) e `ui/main_window.py` (selettore FPS righe ~898-977, aspect ratio finestra righe ~1033-1104).

### Cosa viene estratto (`VideoInfoWorker.probe_video_info`)

`ffprobe -v error -select_streams v:0 -show_entries stream=r_frame_rate,duration,width,height,codec_name -of json <file>`, timeout **10 s**. Solo il **primo stream video**. Risultato (`info`):

| Campo | Origine | Default se assente/errore | Usato da |
|---|---|---|---|
| `path` | path passato | — | `setSource`, `video_path`, log |
| `fps` | `r_frame_rate` = `num/den` (frazione), solo se `den > 0` | `0.0` | label FPS, `playback_rate` |
| `duration` | `stream.duration` (s) × 1000 → `int` ms | `0` | **da nessuno**: la UI usa la durata del media player |
| `width`, `height` | `stream.width/height` (pixel codificati, nessuna gestione rotazione/PAR) | `0` | aspect ratio della finestra (`main_window` ~1033-1104, attivo solo se entrambi > 0) |
| `codec` | `codec_name` | `'unknown'` | **da nessuno** |

Note di dettaglio:
- Si usa `r_frame_rate` (frame rate "di base" di ffprobe), **non** `avg_frame_rate`. Per video a frame rate variabile i due possono differire.
- `duration` dello stream spesso manca (es. Matroska): in quel caso 0, ed è irrilevante perché non viene usata.

### Fallback e gestione errori

- **File assente** → `error_occurred(index, "File non trovato")`. Prima ancora, `load_video` fa un proprio controllo `exists()` e solleva `FileNotFoundError("File non trovato: <path>")`.
- **ffprobe assente, timeout, JSON non valido, frazione non parsabile** → `log_error("Errore ffprobe per <nome>", ...)` e **si prosegue** con le info di default (`fps=0.0`, `width=height=0`, `codec='unknown'`, `duration=0`): viene comunque emesso `info_ready` (non `error_occurred`) e il video viene caricato. Il caricamento fallisce solo se il file non esiste o c'è un'eccezione imprevista.
- **ffprobe con `returncode != 0`** → silenzio (nessun log), stesse info di default.
- **`den <= 0`** → `fps` resta `0.0`.
- **`fps == 0`** significa "FPS sconosciuti": label **"AUTO FPS"** invece di `"{fps:.2f} FPS"`, e **nessun** `playback_rate` viene applicato (`main_window` controlla `detected_fps > 0`).
- **Errore durante il caricamento asincrono** → UI: esce dallo stato modale, nasconde lo skeleton, mostra `"⚠ ERRORE\n\nErrore caricamento:\n<messaggio>"`, `is_loading=False`, `log_error("Errore caricamento asincrono Feed-N", ...)`.

### Sequenza di caricamento (`VideoPlayerWidget.load_video`)

1. Normalizza il path, verifica che esista.
2. **Salva subito il path** in `user_path_manager.set_video_path(index, path)` (quindi persiste su `user_paths.json` anche se il probing poi fallisce) + log `Percorso video salvato`.
3. Stato "caricamento": skeleton con nome file, status `"⏳ CARICANDO..."`, **stato modale** che disabilita load/remove/play/prev-frame/next-frame/add-marker.
4. Probing **asincrono** in un thread per player (un worker per `video_index`; un nuovo caricamento nello stesso slot fa prima `cleanup_thread`, che attende il thread precedente fino a 2 s). Log `Caricamento asincrono avviato` (`Feed-N: <nome>`).
5. A `info_ready`: salva `width/height/fps`, imposta label FPS, **solo ora** fa `setSource(path)` sul media player (il media viene caricato *dopo* il probing), nasconde skeleton, `is_loaded=True`, `is_loading=False`, esce dallo stato modale, nasconde "carica" e mostra "rimuovi"/"muto", emette `video_load_state_changed(index, True)`, adatta il video alla vista. Log `Info video ricevute` (`FPS: x.xx, Size: WxH`) e `Video caricato (async)`.
6. Rimozione video: `detected_fps=0`, `is_loaded=False`, `video_path=None`, timeline a 0/0, placeholder di nuovo visibile.

Il ramo `_load_video_sync`/`detect_video_fps` (legacy, `async_load=False`, timeout 5 s, solo `r_frame_rate`) non viene portato.

### Come il resto dell'app usa `fps` (contesto per M3+)

- Selettore FPS ("Auto"/preset/"Personalizzato"): `get_selected_fps()` ritorna `None` per "Auto".
- `playback_rate = detected_fps / target_fps` (es. video 24 fps, target 60 → 0.4x; video 60, target 24 → 2.5x), applicato solo a player caricati con `detected_fps > 0`; con "Auto" il rate è 1.0. Alla selezione del FPS e ad ogni nuovo caricamento (`main_window` ~935-977).
- "Personalizzato" apre `FPSDialog` (range 1–240, 3 decimali, step 0.001); annullando si torna ad "Auto".

### Deviazioni previste per SyncView-C (da M2.2)

| Originale | SyncView-C | Perché / rischio |
|---|---|---|
| `ffprobe` esterno via subprocess | `GstDiscoverer` in-process (`gst_discoverer_discover_uri`) | Nessuna dipendenza runtime da ffprobe, cross-platform. `ffprobe`/`gst-discoverer-1.0` restano solo come sanity check manuale |
| `r_frame_rate` | `framerate_num/denom` del caps video dal discoverer | Possono differire su VFR: verificare in M2.2 su file reali. Un VFR che il discoverer riporta come `0/1` ricade naturalmente nel caso "AUTO FPS" |
| Timeout 10 s | `gst_discoverer_new(10 * GST_SECOND)` | Stesso limite |
| Thread Qt + segnali | Discover sincrono in un worker (`GTask`), risultato al main thread (`g_idle_add`/callback del task); una richiesta attiva per slot, un nuovo load nello stesso slot sostituisce/annulla la precedente | Equivalente del `cleanup_thread`; nessun widget toccato dal thread |
| Probing fallito ⇒ si carica comunque con info vuote | Stesso comportamento: `log_error` + info di default (`fps=0`), poi sarà il bus della pipeline a segnalare un eventuale errore di playback | Da confermare in M2.4: se il discoverer fallisce il file spesso non è riproducibile, ma si mantiene la semantica originale |
| `duration` e `codec` estratti ma inutilizzati | Si estraggono comunque (la durata del discoverer è affidabile) ma non sono prerequisiti | Nessun comportamento da replicare |
| `width/height` codificati, niente PAR/rotazione | Stessi valori (`GstDiscovererVideoInfo`), niente correzione PAR/rotazione nel porting 1:1 | **Rischio noto**: il widget Qt poteva applicare orientazione/PAR in modo proprio; `playbin3` + `gtk4paintablesink` potrebbe comportarsi diversamente su video ruotati o con pixel non quadrati. Da verificare in M2.4 prima di dare per buono l'aspect ratio della finestra |

### Esito di M2.2: confronto con `ffprobe` su file reali

`core/discoverer.c` (vedi `tests/test_discoverer.c`) confrontato con `ffprobe` su file non di test:

| File | ffprobe (`r_frame_rate` / `avg_frame_rate`) | SyncView (`fps`) | Dimensioni, durata, codec |
|---|---|---|---|
| MP4 AV1 a frame rate variabile #1 | 359/12 = 29.92 / 27.85 | **29.97** (stima) | identici (1280×720, 79909 ms, av1) |
| MP4 AV1 a frame rate variabile #2 | 30000/1001 = 29.97 / 28.63 | **30.00** (stima) | identici (1280×720, 56127 ms, av1) |
| MKV AV1 a frame rate costante | 30/1 | 30.00 | identici (320×240; durata 4000 ms, ffprobe `N/A`) |

**Scoperta: `GstDiscoverer` non riporta un framerate per i file a frame rate variabile.** Sui due MP4 sopra le caps non contengono `framerate` e `gst_discoverer_video_info_get_framerate_*` dà `0/1` (nessun tag alternativo). Usare solo quel valore avrebbe dato `fps = 0` ("AUTO FPS") e **disabilitato `playback_rate`** per i file VFR — molto comuni (MP4 da telefono) — mentre l'originale otteneva un valore da `r_frame_rate`. Deviazione adottata (`discoverer_estimate_fps`): se il framerate dichiarato è 0/1 si stima il frame rate **nominale** dai timestamp dei primi ~150 frame (pipeline `filesrc ! parsebin` senza decodifica, al massimo 3 s; media degli intervalli entro ±25% della mediana, agganciata al valore standard più vicino entro l'1%: 23.976/24/25/29.97/30/48/50/59.94/60/120).

Dettagli emersi e scelte:
- Il flusso va letto **all'uscita del demuxer**, non dopo i parser: `av1parse` elimina timestamp/durate da gran parte dei buffer (prima versione: stima sbagliata di un fattore 4). Si usa il segnale `autoplug-continue` di `parsebin` per fermare l'autoplug sul flusso video elementare (caps video con `width`).
- Matroska/WebM ha timestamp al millisecondo: a 29.97 fps gli intervalli alternano 33/34 ms. La sola mediana dava 30.3 fps; la media degli intervalli vicini alla mediana recupera 29.97.
- Il valore stimato può differire di ≤0,3% da `r_frame_rate` (29.97 vs 29.92 sul primo file): è il frame rate *nominale* dei campioni, non quello "di base" di ffprobe. Effetto su `playback_rate = detected_fps/target_fps`: trascurabile.
- La stima costa una seconda apertura del file solo per i file senza framerate dichiarato; i file a frame rate costante non la pagano.
- `duration`: il discoverer la fornisce anche dove ffprobe dà `N/A` (MKV); come nell'originale nessun codice ne dipende.
- Il timeout di `gst_discoverer_new` accetta solo 1–3600 s (fuori range: `CRITICAL` di GObject): `discoverer_probe_file` limita il valore e i test rendono fatali i `CRITICAL`.

### Messaggi di log da mantenere (testo originale, `Feed-N` = `video_index + 1`)

`Caricamento asincrono avviato` · `Percorso video salvato` · `Info video ricevute` (`FPS: {:.2f}, Size: WxH`) · `Video caricato (async)` · errori `Errore ffprobe per <nome>` → nel porting `Errore probing <nome>` (il tool non è più ffprobe) · `Errore caricamento asincrono Feed-N`.

---

## Ottimizzazioni approvate rispetto all'originale

La riscrittura mira a replicare **e ottimizzare**. Le ottimizzazioni O1–O9 sono elencate in [PLAN.md](../PLAN.md#ottimizzazioni-approvate-rispetto-alloriginale) con la milestone che le implementa. Questa sezione raccoglierà, man mano che vengono implementate, il comportamento dell'originale, quello nuovo e le **misure** (non assunzioni) dove l'esito dipende da dati/piattaforma:

| ID | Originale | SyncView-C | Misure / stato |
|---|---|---|---|
| O1 | Step fisso in ms (40/33/100/200), nessun vero avanzamento di un frame | Preset ms invariati + "Frame esatto" (`GST_EVENT_STEP` / seek accurato) | da fare (M2.7) |
| O2 | Probing fallito → carica comunque, nessun messaggio | Errori specifici (plugin mancante col nome del codec, timeout, file corrotto) | da fare (M2.2) |
| O3 | Path salvato in `user_paths` prima del probing | Salvato solo a caricamento riuscito | da fare (M2.8) |
| O4 | Seek+play in sequenza sui player (avvio sfalsato) | Clock/base-time condivisi, avvio simultaneo; drift-correction facoltativa | da misurare (M3.10) |
| O5 | Timer di polling della posizione | Tick callback del frame clock, attivo solo in `PLAYING` | da fare (M2.6) |
| O6 | Decodifica delegata a QtMultimedia, non osservabile | Decoder in uso loggato, plugin hw rilevati dalla verifica dipendenze | da misurare (M2.4, M2.10) |
| O7 | `save_incremental` (id modificati) + journal SQLite di default | Id sporchi + `PRAGMA journal_mode=WAL` | da fare (M4.12) |
| O8 | Export sempre con ricodifica | Opzione "Rapido" (copia stream, taglio a keyframe); "Preciso" resta default | da misurare (M6.12) |
| O9 | `max_workers = cpu − 1` per qualunque encoder | Limite più basso per encoder hardware | da fare (M6.6) |

Aggiunta non legata a una singola ottimizzazione: **verifica e download delle dipendenze al primo avvio** (M2.10–M2.12), assente nell'originale (che richiedeva `pip install` e `ffmpeg` nel PATH).
