# Prove da fare su Windows

La CI di GitHub su Windows (MSYS2/UCRT64, con `gst-plugins-rs`) compila ed esegue la suite, **compresi `video_player` e `main_window`** con video sintetici; passano. Non ha una GPU né uno schermo reale e non può verificare l'installazione vera, i decoder hardware, file reali e l'aspetto. Salta da sola `deps_dialog` (il test usa uno script `sh` finto come `pkexec`: la finestra va provata a mano). Questo file elenca cosa va provato **a mano su un PC Windows reale** (con schermo). Si aggiorna a ogni milestone; a fine M2 va consegnato a chi ha il PC. Il file gemello per Mac è [TEST_MACOS.md](TEST_MACOS.md).

**Stato (10 ottobre 2026):** due sessioni su un PC Windows 11 reale hanno fatto la preparazione (sezione 0), i test automatici (sezione 1), `--check-deps`, le prove con file reali in vari codec e la caccia allo stallo di `gtk4paintablesink`; gli esiti e le correzioni sono in [RISULTATI_TEST_WINDOWS.md](RISULTATI_TEST_WINDOWS.md). **Lo stallo è risolto** (causa: il passaggio `NULL → READY` del sink eseguito in un thread di `decodebin3`; correzione: portare il sink a `READY` dal thread principale prima di ogni caricamento, vedi «Stallo del sink» nella sezione 1). **Non** sono state fatte le prove manuali della sezione 2 (le voci già coperte sono segnate sotto). Resta aperto: i 100 fps sul video interlacciato (voce «DA RIPROVARE»; manca una clip interlacciata sul PC di prova).

Chi prova: segni ogni voce con ✅ / ❌ e, per ogni ❌, allega l'output richiesto (vedi «Cosa raccogliere»).

## Riepilogo: cosa di M2 NON è confermato su Windows

| Milestone | Cosa è stato verificato in CI (Windows) | Cosa resta da confermare su un PC vero |
|---|---|---|
| M1 (core: sync, marker, SQLite, percorsi, logger) | test in CI | percorsi con **backslash, accenti e spazi** (`C:\Users\Marco Rossi\Video\è.mp4`), file dei percorsi e log in `%USERPROFILE%\.syncview\` |
| M2.2 `discoverer` | test in CI | file reali (vedi M2.7) |
| M2.4–M2.7 player: carica, play, seek, passo per frame, velocità | test in CI con video sintetici VP8 (`video_player` passa) | su un PC vero: **decoder hardware** (D3D11/NVIDIA/Intel): quale decoder usa davvero (riga `[GST] decoder video in uso` nel log); audio; scorrevolezza |
| M2.7 su **file reali** | su **Linux** 69 clip (H.264 30 e 60 fps, H.265, AV1, VP9, VP8; MP4, WebM, MKV, MOV) con decoder hardware NVIDIA/VA: passo ±1 frame, seek e 2× tutti esatti. Su Windows solo sintetici | gli stessi controlli con file reali **su Windows** (`test_real_clips`, vedi più sotto) e con file da telefono/action cam, anche a framerate variabile (mai provato) |
| M2.6 posizione col frame clock | eseguito in CI (le verifiche sul frame clock si saltano da sole se il clock non batte) | il tempo avanza fluido; da fermo nessun risveglio periodico |
| M2.8 finestra minima | `main_window` eseguito in CI (passa) | aspetto vero; il video si carica nella finestra con scheda video reale; dialogo «Apri» **nativo di Windows** (i filtri sono per estensione proprio per questo); tema chiaro/scuro che segue Windows; barra del titolo e pulsanti della finestra; scala dello schermo 125%/150%; chiusura pulita (nessun `syncview.exe` rimasto) |
| M2.9 finestre di debug | test in CI (`debug_windows` OK) | aspetto, Pausa/Copia/filtri, interruttori dei moduli, chiusura insieme alla principale |
| M2.10 verifica dipendenze | test con sonde simulate | cosa trova davvero su Windows; il nome del pacchetto MSYS2 della tabella (`mingw-w64-ucrt-x86_64-gst-plugins-rs`) e il comportamento con l'installer ufficiale |
| M2.11 installazione delle dipendenze | download con SHA-256, ZIP, piani e consenso verificati con server locale e funzioni finte; **il lancio vero dell'installer non è mai stato eseguito** | `gst-plugins-rs` nella CI di Windows ora c'è; sul PC vero: l'app scarica l'installer ufficiale (527 MB) e lo lancia con UAC; rifiutando UAC non cambia nulla; dopo l'installazione l'app trova i plugin (il registro/percorso di GStreamer) |
| M2.12 interfaccia del primo avvio | `deps_dialog`, `deps_state`, menu e riapertura dopo un plugin mancante in test su Linux (installatore finto) | **tutto sul sistema reale**: la finestra «Preparazione di SyncView» (aspetto, tema, scala dello schermo), l'installazione vera con l'avviso sulla UAC, «Continua senza» ricordato dopo il riavvio, Menu → «Verifica dipendenze…» |

**Priorità** (se il tempo di chi prova è poco): 1) `gtk4paintablesink` esiste? 2) `main_window` e `video_player` passano? 3) un video reale si apre, scorre e si avanza di un frame? 4) il dialogo «Apri» è quello di Windows? 5) il tema segue Windows?

## 0. Preparazione

Come in CI: shell **MSYS2 UCRT64** (non PowerShell, non MINGW64).

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-meson mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-gtk4 mingw-w64-ucrt-x86_64-gstreamer mingw-w64-ucrt-x86_64-gst-plugins-base \
  mingw-w64-ucrt-x86_64-gst-plugins-good mingw-w64-ucrt-x86_64-gst-plugins-ugly mingw-w64-ucrt-x86_64-gst-plugins-bad mingw-w64-ucrt-x86_64-gst-libav \
  mingw-w64-ucrt-x86_64-gst-plugins-rs mingw-w64-ucrt-x86_64-sqlite3 mingw-w64-ucrt-x86_64-json-glib \
  mingw-w64-ucrt-x86_64-libsoup3
git clone <repository> && cd SyncView && git checkout SyncView-C
meson setup build && meson compile -C build
```

