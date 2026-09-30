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

### Messaggi di log da mantenere (testo originale, `Feed-N` = `video_index + 1`)

`Caricamento asincrono avviato` · `Percorso video salvato` · `Info video ricevute` (`FPS: {:.2f}, Size: WxH`) · `Video caricato (async)` · errori `Errore ffprobe per <nome>` → nel porting `Errore probing <nome>` (il tool non è più ffprobe) · `Errore caricamento asincrono Feed-N`.
