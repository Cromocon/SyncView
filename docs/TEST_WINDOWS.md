# Prove da fare su Windows

La CI di GitHub su Windows (MSYS2/UCRT64, con `gst-plugins-rs`) compila ed esegue la suite, **compresi `video_player` e `main_window`** con video sintetici; passano. Non ha una GPU né uno schermo reale e non può verificare l'installazione vera, i decoder hardware, file reali e l'aspetto. Salta da sola `deps_dialog` (il test usa uno script `sh` finto come `pkexec`: la finestra va provata a mano). Questo file elenca cosa va provato **a mano su un PC Windows reale** (con schermo). Si aggiorna a ogni milestone; a fine M2 va consegnato a chi ha il PC. Il file gemello per Mac è [TEST_MACOS.md](TEST_MACOS.md).

**Stato (6 ottobre 2026):** una prima sessione su un PC Windows 11 reale ha fatto la preparazione (sezione 0), i test automatici (sezione 1) e `--check-deps`; gli esiti e le correzioni fatte sono in [RISULTATI_TEST_WINDOWS.md](RISULTATI_TEST_WINDOWS.md). **Non** sono state fatte le prove manuali della sezione 2 (le voci già coperte sono segnate sotto). Restano aperti: lo stallo intermittente di `gtk4paintablesink` e i 100 fps sul video interlacciato (voci segnate «DA RIPROVARE»).

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
- [ ] Ripeti `meson test -C build video_player main_window` **almeno 10 volte** (`meson test -C build video_player main_window --repeat 10`): nessun test intermittente. **DA RIPROVARE:** nella prima sessione 2 esecuzioni su 6 hanno fallito con un caricamento che non si completava in 10–15 s (stallo del sink, vedi RISULTATI_TEST_WINDOWS.md). Per ogni fallimento allega `SYNCVIEW_DEBUG=1` e la riga `smontaggio abbandonato`.
- [ ] **Riproduzione minima dello stallo** (utile per segnalarlo a gst-plugins-rs): `SYNCVIEW_TEST_ONLY=dispose_while_loading SYNCVIEW_DEBUG=1 build/tests/test_video_player.exe`. Atteso: nessuna pipeline bloccata; nella prima sessione 1–3 su 10 restavano in `playsink READY -> PAUSED`.

## 2. Prove manuali dell'app

Avvio dalla shell MSYS2: `build/src/syncview.exe` (con `--debug` per i log).

Già fatto il 5 ottobre (da PC reale): compilazione, `--check-deps`, avvio dell'app e finestra «Preparazione». Non fatto: tutto il resto di questa sezione.

- [ ] **Finestra vuota**: si apre, mostra «Nessun video» e «Carica video…». Aspetto come nei mockup della direzione 1b.
- [ ] **Tema**: con Windows in modalità **chiara** l'app è chiara, in **scura** è scura; cambiando la modalità di Windows ad app aperta il tema cambia da solo (prova anche `SYNCVIEW_THEME=light` / `dark`: in MSYS2 `SYNCVIEW_THEME=dark build/src/syncview.exe`).
- [ ] **Scala dello schermo**: a 100%, 125% e 150% testi e pulsanti restano nitidi e non si tagliano.
- [ ] **Due video in fretta**: Ctrl+O, scegli un video e **subito** Ctrl+O con un secondo (il primo è ancora in caricamento). Atteso: il secondo si carica e l'app non si blocca. Controlla nel log (`SYNCVIEW_DEBUG=1`) se compare `smontaggio abbandonato`: è lo stallo del sink (vedi RISULTATI_TEST_WINDOWS.md); allega la riga e quelle `[GST]` vicine.
- [ ] **Apri video** (Ctrl+O): si apre il dialogo **nativo di Windows** (non uno GTK generico, riconoscibile dall'aspetto) con i filtri per estensione; apri un `.mp4`, un `.mkv` e un file in una cartella con **spazi e accenti nel nome**: il video compare con chip «A · FEED-1», fps e tempo.
- [ ] **Riproduzione**: Play/Pausa dal pulsante e con Spazio; tempo e barra avanzano fluidi; audio presente se il file lo ha.
- [ ] **Passo per frame**: ←/→ e −1/+1 avanzano esattamente di un frame (40 ms a 25 fps); Shift+←/→ di **10 frame** (a 30 fps: 333 ms; a 60 fps: 167 ms). Il passaggio del mouse sui pulsanti −10/−1/+1/+10 mostra «N frame indietro/avanti».
- [ ] **Video interlacciato (DA RIPROVARE)**: nella prima sessione `1080i-25-H264.mkv` (immagini a campi, caps `framerate=50/1`, `coded-picture-structure=field`) dava 100 fps e passi di 10/30/5 ms. Ora il programma registra i caps e i primi buffer. Lancia `SYNCVIEW_DEBUG=1 SYNCVIEW_TEST_CLIPS=<cartella con la clip> build/tests/test_real_clips.exe 2> clips.log` e incolla le righe `sink: caps …` e `sink: buffer …` (servono a capire se è sbagliato il framerate o la durata dei buffer). Atteso a regime: 25 fps e passo di 40 ms.
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
- Dopo la correzione dello stallo del sink (riuso del player o apertura ritardata): rieseguire la ripetizione 10× e la prova «Due video in fretta».