- **Controlla i plugin** (il primo ostacolo; se `gtk4paintablesink` manca, fermati e scrivilo):

```bash
for e in gtk4paintablesink playbin3 videotestsrc videoconvert vp8enc webmmux filesink; do gst-inspect-1.0 $e >/dev/null 2>&1 && echo "OK  $e" || echo "MANCA  $e"; done
```

- Versione di Windows (`winver`), scheda video e driver (Gestione dispositivi → Schede video).
- **Smart App Control**: se è attivo (Sicurezza di Windows → Controllo app e browser) blocca i DLL e gli eseguibili non firmati di MSYS2 (`meson`, `gst-inspect-1.0`, i test compilati) con «An Application Control policy has blocked this file». Nella prima sessione è stato disattivato, e non si può riattivare senza reimpostare Windows: usa un PC o una VM di prova.
- `pacman -Syu` va lanciato **due volte** (la prima chiude il terminale). `gdb` fa parte della toolchain e serve per i blocchi (`gdb -p <pid>`, poi `thread apply all bt`).

## 1. Test automatici, con un display vero (non in CI)

```bash
meson test -C build --print-errorlogs
```

- [ ] Esito atteso: **22 test: tutti OK tranne due `SKIP` previsti** (dopo le correzioni del 5 ottobre: 20 OK, 2 SKIP, 0 FAIL; prima: `main_window` FAIL e `video_player` TIMEOUT) — `real_clips` (se non gli dai `SYNCVIEW_TEST_CLIPS`, vedi sotto) e `deps_dialog` (salta su Windows per costruzione). Nessun `SKIP` su `video_player` e `main_window`: se ci sono, manca `gtk4paintablesink` (vedi sopra).
- [ ] **`main_window`** e **`video_player`** sono i test più importanti: in CI passano, qui li provi con uno schermo e una GPU veri. Se falliscono, allega il log (righe `attesa dello stato … scaduta`, messaggi `[GST]`, asserzioni).
- [ ] Ripeti `meson test -C build video_player main_window` **almeno 10 volte** (`meson test -C build video_player main_window --repeat 10`): nessun test intermittente. Prima della correzione dello stallo (vedi sotto) falliva circa 1 esecuzione su 3. Dopo la correzione, il 10 ottobre: `main_window` 10 su 10 OK; `video_player` 7 OK e 3 FAIL per un'altra gara, rara, nell'asserzione dell'ultimo frame (`last_end >= duration_ns - 2 ms`, `test_video_player.c:2222`, non è lo stallo; dettagli in RISULTATI_TEST_WINDOWS.md): se la vedi, annotala e allega il log. Se ricompare un fallimento allega `SYNCVIEW_DEBUG=1` e la riga `smontaggio abbandonato`.
- [ ] **Riproduzione dello stallo** (ora è un controllo): `SYNCVIEW_TEST_ONLY=dispose_while_loading SYNCVIEW_DEBUG=1 build/tests/test_video_player.exe`. Atteso: esce con 0 e **nessuna** riga `smontaggio abbandonato` (il test lo verifica con `syncview_video_player_abandoned_teardowns()` e fallisce se una pipeline resta bloccata). Prima della correzione 4–5 caricamenti su 10 restavano in `playsink READY -> PAUSED`.
- [ ] **Lo stesso scenario con file e decoder veri**: `SYNCVIEW_TEST_ONLY=dispose_while_loading SYNCVIEW_TEST_DISPOSE_FILE=<file> build/tests/test_video_player.exe` con un MP4/H.264, un H.265, un VP9, un AV1 e un VP8 (anche file grandi, 720p/1080p). Atteso: nessuna riga `smontaggio abbandonato` per nessun codec. Il 10 ottobre su Windows (clip 720p, 4 esecuzioni da 10 caricamenti per codec) c'erano 0 abbandoni su 200 caricamenti; senza la correzione ne uscivano fino a 16 su 40.

