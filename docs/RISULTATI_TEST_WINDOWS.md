# Risultati delle prove su Windows (5 ottobre 2026)

Esito di una sessione di prove fatta seguendo [TEST_WINDOWS.md](TEST_WINDOWS.md) su un PC Windows reale. Coprono la preparazione (sezione 0), i test automatici (sezione 1) e `--check-deps`. Le prove manuali con interfaccia (sezione 2) **non** sono state fatte.

## Ambiente

- Windows 11 Home 10.0.26200.
- Schede video: NVIDIA GeForce RTX 4060 Laptop e Intel UHD Graphics.
- MSYS2 UCRT64 installato con `winget install MSYS2.MSYS2`; pacchetti della sezione 0 di `TEST_WINDOWS.md`; GStreamer 1.28.7.
- Su questo PC era attivo **Smart App Control** (`VerifiedAndReputablePolicyState = 1`): blocca i DLL e gli eseguibili non firmati di MSYS2 (`meson`, `gst-inspect-1.0`, i test compilati). Per proseguire è stato disattivato (operazione non reversibile senza reimpostare Windows). Chi prova su un PC con SAC attivo vedrà `An Application Control policy has blocked this file`.

## Cosa è stato fatto

1. Installazione di MSYS2 e dei pacchetti (`pacman -Syu` va ripetuto due volte: la prima chiude il terminale).
2. Controllo dei plugin: `gtk4paintablesink`, `playbin3`, `videotestsrc`, `videoconvert`, `vp8enc`, `webmmux`, `filesink` → tutti OK.
3. `meson setup build && meson compile -C build` → compilazione senza errori.
4. `meson test -C build --print-errorlogs`.
5. Prove mirate su `video_player` e `main_window` (con `SYNCVIEW_DEBUG=1`, e con i decoder NVIDIA a rank 0 tramite `GST_PLUGIN_FEATURE_RANK`).
6. `build/src/syncview.exe --check-deps`.

## Risultati

### `meson test`: 18 OK, 2 SKIP, 1 FAIL, 1 TIMEOUT (22 test)

| Test | Esito |
|---|---|
| `real_clips`, `deps_dialog` | SKIP (previsti) |
| `main_window` | **FAIL** (exit `0xc0000409`) |
| `video_player` | **TIMEOUT** dopo 240 s |
| tutti gli altri 18 | OK |

