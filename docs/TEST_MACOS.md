# Prove da fare su macOS

La CI di GitHub ha un runner macOS senza display attivo e senza GPU: non può verificare tutto ciò che riguarda finestre, frame clock e rendering. Questo file elenca cosa va provato **a mano su un Mac reale** (con schermo). Si aggiorna a ogni milestone; a fine M2 va consegnato a chi ha il Mac.

Cosa fa la CI su macOS: esegue `video_player`, `debug_windows` e `deps_dialog` (con le verifiche sul frame clock saltate) e **salta `main_window`**; la CI non installa `gst-plugins-rs`, quindi lì non c'è `gtk4paintablesink` reale: i test che ne hanno bisogno si saltano o girano con ciò che il runner offre. Il file gemello per Windows è [TEST_WINDOWS.md](TEST_WINDOWS.md).

Chi prova: segni ogni voce con ✅ / ❌ e, per ogni ❌, allega l'output richiesto (vedi «Cosa raccogliere»).

## Riepilogo: cosa di M2 NON è confermato su macOS

| Milestone | Cosa è stato verificato | Cosa resta da confermare su un Mac vero |
|---|---|---|
| M2.2 `discoverer` | test in CI (macOS incluso) | niente di specifico; solo con file reali (vedi M2.7) |
| M2.4–M2.5 player: carica, play, pausa, stop | test in CI (macOS incluso), file VP8/WebM generati | **decoder hardware** (VideoToolbox): quale decoder usa davvero (riga `[GST] decoder video in uso` nel log) e se funziona; audio |
| M2.6 posizione/durata col frame clock | test **saltati** su macOS in CI (runner senza display attivo) | tutto: il polling segue il frame clock, il tempo avanza fluido, da fermo nessun risveglio periodico |
| M2.7 seek, passo per frame, velocità | test in CI (macOS incluso) con video sintetici VP8; su **Linux** 69 clip reali (H.264 30 e 60 fps, H.265, AV1, VP9, VP8; MP4, WebM, MKV, MOV) con decoder hardware: tutto esatto | gli stessi controlli con file reali **su Mac** (`test_real_clips`, vedi più sotto), anche da telefono o action cam, 25/30/50/60 fps e **framerate variabile** (mai provato); decoder VideoToolbox (H.264/H.265) |
| M2.8 finestra minima | test `main_window` **saltato** su macOS in CI | **il video si carica nella finestra** (problema aperto: in CI la pipeline resta ferma dopo `READY → PAUSED`), dialogo «Apri» nativo, tema chiaro/scuro che segue macOS, scorciatoie (Ctrl o Cmd?), chiusura pulita, aspetto della barra del titolo |
| M2.9 finestre di debug | test in CI (macOS incluso) | aspetto, Pausa/Copia/filtri, interruttori dei moduli, chiusura insieme alla principale |
| M2.10 verifica dipendenze | test con sonde simulate | **cosa trova davvero su un Mac**: `gtk4paintablesink` esiste su Homebrew? i nomi dei pacchetti Homebrew nella tabella sono giusti? (marcati «non confermati» in `src/core/deps_check.c`) |
| M2.11 installazione delle dipendenze | download con SHA-256, piani e consenso verificati con server locale e funzioni finte; **il lancio vero dell'installer non è mai stato eseguito** | l'app scarica il `.pkg` ufficiale (154 MB) e lo apre con `open -W`: si apre l'Installer di macOS e chiede lui la password; annullando non cambia nulla; dopo l'installazione l'app trova i plugin (percorso del framework GStreamer) |
| M2.12 interfaccia del primo avvio | `deps_dialog`, `deps_state`, menu e riapertura dopo un plugin mancante in test su Linux (installatore finto) | **tutto sul sistema reale**: la finestra «Preparazione di SyncView» (aspetto, tema, scala dello schermo), l'installazione vera con l'avviso sulla password di sistema, «Continua senza» ricordato dopo il riavvio, Menu → «Verifica dipendenze…» |

**Priorità** (se il tempo del collega è poco): 1) `gtk4paintablesink` esiste? 2) `main_window` passa? 3) un video reale si apre, scorre e si può avanzare di un frame? 4) il tema segue macOS?

## 0. Preparazione

```bash
brew install meson ninja gtk4 gstreamer gst-plugins-base gst-plugins-good gst-plugins-ugly gst-plugins-bad gst-libav gst-plugins-rs sqlite json-glib libsoup pkg-config
git clone <repository> && cd SyncView && git checkout SyncView-C
meson setup build && meson compile -C build
```