### Stallo del sink: cosa è, come è stato corretto, come verificarlo

- **Sintomo:** un caricamento non finisce mai (il test scade con `attesa dello stato … scaduta` o `spin_until(&ev.got_loaded, 10000)`), oppure all'uscita compare `smontaggio abbandonato: la pipeline non si è assestata entro 10 s`. Capitava con ogni codec (H.264, H.265, VP8, VP9, AV1), con decoder hardware e software, e con OpenGL attivo o disattivato.
- **Causa** (trovata il 10 ottobre con nomi univoci per pipeline, `GST_DEBUG` e `gdb`): `playsink` porta il sink video da `NULL` a `READY` in un thread di `decodebin3` (quello che aggiunge il pad `video_0`). A volte `gtk4paintablesink` (0.15.3, gst-plugins-rs 1.28.7 di MSYS2) resta bloccato in quel passaggio, in `WaitOnAddress` dentro `libgstgtk4.dll`, finché il processo non esce. Il thread principale intanto gira regolarmente: non è un deadlock con il main loop. Nelle pipeline sane il passaggio dura ~0,1 ms; in quelle bloccate non finisce, il sink non riceve mai il primo buffer e `ASYNC_DONE` non arriva.
- **Correzione:** `start_pending_load()` in `src/video/video_player.c` porta il sink a `READY` **dal thread principale** prima di mettere la pipeline in PAUSED; il passaggio di `playsink` diventa un no-op. Effetto misurato: 0 caricamenti bloccati su 60 con la correzione, 25 su 60 senza.
- **Regressione:** `test_dispose_while_loading` ora fallisce (assert su `syncview_video_player_abandoned_teardowns()`) se una pipeline resta bloccata. Verificato togliendo la correzione: fallisce 3 volte su 3.
- **Da controllare su altri sistemi:** su Linux e macOS lo stallo non è mai stato osservato; la correzione vale anche lì, ma va confermato che il test passa ancora (CI Linux/macOS) e che il passaggio anticipato a `READY` non cambia il comportamento.
- **Segnalazione a monte:** resta valida per `gst-plugins-rs` (riproduzione: `dispose_while_loading` senza la riga di `start_pending_load`). Se il plugin verrà corretto, la riga resta innocua.

