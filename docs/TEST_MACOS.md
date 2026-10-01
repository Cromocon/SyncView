# Prove da fare su macOS

La CI di GitHub ha un runner macOS senza display attivo e senza GPU: non può verificare tutto ciò che riguarda finestre, frame clock e rendering. Questo file elenca cosa va provato **a mano su un Mac reale** (con schermo). Si aggiorna a ogni milestone; a fine M2 va consegnato a chi ha il Mac.

Chi prova: segni ogni voce con ✅ / ❌ e, per ogni ❌, allega l'output richiesto (vedi «Cosa raccogliere»).

## 0. Preparazione

```bash
brew install meson ninja gtk4 gstreamer gst-plugins-base gst-plugins-good gst-plugins-bad gst-libav gst-plugins-rs sqlite json-glib pkg-config
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

- [ ] Esito atteso: **tutti OK** (17 a oggi), nessun `SKIP` su `video_player` e `main_window`.
- [ ] **`main_window`** (problema aperto, il più importante): sul runner CI il caricamento di un video agganciato alla finestra non termina (la pipeline resta ferma dopo `READY → PAUSED`). Se qui passa, il problema è solo del runner; se fallisce, allega il log (la riga `attesa dello stato … scaduta` e i messaggi `[GST]`).
- [ ] **`video_player`**: con un display reale girano anche le verifiche sul frame clock (polling della posizione, ticker, tempo che avanza). In CI sono saltate. Messaggi «frame clock irregolare/assente» = il frame clock non batte: segnalalo.
- [ ] Ripeti `meson test -C build video_player` altre 2 volte: nessun test intermittente.

## 2. Prove manuali dell'app

Avvio: `build/src/syncview` (con `--debug` per i log; vedi M2.9).

- [ ] **Finestra vuota**: si apre, mostra «Nessun video» e il pulsante «Carica video…». Aspetto come nei mockup della direzione 1b (barra del titolo, riquadro nero, pulsanti arrotondati).
- [ ] **Tema**: con macOS in modalità **chiara** l'app è chiara, in **scura** è scura; cambiando la modalità di sistema a app aperta, il tema cambia da solo (provare anche `SYNCVIEW_THEME=light` / `dark`).
- [ ] **Apri video** (Ctrl+O, che su Mac potrebbe dover essere Cmd+O: annota): si apre il dialogo di sistema (non quello GTK generico) con i filtri per estensione; scegli un `.mp4` e un `.mkv`: il video compare, con chip «A · FEED-1», fps e tempo.
- [ ] **Riproduzione**: Play/Pausa dal pulsante e con Spazio; il tempo e la barra avanzano **fluidi**; audio presente se il file lo ha.
- [ ] **Passo per frame**: ←/→ e i pulsanti −1/+1 avanzano esattamente di un frame (il tempo cambia di 40 ms a 25 fps); Shift+←/→ di 10 frame.
- [ ] **Seek**: trascinando la barra il video segue; Home/Fine vanno all'inizio e alla fine.
- [ ] **File rotto**: apri un file di testo rinominato `.mp4` → scheda di errore «Impossibile riprodurre il video» e, riavviando l'app, **non** ricarica quel file (il percorso salvato resta quello dell'ultimo video valido).
- [ ] **Ricarico all'avvio**: chiudi e riapri l'app: ricarica da sola l'ultimo video valido.
- [ ] **Chiusura**: chiudi la finestra con il pulsante rosso e, da terminale, con Ctrl+C e con `kill -TERM <pid>`: l'app esce in meno di un secondo, nessun processo `syncview` o `gst-*` rimasto (`pgrep -fl syncview`).
- [ ] **Finestre di debug** (M2.9): `build/src/syncview --debug` apre 3 finestre (principale, Log, Moduli); senza `--debug` solo la principale. Cmd+Q e il menu di sistema chiudono tutto?
- [ ] **Log in tempo reale** (con `--debug`): la finestra Log mostra le righe mentre avvengono (apri un video, play, seek); Pausa/Riprendi, Svuota e Copia (incolla altrove per controllare) funzionano; i filtri per modulo e livello nascondono righe.
- [ ] **Interruttori dei moduli**: spegnere VIDEO e fare play/seek → le righe VIDEO non compaiono più né nella finestra né nel terminale (stderr); gli errori sì. Riaccendere con «Tutti».
- [ ] **Chiusura insieme**: chiudere la finestra principale chiude anche Log e Moduli, e l'app termina.
- [ ] **Controllo dipendenze**: `build/src/syncview --check-deps` stampa il report e non si blocca.

## 3. Cosa raccogliere se qualcosa fallisce

```bash
SYNCVIEW_DEBUG=1 GST_DEBUG=3 build/tests/test_main_window 2> main_window.log     # per un test che fallisce
SYNCVIEW_DEBUG=1 build/src/syncview 2> app.log                                    # per l'app
gst-inspect-1.0 gtk4paintablesink > sink.txt 2>&1
```

Allega i file, il modello di Mac e la versione di macOS (`sw_vers`). Per un blocco in caricamento aggiungi anche `GST_DEBUG=gtk4paintablesink:6,GST_STATES:4`.

## Da aggiungere nelle prossime milestone

- M2.11/M2.12: installazione delle dipendenze con l'installer ufficiale di GStreamer (richiede la password di sistema, gestita da macOS).
- M3: più video, sincronizzazione, zoom/pan, drag&drop di file dal Finder.
- Scorciatoie: quali tasti vanno adattati a macOS (Cmd al posto di Ctrl).