- **Il plugin `gtk4paintablesink` su Homebrew non è confermato.** Verifica con `gst-inspect-1.0 gtk4paintablesink`. Se manca, scrivilo: è il primo ostacolo (vedi `src/core/deps_check.c`, tabella dei pacchetti, e `PLAN.md` M2.11/M2.12). Senza di esso nessuna prova con il video è possibile.
- Versione di macOS e architettura (`uname -m`: arm64 o x86_64).

## 1. Test automatici, con un display vero (non in CI)

Esegui **senza** la variabile `CI` impostata (in CI i test a finestra vengono saltati su macOS):

```bash
unset CI
meson test -C build --print-errorlogs
```

- [ ] Esito atteso: **22 test: tutti OK tranne `real_clips`**, che è `SKIP` se non gli dai `SYNCVIEW_TEST_CLIPS` (vedi sotto). Nessun `SKIP` su `video_player`, `main_window` e `deps_dialog`: se ci sono, manca `gtk4paintablesink` (vedi sopra).
- [ ] **`main_window`** (problema aperto, il più importante): sul runner CI il caricamento di un video agganciato alla finestra non termina (la pipeline resta ferma dopo `READY → PAUSED`). Se qui passa, il problema è solo del runner; se fallisce, allega il log (la riga `attesa dello stato … scaduta` e i messaggi `[GST]`).
- [ ] **`video_player`**: con un display reale girano anche le verifiche sul frame clock (polling della posizione, ticker, tempo che avanza). In CI sono saltate. Messaggi «frame clock irregolare/assente» = il frame clock non batte: segnalalo.
- [ ] Ripeti `meson test -C build video_player` altre 2 volte: nessun test intermittente.

## 2. Prove manuali dell'app

Avvio: `build/src/syncview` (con `--debug` per i log; vedi M2.9).

- [ ] **Finestra vuota**: si apre, mostra «Nessun video» e il pulsante «Carica video…». Aspetto come nei mockup della direzione 1b (barra del titolo, riquadro nero, pulsanti arrotondati).
- [ ] **Tema**: con macOS in modalità **chiara** l'app è chiara, in **scura** è scura; cambiando la modalità di sistema a app aperta, il tema cambia da solo (provare anche `SYNCVIEW_THEME=light` / `dark`).
- [ ] **Apri video** (Ctrl+O, che su Mac potrebbe dover essere Cmd+O: annota): si apre il dialogo di sistema (non quello GTK generico) con i filtri per estensione; scegli un `.mp4` e un `.mkv`: il video compare, con chip «A · FEED-1», fps e tempo.
- [ ] **Riproduzione**: Play/Pausa dal pulsante e con Spazio; il tempo e la barra avanzano **fluidi**; audio presente se il file lo ha.
- [ ] **Passo per frame**: ←/→ e i pulsanti −1/+1 avanzano esattamente di un frame (il tempo cambia di 40 ms a 25 fps); Shift+←/→ di **10 frame** (a 30 fps: 333 ms; a 60 fps: 167 ms). Il passaggio del mouse sui pulsanti −10/−1/+1/+10 mostra «N frame indietro/avanti».
- [ ] **File reali, in automatico**: metti 3-4 clip (MP4/H.264, MOV, MKV, 25/30/50/60 fps; le sottocartelle vanno bene) in una cartella e lancia `SYNCVIEW_TEST_CLIPS=<cartella> ./build/tests/test_real_clips`: stampa per ogni file fps, durata e decoder, e termina con `ESITO: OK` o i punti falliti (passo ±1, seek, 2×). Incolla l'output intero.
- [ ] **File reali** (importante, vale su ogni sistema): ripeti riproduzione, seek e passo per frame con un MP4/H.264 da telefono o action cam e, se hai, un MKV o MOV, a 25/30/50/60 fps. Annota per ciascuno: l'estensione, il codec (`gst-discoverer-1.0 <file>` lo mostra), i fps mostrati nel chip e se il passo ±1 cambia davvero il frame (il tempo deve variare di 1/fps). Un video a framerate variabile (spesso quelli da telefono) deve cadere sul passo di 40 ms: segnala se non succede.
- [ ] **Decoder in uso**: con `SYNCVIEW_DEBUG=1 build/src/syncview` apri un MP4/H.264 e cerca nel terminale la riga `[GST] decoder video in uso`: dice se usa un decoder hardware (VideoToolbox) o software. Incollala nella risposta.
- [ ] **Seek**: trascinando la barra il video segue; Home/Fine vanno all'inizio e alla fine.
- [ ] **File rotto**: apri un file di testo rinominato `.mp4` → scheda di errore «Impossibile riprodurre il video» e, riavviando l'app, **non** ricarica quel file (il percorso salvato resta quello dell'ultimo video valido).
- [ ] **Ricarico all'avvio**: chiudi e riapri l'app: ricarica da sola l'ultimo video valido.
- [ ] **Chiusura**: chiudi la finestra con il pulsante rosso e, da terminale, con Ctrl+C e con `kill -TERM <pid>`: l'app esce in meno di un secondo, nessun processo `syncview` o `gst-*` rimasto (`pgrep -fl syncview`).
- [ ] **Finestre di debug** (M2.9): `build/src/syncview --debug` apre 3 finestre (principale, Log, Moduli); senza `--debug` solo la principale. Cmd+Q e il menu di sistema chiudono tutto?
- [ ] **Log in tempo reale** (con `--debug`): la finestra Log mostra le righe mentre avvengono (apri un video, play, seek); Pausa/Riprendi, Svuota e Copia (incolla altrove per controllare) funzionano; i filtri per modulo e livello nascondono righe.
- [ ] **Interruttori dei moduli**: spegnere VIDEO e fare play/seek → le righe VIDEO non compaiono più né nella finestra né nel terminale (stderr); gli errori sì. Riaccendere con «Tutti».
- [ ] **Chiusura insieme**: chiudere la finestra principale chiude anche Log e Moduli, e l'app termina.
> **Attenzione all'installazione vera (macOS).** Con la variabile `SYNCVIEW_DEPS_FAKE_MISSING` anche un solo componente (anche opzionale come `gst-decoder-hevc`) fa preparare all'app il passo «Installa GStreamer con l'installer ufficiale»: premendo «Installa» scarica il `.pkg` (**154 MB**) e lo apre nell'Installer di macOS, che **installa GStreamer sul Mac**. Fallo solo su un Mac di prova. Per provare la finestra senza installare nulla basta **non premere «Installa»** (Annulla / Continua senza / Installa a mano) oppure **annullare la richiesta di autorizzazione** (prova utile: non deve cambiare nulla).

