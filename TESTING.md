# TESTING — SyncView-C

Questo documento spiega come avviare il progetto e cosa testare manualmente allo stato attuale. Viene aggiornato ad ogni milestone con i nuovi elementi testabili — per ora copre solo **M0 (Scaffolding)**, l'unica fase completata.

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
meson test -C build
```

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

- [ ] `meson test -C build` esegue e riporta **1/1 OK** (`syncview:dummy`).
- [ ] Il log completo è consultabile in `build/meson-logs/testlog.txt`.

### M0.5 — Documentazione

- [ ] [README.md](README.md) è presente e descrive correttamente lo stack attuale (pipeline GStreamer manuale, `GstDiscoverer`, plugin base+good+bad+libav).
- [ ] [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) è presente (stub).
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

## Cosa NON è ancora testabile

Tutto ciò che riguarda logica applicativa reale (sync engine, markers, playback video, export, UI oltre la finestra vuota, modalità debug) appartiene a M1 e successive — non ancora implementato. Questo file verrà esteso con una nuova sezione ad ogni milestone completata.