## 2. Prove manuali dell'app

Avvio dalla shell MSYS2: `build/src/syncview.exe` (con `--debug` per i log).

Già fatto il 5 ottobre (da PC reale): compilazione, `--check-deps`, avvio dell'app e finestra «Preparazione». Non fatto: tutto il resto di questa sezione.

- [ ] **Finestra vuota**: si apre, mostra «Nessun video» e «Carica video…». Aspetto come nei mockup della direzione 1b.
- [ ] **Tema**: con Windows in modalità **chiara** l'app è chiara, in **scura** è scura; cambiando la modalità di Windows ad app aperta il tema cambia da solo (prova anche `SYNCVIEW_THEME=light` / `dark`: in MSYS2 `SYNCVIEW_THEME=dark build/src/syncview.exe`).
- [ ] **Scala dello schermo**: a 100%, 125% e 150% testi e pulsanti restano nitidi e non si tagliano.
- [ ] **Due video in fretta**: Ctrl+O, scegli un video e **subito** Ctrl+O con un secondo (il primo è ancora in caricamento). Atteso: il secondo si carica e l'app non si blocca. Con `SYNCVIEW_DEBUG=1` **non** deve comparire `smontaggio abbandonato` (era lo stallo del sink, corretto il 10 ottobre; vedi «Stallo del sink» nella sezione 1). Se compare, allega la riga e quelle `[GST]` vicine.
- [ ] **Apri video** (Ctrl+O): si apre il dialogo **nativo di Windows** (non uno GTK generico, riconoscibile dall'aspetto) con i filtri per estensione; apri un `.mp4`, un `.mkv` e un file in una cartella con **spazi e accenti nel nome**: il video compare con chip «A · FEED-1», fps e tempo.
- [ ] **Riproduzione**: Play/Pausa dal pulsante e con Spazio; tempo e barra avanzano fluidi; audio presente se il file lo ha.
- [ ] **Passo per frame**: ←/→ e −1/+1 avanzano esattamente di un frame (40 ms a 25 fps); Shift+←/→ di **10 frame** (a 30 fps: 333 ms; a 60 fps: 167 ms). Il passaggio del mouse sui pulsanti −10/−1/+1/+10 mostra «N frame indietro/avanti».
- [ ] **Video interlacciato (DA RIPROVARE)**: nella prima sessione `1080i-25-H264.mkv` (immagini a campi, caps `framerate=50/1`, `coded-picture-structure=field`) dava 100 fps e passi di 10/30/5 ms. Ora il programma registra i caps e i primi buffer. Lancia `SYNCVIEW_DEBUG=1 SYNCVIEW_TEST_CLIPS=<cartella con la clip> build/tests/test_real_clips.exe 2> clips.log` e incolla le righe `sink: caps …` e `sink: buffer …` (servono a capire se è sbagliato il framerate o la durata dei buffer). Atteso a regime: 25 fps e passo di 40 ms.
- [x] **File reali, in automatico, per codec (10 ottobre)**: con clip sintetiche 1280×720, 25 fps, 12 s (H.264 MP4, H.265 MP4, VP8 WebM, VP9 WebM, AV1 MKV) `test_real_clips` dà `ESITO: OK` con i decoder hardware (`d3d12h264dec`, `d3d12h265dec`, `nvvp8dec`, `d3d12vp9dec`, `d3d12av1dec`) e con quelli software (`avdec_h264`, `avdec_h265`, `vp8dec`, `vp9dec`). Il test richiede clip **di almeno 9,6 s**: con clip più corte falliscono `durata` e i seek senza che ci sia un difetto. L'AV1 con `d3d12av1dec` prima della correzione dello stallo non si caricava (timeout): ora è OK. Una registrazione di gioco a framerate variabile (29,97 fps nominali, `Apex Legends …mp4`) **fallisce** il passo ±1 e ±10 (durate dei frame irregolari, da 0 a 76 ms): è il caso «framerate variabile» di cui sotto, ancora da gestire.
- [ ] **File reali, in automatico**: metti 3-4 clip (MP4/H.264, MOV, MKV, 25/30/50/60 fps; le sottocartelle vanno bene) in una cartella e lancia `SYNCVIEW_TEST_CLIPS=<cartella> build/tests/test_real_clips.exe`: stampa per ogni file fps, durata e decoder, e termina con `ESITO: OK` o i punti falliti (passo ±1, seek, 2×). Incolla l'output intero.
- [ ] **File reali** (importante, vale su ogni sistema): ripeti riproduzione, seek e passo per frame con un MP4/H.264 da telefono o action cam e, se hai, MKV/MOV, a 25/30/50/60 fps. Annota per ciascuno: estensione, codec (`gst-discoverer-1.0.exe <file>`), fps mostrati nel chip, e se il passo ±1 cambia davvero il frame (il tempo varia di 1/fps). Un video a framerate variabile (spesso da telefono) deve cadere sul passo di 40 ms: segnala se non succede.
- [ ] **Decoder in uso**: con `SYNCVIEW_DEBUG=1 build/src/syncview.exe` apri un MP4/H.264 e cerca nel terminale la riga `[GST] decoder video in uso`: dice se usa un decoder hardware (D3D11, NVIDIA, Intel) o software. Incollala nella risposta.
- [ ] **Seek**: trascinando la barra il video segue; Home/Fine vanno all'inizio e alla fine.
- [ ] **File rotto**: un file di testo rinominato `.mp4` → scheda di errore «Impossibile riprodurre il video»; riavviando l'app **non** ricarica quel file.
- [ ] **Ricarico all'avvio**: chiudi e riapri: ricarica l'ultimo video valido. Controlla il file `%USERPROFILE%\.syncview\user_paths.json` (percorso con backslash salvato e riletto correttamente).
- [ ] **Chiusura**: chiudi con la X della finestra: l'app esce in meno di un secondo e in **Gestione attività** non resta nessun `syncview.exe` né `gst-*`. (Ctrl+C e `kill -TERM` da shell sono previsti solo su Linux/macOS: su Windows non esiste ancora l'uscita da segnale: annota cosa succede con Ctrl+C.)
- [ ] **Video su altro disco o rete**: un file su un disco USB o su una cartella di rete (`\\server\cartella\video.mp4`) si apre e scorre.
- [ ] **Finestre di debug** (M2.9): `build/src/syncview.exe --debug` apre 3 finestre (principale, Log, Moduli); senza `--debug` solo la principale. Pausa/Riprendi, Svuota, Copia, filtri per modulo e livello funzionano; spegnere VIDEO e fare play/seek toglie le righe VIDEO dalla finestra **e** dal terminale (gli errori restano); chiudere la principale chiude anche le altre.
> **Attenzione all'installazione vera (Windows).** Con la variabile `SYNCVIEW_DEPS_FAKE_MISSING` anche un solo componente (anche opzionale come `gst-decoder-hevc`) fa preparare all'app il passo «Installa GStreamer con l'installer ufficiale»: premendo «Installa» scarica **527 MB** e lancia l'installer con UAC, che **installa GStreamer sul PC**. Fallo solo su un PC o una VM di prova. Per provare la finestra senza installare nulla basta **non premere «Installa»** (Annulla / Continua senza / Installa a mano) oppure **rifiutare la richiesta UAC** (prova utile: non deve cambiare nulla).

