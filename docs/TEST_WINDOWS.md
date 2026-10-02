# Prove da fare su Windows

La CI di GitHub su Windows compila ed esegue la suite, ma **non può verificare il video**: nell'installazione della CI manca il pacchetto con `gtk4paintablesink` (`mingw-w64-ucrt-x86_64-gst-plugins-rs`), quindi `video_player` e `main_window` risultano `SKIP` (exit 77). Questo file elenca cosa va provato **a mano su un PC Windows reale** (con schermo). Si aggiorna a ogni milestone; a fine M2 va consegnato a chi ha il PC. Il file gemello per Mac è [TEST_MACOS.md](TEST_MACOS.md).

Chi prova: segni ogni voce con ✅ / ❌ e, per ogni ❌, allega l'output richiesto (vedi «Cosa raccogliere»).

## Riepilogo: cosa di M2 NON è confermato su Windows

| Milestone | Cosa è stato verificato in CI (Windows) | Cosa resta da confermare su un PC vero |
|---|---|---|
| M1 (core: sync, marker, SQLite, percorsi, logger) | test in CI | percorsi con **backslash, accenti e spazi** (`C:\Users\Marco Rossi\Video\è.mp4`), file dei percorsi e log in `%USERPROFILE%\.syncview\` |
| M2.2 `discoverer` | test in CI | file reali (vedi M2.7) |
| M2.4–M2.7 player: carica, play, seek, passo per frame, velocità | **mai eseguiti** (saltati: manca `gtk4paintablesink` nella CI) | **tutto**: caricamento, play/pausa/stop, posizione, seek, passo ±1 frame esatto, velocità; **decoder hardware** (D3D11/NVIDIA/Intel): quale decoder usa davvero (riga `[GST] decoder video in uso` nel log); audio |
| M2.7 su **file reali** | solo video sintetici VP8 (e solo su Linux/macOS) | MP4/H.264, MOV, MKV da telefono o action cam, 25/30/50/60 fps e framerate variabile: il passo ±1 frame è davvero esatto? Vale su ogni sistema |
| M2.6 posizione col frame clock | non eseguito | il tempo avanza fluido; da fermo nessun risveglio periodico |
| M2.8 finestra minima | `main_window` saltato | il video si carica nella finestra; dialogo «Apri» **nativo di Windows** (i filtri sono per estensione proprio per questo); tema chiaro/scuro che segue Windows; barra del titolo e pulsanti della finestra; scala dello schermo 125%/150%; chiusura pulita (nessun `syncview.exe` rimasto) |
| M2.9 finestre di debug | test in CI (`debug_windows` OK) | aspetto, Pausa/Copia/filtri, interruttori dei moduli, chiusura insieme alla principale |
| M2.10 verifica dipendenze | test con sonde simulate | cosa trova davvero su Windows; il nome del pacchetto MSYS2 della tabella (`mingw-w64-ucrt-x86_64-gst-plugins-rs`) e il comportamento con l'installer ufficiale |
| M2.11 installazione delle dipendenze | download con SHA-256, ZIP, piani e consenso verificati con server locale e funzioni finte; **il lancio vero dell'installer non è mai stato eseguito** | `gst-plugins-rs` nella CI di Windows ora c'è; sul PC vero: l'app scarica l'installer ufficiale (527 MB) e lo lancia con UAC; rifiutando UAC non cambia nulla; dopo l'installazione l'app trova i plugin (il registro/percorso di GStreamer) |
| M2.12 interfaccia del primo avvio | (da fare) | tutto |

**Priorità** (se il tempo di chi prova è poco): 1) `gtk4paintablesink` esiste? 2) `main_window` e `video_player` passano? 3) un video reale si apre, scorre e si avanza di un frame? 4) il dialogo «Apri» è quello di Windows? 5) il tema segue Windows?

## 0. Preparazione

Come in CI: shell **MSYS2 UCRT64** (non PowerShell, non MINGW64).

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-meson mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-gtk4 mingw-w64-ucrt-x86_64-gstreamer mingw-w64-ucrt-x86_64-gst-plugins-base \
  mingw-w64-ucrt-x86_64-gst-plugins-good mingw-w64-ucrt-x86_64-gst-plugins-bad mingw-w64-ucrt-x86_64-gst-libav \
  mingw-w64-ucrt-x86_64-gst-plugins-rs mingw-w64-ucrt-x86_64-sqlite3 mingw-w64-ucrt-x86_64-json-glib
git clone <repository> && cd SyncView && git checkout SyncView-C
meson setup build && meson compile -C build
```

- **Controlla i plugin** (il primo ostacolo; se `gtk4paintablesink` manca, fermati e scrivilo):

```bash
for e in gtk4paintablesink playbin3 videotestsrc videoconvert vp8enc webmmux filesink; do gst-inspect-1.0 $e >/dev/null 2>&1 && echo "OK  $e" || echo "MANCA  $e"; done
```

- Versione di Windows (`winver`), scheda video e driver (Gestione dispositivi → Schede video).

## 1. Test automatici, con un display vero (non in CI)

```bash
meson test -C build --print-errorlogs
```

- [ ] Esito atteso: **tutti OK** (17 a oggi), nessun `SKIP` su `video_player` e `main_window` (in CI sono saltati solo perché manca il plugin).
- [ ] **`main_window`** e **`video_player`** sono i test più importanti: sono la prima esecuzione su Windows. Se falliscono, allega il log (righe `attesa dello stato … scaduta`, messaggi `[GST]`, asserzioni).
- [ ] Ripeti `meson test -C build video_player main_window` altre 2 volte: nessun test intermittente.

## 2. Prove manuali dell'app

Avvio dalla shell MSYS2: `build/src/syncview.exe` (con `--debug` per i log).