- [ ] **Finestra «Preparazione di SyncView» (M2.12)**: avvia l'app con `SYNCVIEW_DEPS_FAKE_MISSING=gst-decoder-hevc,ffmpeg` (variabile d'ambiente; `all` per simulare tutto): all'avvio compare la finestra con le righe dei componenti, l'**elenco esatto** di ciò che verrà installato (URL, dimensione, SHA-256 dove c'è un download) e l'avviso sulla password di sistema. Controlla: aspetto leggibile in tema chiaro e scuro; **nulla parte finché non premi «Installa»**; «Annulla» chiude senza cambiare nulla; «Continua senza» e riavvio con la stessa variabile → **nessuna finestra**; Menu → «Verifica dipendenze…» → la finestra compare sempre (con tutto a posto dice «Tutte le dipendenze sono a posto»). Con la variabile e «Installa» su una macchina di prova: compare la richiesta di autorizzazione di macOS; **rifiutandola** la finestra dice che non è stato modificato nulla e **non riprova da sola**; dopo un'installazione vera il ricontrollo finale mostra le righe verdi (o dice che serve riavviare SyncView). Allega una schermata di ogni passaggio. Per ripetere la prova da zero cancella il file dello stato delle dipendenze (`~/.syncview/deps_state.json`).
- [ ] **Controllo dipendenze**: `build/src/syncview --check-deps` stampa il report e non si blocca. Incolla l'output intero: serve a verificare i nomi dei pacchetti Homebrew della tabella.

- [ ] **Dopo un'installazione vera (solo su Mac di prova)**: dopo che l'Installer di GStreamer ha finito, `build/src/syncview --check-deps` trova i plugin (percorso del framework GStreamer) e la finestra «Preparazione» mostra le righe verdi o dice di riavviare SyncView. Incolla l'output.

## 3. Cosa raccogliere se qualcosa fallisce

```bash
SYNCVIEW_DEBUG=1 GST_DEBUG=3 build/tests/test_main_window 2> main_window.log     # per un test che fallisce
SYNCVIEW_DEBUG=1 build/src/syncview 2> app.log                                    # per l'app
gst-inspect-1.0 gtk4paintablesink > sink.txt 2>&1
```

Allega i file, il modello di Mac, la versione di macOS (`sw_vers`) e, per la finestra delle dipendenze, `~/.syncview/deps_state.json` con una schermata. Per un blocco in caricamento aggiungi anche `GST_DEBUG=gtk4paintablesink:6,GST_STATES:4`.

## Da aggiungere nelle prossime milestone

- (M2.11/M2.12 fatte: vedi le prove sopra) installazione delle dipendenze con l'installer ufficiale di GStreamer (richiede la password di sistema, gestita da macOS).
- M3: più video, sincronizzazione, zoom/pan, drag&drop di file dal Finder.
- Scorciatoie: quali tasti vanno adattati a macOS (Cmd al posto di Ctrl).