- **`main_window`**: dopo aver caricato `o3.webm` e provato un file rotto, il caricamento di `o3-second.webm` resta fermo su «Analisi del file…»; il test scade con `attesa dello stato 2 scaduta` (assert in `tests/test_main_window.c`, riga 333). Decoder in uso: `nvvp8dec` (hardware NVIDIA).
- **`video_player`**: i test passano fino a `short.webm`; poi nel log compare `smontaggio forzato: la pipeline non si è assestata entro 10 s` e il sink continua a segnalare `Have too many pending frames`. Il processo non termina.
- Riproducibile: visto in due esecuzioni complete e in una di `video_player` con debug. Non ripetuto 3 volte come chiede il documento.
- Con i decoder NVIDIA disattivati: `video_player` si blocca allo stesso punto (quindi non è solo il decoder hardware); `main_window` fallisce alla riga 529 (la finestra delle dipendenze non compare dopo l'errore «Manca un decoder»). Quest'ultimo caso può dipendere dalla variabile di rank impostata per la prova.

### `--check-deps`

Non si blocca. Riporta «Riproduzione: NON possibile» perché manca il demuxer `wmv`, mentre mp4/mov, avi, mkv, flv ci sono. Trovati decoder H.264/H.265/VP9/AV1 e hardware (NVIDIA, D3D11, D3D12, Intel QSV) e ffmpeg 9.0.2. Possibile difetto della tabella: un solo formato mancante rende «non possibile» tutta la riproduzione.

## Non fatto

Prove manuali della sezione 2 (dialogo «Apri» nativo, tema, scala dello schermo, riproduzione e passo per frame, chiusura, finestre di debug, finestra «Preparazione»), `test_real_clips` (nessuna clip reale) e installazione vera con l'installer ufficiale di GStreamer.

## Indagine sulle cause

Strumenti usati: log di SyncView (`SYNCVIEW_DEBUG=1`) e `gdb -p <pid>` sul test bloccato (`gdb` fa parte della toolchain MSYS2 UCRT64): `thread apply all bt`.

### 1. `video_player` bloccato: stallo dentro `gtk4paintablesink`

- Il test `test_dispose_while_loading` distrugge dei player mentre la pipeline sale da READY a PAUSED. `teardown_pipeline` aspetta che la transizione si assesti (lasciando girare il main loop) e poi porta a NULL.
- Su Windows qualche pipeline **non si assesta mai**: il log dice `smontaggio: playsink non si assesta (READY -> PAUSED)`. Su 11 smontaggi rimandati ne restava bloccato 1–2.
- Dopo il timeout di 10 s il codice chiamava comunque `gst_element_set_state(NULL)` **sul thread principale**: il thread principale restava fermo per sempre (gdb: `poll_until_settled` → `libgstplayback` → `RtlEnterCriticalSection`). Nello stesso momento 5 thread `pool-N` di playsink erano fermi in `WaitOnAddress` dentro `libgstgtk4.dll`: il sink aspetta qualcosa che non arriva. Il flusso incessante di `Have too many pending frames` nel log è un effetto di questo blocco.
- Non è colpa del decoder hardware: succede anche con i decoder NVIDIA a rank 0. Non ho trovato la causa dentro il sink (DLL Rust senza simboli) né segnalazioni già note.
- **Correzione** (`src/video/video_player.c`, `poll_until_settled`): dopo i 10 s la pipeline bloccata viene **abbandonata** (si chiama `done()` ma non `set_state(NULL)` né l'`unref`): meglio una pipeline persa che un'app congelata. Il log lo dice: `smontaggio abbandonato: …` preceduto dall'elenco degli elementi non assestati (`log_unsettled_elements`). Su Linux e macOS il ramo non scatta mai in pratica (lo smontaggio si assesta), quindi il comportamento lì non cambia.
- Effetto collaterale: una pipeline abbandonata tiene aperti i file del test, quindi su Windows il `rm -rf` finale della cartella temporanea può fallire. In `tests/test_video_player.c` è diventato un avviso (`#ifdef _WIN32`); altrove resta un'asserzione.

### 2. `main_window`: la finestra «Preparazione» non compariva dopo un decoder mancante

- Il test aspettava la finestra per 10 s, ma `on_missing_plugin_idle` non veniva mai eseguita: era agganciata con `g_idle_add` (priorità idle, 200) e su Windows le sorgenti a priorità più alta del main loop di GTK la tenevano sempre in coda.
- **Correzione** (`src/ui/main_window.c`): `g_idle_add_full(G_PRIORITY_DEFAULT, …)`. Aggiunta anche la riga di log `Plugin mancante nel player: avvio il controllo delle dipendenze`.

### 3. `video_player`: test troppo stretti sui tempi

- `test_ticker_only_while_playing` e `test_frame_clock_drives_the_ticks` aspettavano il primo aggiornamento di posizione per soli 300 ms; su Windows con il decoder hardware arriva dopo circa 470 ms. Ora aspettano il primo aggiornamento (con un limite di 5 e 10 s) e poi misurano, come già fa un altro test.

### Stato dopo le correzioni

- Due esecuzioni complete di `meson test`: 20 OK, 2 SKIP, 0 FAIL.
- Ripetendo solo `video_player` e `main_window` resta **un'intermittenza**: 2 esecuzioni su 6 con l'hardware (e 1 su 4 con i decoder NVIDIA a rank 0, quindi non è legata al decoder hardware) hanno fallito perché un normale caricamento non si completava in 10–15 s (`spin_until(&ev.got_loaded, 10000)` a riga 826 di `test_video_player.c`, `spin_until_state(…LOADED, 15000)` a riga 399 di `test_main_window.c`). È lo stesso stallo del sink descritto al punto 1, ma su una pipeline normale. Causa non ancora risolta; sospetto: gli stalli seguono i player distrutti durante il caricamento.

## Caccia allo stallo del sink (seconda sessione)

### Riproduzione minima

`SYNCVIEW_TEST_ONLY=dispose_while_loading SYNCVIEW_DEBUG=1 build/tests/test_video_player.exe` (nome del test: `test_dispose_while_loading`; 10 player distrutti subito dopo `load()`, con a volte 20 ms di attesa) lascia **1–3 pipeline su 10** bloccate in `playsink READY -> PAUSED` (4 esecuzioni su 4). Con `GST_DEBUG=gtk4paintablesink:7,playsink:6` il sink dopo la creazione non scrive più nulla: l'attesa avviene in un punto senza log.

### Cosa fanno i thread (gdb durante lo stallo, thread principale vivo)

- Il thread principale cicla normalmente nel main loop del test; i thread bloccati sono i `pool-N` di GStreamer (cambio di stato asincrono di `playsink`), fermi in `WaitOnAddress` dentro `libgstgtk4.dll`, chiamati da `change_state` del sink. Uno per ogni pipeline abbandonata.
- `WaitOnAddress` è usato sia da `std::sync::mpsc::recv` sia da `std::sync::Mutex`: senza simboli non si distingue.
- Il sorgente di `gtk4paintablesink` (gst-plugins-rs, `video/gtk4/src/sink/imp.rs` e `utils.rs`) in `NullToReady` chiama `invoke_on_main_thread` (due volte: `gdk::Display::default()` e, se serve, `gtk::init`), che fa `MainContext::default().invoke(...)` e aspetta il risultato con un canale. `g_main_context_invoke` **esegue la funzione direttamente nel thread chiamante se il contesto non è posseduto da nessuno in quel momento** (nei test il thread principale lo prende solo dentro `g_main_context_iteration`); altrimenti la mette in coda per il thread principale. La stessa funzione tiene `GL_CONTEXT` (mutex globale) durante `initialize_gl_context_main`.

### Esperimenti

- Se il thread principale tiene il contesto **sempre** (`g_main_context_acquire` all'inizio del test), `dispose_while_loading` blocca **10 pipeline su 10** (invece di 1–3). Il comportamento dipende quindi da quale thread esegue le funzioni del sink: l'intermittenza è una gara tra il thread del sink e il thread principale. (Con il contesto sempre posseduto fallisce anche qualunque test che blocca il thread principale, per esempio `gst_element_get_state(..., 10 s)` alla riga 637: non è un confronto pulito.) Un'app GTK reale, che vive dentro `g_application_run`, si comporta come quel caso.
- Priorità del main loop: sorgenti a priorità 0 e inferiori girano, quelle a priorità ≥ 1 (compresa `G_PRIORITY_DEFAULT_IDLE` = 200) **non vengono mai servite** durante il caricamento: qualcosa a priorità 0 è sempre pronto. È la causa del problema 2 sopra e vale da ricordare per ogni `g_idle_add` del progetto.
- Non dipende dal decoder: capita anche con i decoder NVIDIA a rank 0.

### Cosa non è stato accertato

Quale mutex/canale preciso resta in attesa dentro il sink (il DLL non ha simboli; per saperlo servirebbe una build di `gst-plugins-rs` con simboli o un log aggiuntivo nel sink). Non ho trovato segnalazioni identiche nel tracker di gst-plugins-rs.

### Ipotesi di lavoro e prossimi passi

1. La condizione «distruggere un player mentre `playsink` sta passando READY→PAUSED» è quella che scatena lo stallo; l'app reale la crea aprendo un secondo video mentre il primo è in caricamento. Verificarlo a mano nell'app (Ctrl+O due volte in fretta) e guardare nel log `smontaggio abbandonato`.
2. Possibile evitare la condizione: non distruggere mai una pipeline in READY→PAUSED ma **riutilizzare** il player (nuovo `uri` dopo un passaggio a NULL pulito), oppure ritardare l'apertura del nuovo video finché il caricamento in corso non finisce. Va valutato per Linux e macOS prima di toccarlo: oggi lì lo smontaggio rimandato si assesta sempre.
3. Segnalare il problema a gst-plugins-rs con la riproduzione minima qui sopra e il backtrace.

## Prova con file reali su Windows (`test_real_clips`)

`SYNCVIEW_TEST_CLIPS=C:/Users/2002m/Desktop/Video build/tests/test_real_clips.exe` su 5 clip (decoder hardware D3D12 per H.264):

| Clip | fps | Decoder | Esito |
|---|---|---|---|
| `Big_Buck_Bunny_1080_10s_30MB.mp4` | 30 | d3d12h264dec (hw) | OK |
| `FPS_test_1080p50_L4.2.mkv` | 50 | d3d12h264dec (hw) | OK |
| `sample-1.mov` | 25 | avdec_mpeg4 (sw) | OK |
| `sample-3.mp4` | 25 | d3d12h264dec (hw) | OK |
| `1080i-25-H264.mkv` (interlacciato, immagini a campi) | **100 (sbagliato)** | d3d12h264dec (hw) | **FALLITO** (10 verifiche) |

Per la clip interlacciata il file dichiara `framerate=50/1` con `coded-picture-structure=field` (25 fotogrammi al secondo, 50 campi) e il test misura 100 fps e passi di 10 ms, 30 ms e 5 ms: il passo ±1 frame non è esatto. È un problema **diverso dallo stallo** e riguarda i video interlacciati (da telecamera/broadcast): va guardato a parte (`discoverer`/`video_player`: framerate dei caps contro intervallo reale tra i frame).

## Difetto trovato provando l'app a mano: il piano di installazione per Windows non risolve il demuxer WMV

Avviando `build/src/syncview.exe` da MSYS2 UCRT64 compare la finestra «Preparazione di SyncView» con «Demuxer dei formati supportati: Da installare». Premendo «Installa» finisce con «L'installazione è terminata ma mancano ancora: Demuxer dei formati supportati» e la riga diventa «Installazione fallita» (screenshot del 2026-10-05, 22:31–22:32).

**Causa** (verificata con `gst-inspect-1.0` e `pacman` nel MSYS2 di questo PC):
- Mancano `asfdemux`, `avdemux_asf` e `avdemux_flv`, quindi il formato **wmv** è senza demuxer. Gli altri (mp4/mov, avi, mkv, flv) sono presenti.
- `asfdemux` sta in `gst-plugins-ugly` (`mingw-w64-ucrt-x86_64-gst-plugins-ugly`, disponibile e non installato). `libav` qui non fornisce `avdemux_asf`.
- Il controllo in `src/core/deps_check.c` (`CONTAINER_RULES`, `check_demuxers`) lo sa (commento «asfdemux è in plugins-ugly»), ma il piano di installazione usa solo `PKG_GOOD` e `PKG_LIBAV`: non contiene mai «ugly». L'installazione quindi non può aggiungere il demuxer e l'esito è sempre «fallita».

**Cosa vede l'utente di sbagliato**
1. L'installazione non può riuscire: il pulsante «Installa» non porta a nulla.
2. Il comando manuale per Windows/MSYS2 (`pacman -S ... base good bad libav rs`) non include `gst-plugins-ugly`: seguendolo il problema resta.
3. I comandi manuali per Debian/Arch/Fedora/openSUSE non nominano «ugly» (`gstreamer1.0-plugins-ugly`, `gst-plugins-ugly`, `gstreamer1-plugins-ugly`): lo stesso difetto vale sugli altri sistemi se il demuxer WMV manca (non verificato qui).
4. Il testo «Senza questi componenti SyncView non può riprodurre nessun video» è falso: manca solo WMV e mp4/mkv/mov funzionano. Il demuxer è classificato `DEPS_FEATURE_PLAYBACK` come se bloccasse tutta la riproduzione (già annotato nella lista dei problemi aperti).
5. Il titolo dice «Manca un componente per leggere i video» anche se manca un solo formato poco usato.

**Da fare** (non ancora fatto, da verificare su Linux e macOS prima di toccare il codice):
- aggiungere un insieme di pacchetti «ugly» al piano per `asfdemux` (e valutare `avdemux_flv` in libav);
- aggiornare i comandi manuali di tutti i sistemi;
- non trattare un formato mancante come «riproduzione impossibile» se gli altri formati funzionano.

Soluzione provvisoria su questo PC: `pacman -S mingw-w64-ucrt-x86_64-gst-plugins-ugly` e riavviare SyncView, oppure «Continua senza».

## Difetto di comportamento (non solo Windows): la finestra «Preparazione di SyncView» si chiude con la X

Osservato a mano su Windows (2026-10-05): la finestra che avvisa dei componenti mancanti si può chiudere con la **X** della barra del titolo (negli screenshot la X è visibile). Per il funzionamento corretto dell'app non dovrebbe essere possibile: l'utente deve scegliere esplicitamente tra «Installa», «Installa a mano», «Annulla» e «Continua senza».

**Cosa dice il codice** (`src/ui/deps_dialog.c`, solo lettura, nessuna modifica):
- `on_close_request` (collegato a `close-request`, riga ~1404) blocca la chiusura **solo mentre un'installazione è in corso**: in quel caso chiede l'annullamento e la finestra resta. In ogni altro stato restituisce `FALSE` e la finestra si chiude.
- Il codice non è specifico di Windows: la X e i gesti equivalenti (Alt+F4, Esc se previsto, chiusura dal gestore finestre) si comportano allo stesso modo su Linux e macOS, quindi il difetto vale su tutte le piattaforme, anche se qui è stato visto solo su Windows.

**Cosa non è stato verificato**
- Cosa succede dopo la chiusura con la X: quale esito (`outcome`) riceve il chiamante tramite `done`, se l'app si avvia lo stesso senza componenti, se riappare al prossimo avvio. Va provato a mano prima di decidere la correzione.
- Se anche la finestra aperta dopo un plugin mancante (`SYNCVIEW_DEPS_SHOW_AFTER_ERROR`) abbia lo stesso comportamento.

**Da fare** (non fatto, da valutare su Linux e macOS): decidere se la X deve essere disattivata (`gtk_window_set_deletable(FALSE)`) o se deve equivalere a una scelta esplicita (per esempio «Continua senza» o «Annulla»), e bloccare Esc/Alt+F4 allo stesso modo. Dove la X viene nascosta (alcuni gestori finestre Linux la mostrano comunque) serve comunque un `close-request` che non chiuda in silenzio.

### Verifica a mano sulla chiusura con la X (2026-10-05, Windows)

Chiudendo con la X la finestra «Preparazione di SyncView» (componenti mancanti), **l'app resta utilizzabile normalmente**: la finestra principale risponde ai comandi come se i componenti ci fossero. Quindi la X equivale in pratica a «Continua senza», senza nessun avviso e senza una scelta esplicita. Questo risponde alla prima domanda lasciata aperta qui sopra (l'app si avvia lo stesso senza componenti).

Conseguenza: con un componente davvero indispensabile (per esempio il decoder H.264) l'utente può arrivare ad aprire un video che non si può riprodurre, e l'errore compare solo allora. Resta da controllare se la finestra riappare al prossimo avvio e se dopo la X il pulsante/menu «Verifica dipendenze» la riapre.