- [ ] **Chiusura della finestra «Preparazione» con la X (NUOVO, 6 ottobre)**: con un componente indispensabile mancante (per esempio `SYNCVIEW_DEPS_FAKE_MISSING=gst-gtk4sink`) la X della barra del titolo, Alt+F4 ed Esc **non chiudono più** la finestra: compare «Scegli un'opzione: Annulla o Continua senza». «Annulla», «Continua senza» e «Chiudi» chiudono come prima. Prova anche dalla schermata «Installa a mano». Con soli componenti opzionali mancanti (per esempio `gst-decoder-hevc`) la X chiude. Prova anche la finestra che compare dopo aver aperto un file con un decoder mancante (`SYNCVIEW_DEPS_SHOW_AFTER_ERROR`).
- [ ] **Formato WMV senza demuxer (NUOVO)**: senza `asfdemux` (su MSYS2 manca finché non installi `mingw-w64-ucrt-x86_64-gst-plugins-ugly`) la finestra elenca «wmv» tra i formati senza demuxer, ma **come opzionale** (titolo «Alcuni componenti opzionali non sono installati») e non dice più «nessun video»; `--check-deps` non scrive più «Riproduzione: NON possibile» per il solo WMV. Con `gst-plugins-ugly` installato il componente diventa verde. Il comando manuale e il piano devono nominare `gst-plugins-ugly`.
- [ ] **Finestra «Preparazione di SyncView» (M2.12)**: avvia l'app con `SYNCVIEW_DEPS_FAKE_MISSING=gst-decoder-hevc,ffmpeg` (variabile d'ambiente; `all` per simulare tutto): all'avvio compare la finestra con le righe dei componenti, l'**elenco esatto** di ciò che verrà installato (URL, dimensione, SHA-256 dove c'è un download) e l'avviso sulla UAC. Controlla: aspetto leggibile in tema chiaro e scuro; **nulla parte finché non premi «Installa»**; «Annulla» chiude senza cambiare nulla; «Continua senza» e riavvio con la stessa variabile → **nessuna finestra**; Menu → «Verifica dipendenze…» → la finestra compare sempre (con tutto a posto dice «Tutte le dipendenze sono a posto»). Con la variabile e «Installa» su una macchina di prova: compare la richiesta di autorizzazione di Windows; **rifiutandola** la finestra dice che non è stato modificato nulla e **non riprova da sola**; dopo un'installazione vera il ricontrollo finale mostra le righe verdi (o dice che serve riavviare SyncView). Allega una schermata di ogni passaggio. Per ripetere la prova da zero cancella il file dello stato delle dipendenze (`~/.syncview/deps_state.json`, cioè `%USERPROFILE%\.syncview\deps_state.json`).
- [ ] **Controllo dipendenze**: `build/src/syncview.exe --check-deps` stampa il report e non si blocca. Incolla l'output intero.

- [ ] **Dopo un'installazione vera (solo su PC/VM di prova)**: dopo che l'installer di GStreamer ha finito, `build/src/syncview.exe --check-deps` trova i plugin (registro e percorso di GStreamer) e la finestra «Preparazione» mostra le righe verdi o dice di riavviare SyncView. Incolla l'output.

## 3. Cosa raccogliere se qualcosa fallisce

In MSYS2:

```bash
SYNCVIEW_DEBUG=1 GST_DEBUG=3 build/tests/test_main_window.exe 2> main_window.log   # per un test che fallisce
SYNCVIEW_DEBUG=1 build/src/syncview.exe 2> app.log                                  # per l'app
gst-inspect-1.0 gtk4paintablesink > sink.txt 2>&1
```

Allega i file, `winver`, la scheda video, il file `%USERPROFILE%\.syncview\syncview_log.txt` e, per la finestra delle dipendenze, `%USERPROFILE%\.syncview\deps_state.json` con una schermata. Per un blocco in caricamento aggiungi `GST_DEBUG=gtk4paintablesink:6,GST_STATES:4`.

## Da aggiungere nelle prossime milestone

- (M2.11/M2.12 fatte: vedi le prove sopra) installazione delle dipendenze con l'installer ufficiale di GStreamer (richiesta UAC gestita da Windows) e confronto con MSYS2.
- M3: più video, sincronizzazione, zoom/pan, drag&drop di file da Esplora risorse.
- Uscita ordinata da segnale/Ctrl+C su Windows (oggi solo Unix).
- Dopo la correzione dello stallo del sink (sink portato a `READY` dal thread principale, 10 ottobre): su un secondo PC Windows (altra GPU, per esempio solo Intel o solo AMD) rieseguire la ripetizione 10×, la riproduzione con `SYNCVIEW_TEST_DISPOSE_FILE` e la prova «Due video in fretta». Rimane da verificare su Linux e macOS che la correzione non cambi nulla.
- Video a **framerate variabile** (registrazioni di gioco, telefoni): il passo ±1/±10 frame non è esatto; decidere come comportarsi (per esempio passo basato sul frame reale).