- [ ] **Finestra vuota**: si apre, mostra «Nessun video» e «Carica video…». Aspetto come nei mockup della direzione 1b.
- [ ] **Tema**: con Windows in modalità **chiara** l'app è chiara, in **scura** è scura; cambiando la modalità di Windows ad app aperta il tema cambia da solo (prova anche `SYNCVIEW_THEME=light` / `dark`: in MSYS2 `SYNCVIEW_THEME=dark build/src/syncview.exe`).
- [ ] **Scala dello schermo**: a 100%, 125% e 150% testi e pulsanti restano nitidi e non si tagliano.
- [ ] **Apri video** (Ctrl+O): si apre il dialogo **nativo di Windows** (non uno GTK generico, riconoscibile dall'aspetto) con i filtri per estensione; apri un `.mp4`, un `.mkv` e un file in una cartella con **spazi e accenti nel nome**: il video compare con chip «A · FEED-1», fps e tempo.
- [ ] **Riproduzione**: Play/Pausa dal pulsante e con Spazio; tempo e barra avanzano fluidi; audio presente se il file lo ha.
- [ ] **Passo per frame**: ←/→ e −1/+1 avanzano esattamente di un frame (40 ms a 25 fps); Shift+←/→ di 10.
- [ ] **File reali, in automatico**: metti 3-4 clip (MP4/H.264, MOV, MKV, 25/30/50/60 fps) in una cartella e lancia `SYNCVIEW_TEST_CLIPS=<cartella> build/tests/test_real_clips.exe`: stampa per ogni file fps, durata e decoder, e termina con `ESITO: OK` o i punti falliti (passo ±1, seek, 2×). Incolla l'output intero.
- [ ] **File reali** (importante, vale su ogni sistema): ripeti riproduzione, seek e passo per frame con un MP4/H.264 da telefono o action cam e, se hai, MKV/MOV, a 25/30/50/60 fps. Annota per ciascuno: estensione, codec (`gst-discoverer-1.0.exe <file>`), fps mostrati nel chip, e se il passo ±1 cambia davvero il frame (il tempo varia di 1/fps). Un video a framerate variabile (spesso da telefono) deve cadere sul passo di 40 ms: segnala se non succede.
- [ ] **Decoder in uso**: con `SYNCVIEW_DEBUG=1 build/src/syncview.exe` apri un MP4/H.264 e cerca nel terminale la riga `[GST] decoder video in uso`: dice se usa un decoder hardware (D3D11, NVIDIA, Intel) o software. Incollala nella risposta.
- [ ] **Seek**: trascinando la barra il video segue; Home/Fine vanno all'inizio e alla fine.
- [ ] **File rotto**: un file di testo rinominato `.mp4` → scheda di errore «Impossibile riprodurre il video»; riavviando l'app **non** ricarica quel file.
- [ ] **Ricarico all'avvio**: chiudi e riapri: ricarica l'ultimo video valido. Controlla il file `%USERPROFILE%\.syncview\user_paths.json` (percorso con backslash salvato e riletto correttamente).
- [ ] **Chiusura**: chiudi con la X della finestra: l'app esce in meno di un secondo e in **Gestione attività** non resta nessun `syncview.exe` né `gst-*`. (Ctrl+C e `kill -TERM` da shell sono previsti solo su Linux/macOS: su Windows non esiste ancora l'uscita da segnale: annota cosa succede con Ctrl+C.)
- [ ] **Video su altro disco o rete**: un file su un disco USB o su una cartella di rete (`\\server\cartella\video.mp4`) si apre e scorre.
- [ ] **Finestre di debug** (M2.9): `build/src/syncview.exe --debug` apre 3 finestre (principale, Log, Moduli); senza `--debug` solo la principale. Pausa/Riprendi, Svuota, Copia, filtri per modulo e livello funzionano; spegnere VIDEO e fare play/seek toglie le righe VIDEO dalla finestra **e** dal terminale (gli errori restano); chiudere la principale chiude anche le altre.
- [ ] **Controllo dipendenze**: `build/src/syncview.exe --check-deps` stampa il report e non si blocca. Incolla l'output intero.

- [ ] **Installazione delle dipendenze (M2.11)**: simula la mancanza e lascia che l'app installi. Su una macchina di prova (o dopo aver disinstallato GStreamer) avvia l'app e, quando sarà disponibile l'interfaccia (M2.12), accetta l'installazione: devono comparire l'elenco esatto (URL, dimensione, SHA-256) e l'avviso sulla UAC. Annullando la richiesta di autorizzazione non deve cambiare nulla e l'app non deve riprovare da sola. Dopo l'installazione l'app deve trovare i plugin (`syncview --check-deps`).

## 3. Cosa raccogliere se qualcosa fallisce

In MSYS2:

```bash
SYNCVIEW_DEBUG=1 GST_DEBUG=3 build/tests/test_main_window.exe 2> main_window.log   # per un test che fallisce
SYNCVIEW_DEBUG=1 build/src/syncview.exe 2> app.log                                  # per l'app
gst-inspect-1.0 gtk4paintablesink > sink.txt 2>&1
```

Allega i file, `winver`, la scheda video e il file `%USERPROFILE%\.syncview\syncview_log.txt`. Per un blocco in caricamento aggiungi `GST_DEBUG=gtk4paintablesink:6,GST_STATES:4`.

## Da aggiungere nelle prossime milestone

- M2.11/M2.12: installazione delle dipendenze con l'installer ufficiale di GStreamer (richiesta UAC gestita da Windows) e confronto con MSYS2.
- M3: più video, sincronizzazione, zoom/pan, drag&drop di file da Esplora risorse.
- Uscita ordinata da segnale/Ctrl+C su Windows (oggi solo Unix).
