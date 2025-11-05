# SyncView - Tactical Multi-Video Analysis (T-MVA)

**Version:** 3.0 (Phase 3 Complete - Timeline & Markers)  
**Status:** ✅ Production Ready  
**Last Updated:** October 19, 2025

---

## 📋 Documentation

- 📖 **[README.md](README.md)** (this file) - Overview, installation, usage
- 📝 **[CHANGELOG.md](CHANGELOG.md)** - Version changelog and release notes
- 🧹 **[DEBLOAT_REPORT.md](DEBLOAT_REPORT.md)** - Code optimization report
- 📚 **[DOCUMENTATION_INDEX.md](DOCUMENTATION_INDEX.md)** - Navigation guide

---

## Descrizione

SyncView è un'applicazione desktop professionale per l'analisi multi-video tattica costruita con PyQt6. Permette la visualizzazione sincronizzata di fino a 4 video simultanei con controlli frame-by-frame, timeline interattiva e sistema di markers per annotazioni.

### 🎯 Caratteristiche Principali

- **4 Video Simultanei**: Griglia 2x2 con player indipendenti
- **Sincronizzazione Avanzata**: Controllo globale o individuale
- **Timeline Interattiva**: Visualizzazione grafica posizione e durata
- **Sistema Markers**: Annotazioni temporali con colori e categorie
- **Frame-by-Frame**: Navigazione precisa frame per frame
- **FPS Detection**: Rilevamento automatico framerate
- **Custom Window**: Finestra Qt personalizzata senza decorazioni OS
- **Persistenza Automatica**: Auto-save markers ogni 30 secondi
- **Export CSV**: Esportazione markers per analisi esterna
- **Tactical Theme**: Tema militare professionale

---

## Struttura del Progetto

```
SyncView/
├── config/          # Configurazione dell'applicazione
│   ├── __init__.py
│   ├── settings.py  # Colori, FPS options, formati supportati
│   └── user_paths.py  # Gestione path utente memorizzati
├── core/            # Logica di business
│   ├── __init__.py
│   ├── logger.py    # Sistema di logging categorizzato
│   ├── sync_manager.py  # Motore di sincronizzazione
│   ├── markers.py   # Gestione markers con SQLite
│   ├── marker_db.py # Database layer per markers
│   └── advanced_exporter.py  # Export video con parallelizzazione
├── ui/              # Interfaccia utente
│   ├── __init__.py
│   ├── main_window.py      # Finestra principale + DraggableTitleBar
│   ├── video_player.py     # Widget player video individuale
│   ├── fps_dialog.py       # Dialogo FPS personalizzato
│   ├── marker_dialog.py    # Dialog gestione markers
│   ├── simple_export_dialog.py  # Dialog export semplificato
│   └── styles.py           # Qt stylesheets
├── main.py          # Entry point dell'applicazione
├── requirements.txt # Dipendenze Python
├── run.sh          # Script avvio rapido (Linux/macOS)
├── run.bat         # Script avvio rapido (Windows)
├── MARKER_DATABASE.md            # Documentazione sistema database
├── COMPLETE_HISTORY.md           # Storia completa sviluppo
├── TECHNICAL_DOCUMENTATION.md    # Documentazione tecnica
├── CHANGELOG.md                  # Changelog versioni
├── DEVELOPER_LOG.md              # Log sviluppatore
└── README.md                     # Questo file

Note: L'applicazione non crea directory predefinite. I path dei video
e della directory di export vengono memorizzati in ~/.syncview/user_paths.json
```

---

## 🚀 Quick Start

### 🐧 Linux / 🍎 macOS

#### Metodo 1: Script Automatico (Raccomandato)

```bash
chmod +x run.sh  # Solo la prima volta
./run.sh
```

Lo script automaticamente:
- ✅ Rileva il sistema operativo
- ✅ Verifica Python3 e fornisce istruzioni se mancante
- ✅ Crea virtual environment (se non esiste)
- ✅ Installa tutte le dipendenze da requirements.txt
- ✅ Verifica FFmpeg e suggerisce installazione
- ✅ Avvia l'applicazione

**Prerequisiti Linux:**
```bash
# Ubuntu/Debian
sudo apt install python3 python3-venv python3-pip ffmpeg

# Fedora/RHEL
sudo dnf install python3 python3-pip ffmpeg

# Arch Linux
sudo pacman -S python python-pip ffmpeg
```

**Prerequisiti macOS:**
```bash
# Con Homebrew
brew install python3 ffmpeg
```

### 🪟 Windows

#### Metodo 1: Script Automatico (Raccomandato)

**Doppio click su:** `run.bat`

oppure da PowerShell/CMD:
```cmd
run.bat
```

Lo script automaticamente:
- ✅ Rileva Python (python, py, o python3)
- ✅ Crea virtual environment (se non esiste)
- ✅ Installa tutte le dipendenze
- ✅ Verifica FFmpeg e suggerisce installazione
- ✅ Avvia l'applicazione

**Prerequisiti Windows:**
1. **Python 3.10+**: [Download da python.org](https://www.python.org/downloads/)
   - ⚠️ **IMPORTANTE**: Durante l'installazione seleziona "Add Python to PATH"
2. **FFmpeg** (opzionale ma raccomandato):
   ```powershell
   # Con winget
   winget install FFmpeg
   
   # Con Chocolatey
   choco install ffmpeg
   ```
   oppure scarica da [ffmpeg.org](https://ffmpeg.org/download.html)

---

### 🛠️ Metodo 2: Installazione Manuale (Tutte le piattaforme)

#### 1. Creare l'ambiente virtuale
```bash
# Linux/macOS
python3 -m venv .venv

# Windows
python -m venv .venv
```

#### 2. Attivare l'ambiente virtuale
```bash
# Linux/macOS
source .venv/bin/activate

# Windows (CMD)
.venv\Scripts\activate.bat

# Windows (PowerShell)
.venv\Scripts\Activate.ps1
```

#### 3. Installare le dipendenze
```bash
pip install --upgrade pip
pip install -r requirements.txt
```

#### 4. Avviare l'applicazione
```bash
python main.py
```

---

## 📦 Dipendenze

**Richieste:**
- Python 3.10+
- PyQt6 6.6.0+
- moviepy 1.0.3+ (per export futuro)

**Opzionali:**
- FFmpeg/FFprobe (per rilevamento FPS automatico)

---

## ⌨️ Scorciatoie da Tastiera

### Controlli Playback
| Tasto | Azione |
|-------|--------|
| `Spazio` | Play / Pausa globale |
| `Home` | Vai all'inizio |
| `End` | Vai alla fine |
| `M` | Mute master audio |

### Navigazione Frame (Modalità Frame attiva)
| Tasto | Azione |
|-------|--------|
| `←` | Frame precedente |
| `→` | Frame successivo |
| `Shift + ←` | Indietro 10 frame |
| `Shift + →` | Avanti 10 frame |

### Timeline & Markers
| Tasto | Azione |
|-------|--------|
| `Ctrl+M` | Aggiungi marker alla posizione corrente |
| `P` | Vai al marker precedente |
| `N` | Vai al marker successivo |
| `Ctrl+Shift+M` | Apri dialog gestione markers |

### Sistema
| Tasto | Azione |
|-------|--------|
| `Ctrl+O` | Carica video |
| `Ctrl+S` | Toggle sincronizzazione |
| `Ctrl+F` | Toggle modalità frame |
| `F1` | Mostra guida |

---

## 📍 Sistema Markers

### Funzionalità Markers

**Creazione:**
1. Metti in pausa il video alla posizione desiderata
2. Premi `Ctrl+M` o click su "+ Marker"
3. Inserisci etichetta descrittiva
4. Il marker viene salvato automaticamente

**Navigazione:**
- Click su marker nella timeline → salta a quel timestamp
- Premi `P` / `N` per navigare tra markers
- Drag marker sulla timeline per spostarlo

**Gestione:**
- `Ctrl+Shift+M` apre il dialog gestione
- Filtri per categoria e ricerca testuale
- Edit inline (doppio click)
- Export CSV per analisi esterna
- Statistiche dettagliate

**Persistenza:**
- Auto-save ogni 30 secondi
- Salvataggio automatico alla chiusura
- File: `syncview_markers.json`
- Caricamento automatico all'avvio

### Categorie Predefinite
- **default**: Markers generici
- **action**: Azioni tattiche
- **event**: Eventi importanti
- **note**: Note generali
- **highlight**: Momenti salienti
- **review**: Da rivedere

### Colori Disponibili
- 🔴 Rosso (eventi critici)
- 🟡 Giallo (attenzione)
- 🟢 Verde (positivo)
- 🔵 Blu (informazione)
- 🟣 Viola (speciale)
- 🟠 Arancio (alert)
- 🩵 Ciano (nota)
- 🩷 Rosa (highlight)

---

## 📖 Utilizzo

---

## 🎨 Tema Tattico

**Colori:**
- Primary: `#0d0d0d` (Nero puro)
- Secondary: `#1a1a1a` (Grigio scuro)
- Accent: `#4a9f5e` (Verde tattico)
- Text: `#e0e0e0` (Grigio chiaro)
- Error: `#cc5555` (Rosso)

---

## 📊 Fasi di Sviluppo

- ✅ **Fase Pre-Launch**: Setup progetto e struttura
- ✅ **Fase 0**: Gestione dipendenze e logging
- ✅ **Fase 1**: UI e playback base (v1.0 - v1.3)
  - v1.0: Implementazione iniziale
  - v1.1: Controlli unificati
  - v1.2: Preview frame e sync timeline
  - v1.3: Colori attenuati e FPS detection
- ✅ **Fase 2**: Funzionalità avanzate (v2.0 - v2.5)
  - v2.0: Motore sincronizzazione avanzato
  - v2.1: Controlli frame-by-frame
  - v2.2: Finestra Qt personalizzata
  - v2.3: UI unificata (menu rimossi)
  - v2.4: Controlli per-video
  - v2.4b: Visibilità controlli smart
  - v2.5: Fix drag & drop
- ✅ **Fase 3**: Timeline markers e annotazioni (v3.0)
  - Sistema markers completo (CRUD)
  - Timeline widget interattiva
  - Persistenza automatica JSON
  - Dialog gestione markers
  - Export CSV
  - Shortcuts dedicati (Ctrl+M, P, N)
- 🔄 **Fase 4**: Export con moviepy (Pianificata)

---

## 🎮 Utilizzo

### Caricamento Video

**Metodo 1: Drag & Drop**
- Trascina file video sui placeholder

**Metodo 2: Bottone Carica**
- Click su "📁 CARICA" in ogni video slot

**Metodo 3: Scorciatoia**
- Premi `Ctrl+O`

**Metodo 4: Auto-caricamento**
- Se hai caricato video in precedenza, verranno ricaricati automaticamente
- all'avvio (se i file esistono ancora)

**Nota:** L'applicazione memorizza i path dei video caricati in
`~/.syncview/user_paths.json` per ricaricarli automaticamente.

### Modalità di Controllo

**Sincronizzazione ON (default):**
- Controlli globali visibili
- Play/Pause/Inizio/Fine agiscono su tutti i video
- Timeline sincronizzate
- Controlli individuali nascosti

**Sincronizzazione OFF:**
- Controlli globali nascosti
- Ogni video controllabile indipendentemente
- Timeline indipendenti
- Bottone "🔄 SINCRONIZZA TUTTO" disponibile

### Modalità Frame

**Attivazione:**
- Click su checkbox "Modalità Frame"
- Oppure `Ctrl+F`

**Controlli:**
- `⏪ -10`: Indietro 10 frame
- `◀ -1`: Frame precedente (o freccia `←`)
- `▶ +1`: Frame successivo (o freccia `→`)
- `⏩ +10`: Avanti 10 frame

---

## 🛠️ Sviluppo

### Struttura Codice

**Pattern MVC:**
- Model: `core/sync_manager.py`, stato in VideoPlayerWidget
- View: `ui/*.py` (componenti Qt)
- Controller: Connessioni signal/slot in main_window.py

**Signal/Slot:**
```python
# Custom signals
position_changed = pyqtSignal(int)
duration_changed = pyqtSignal(int)
user_seeked = pyqtSignal(int, int)

# Connections
player.user_seeked.connect(self.on_video_seeked)
```

### Logging

```python
from core.logger import logger

logger.log_user_action("Action", "Details")
logger.log_video_action(index, "Action", "Details")
logger.log_error("Message", exception)
```

Output: `syncview_log.txt`

### Testing

```bash
# Run application
python main.py

# Check logs
tail -f syncview_log.txt
```

---

## 🐛 Troubleshooting

**Problema: FPS detection non funziona**
- Soluzione: Installa FFmpeg (`sudo apt install ffmpeg` su Linux)
- Fallback: Usa "Auto" o imposta FPS manualmente

**Problema: Video non si carica**
- Verifica formato supportato (.mp4, .avi, .mov, .mkv, ecc.)
- Controlla permessi file
- Consulta `syncview_log.txt` per errori

**Problema: Title bar non draggable**
- Riavvia l'applicazione
- Verifica che non ci siano overlay di sistema

**Problema: Sincronizzazione non funziona**
- Verifica checkbox "Sincronizzazione Attiva"
- Controlla che tutti i video siano caricati
- Usa "🔄 SINCRONIZZA TUTTO" per reset

---

## 📄 Licenza

Questo progetto è stato sviluppato per scopi di analisi video tattica.

---

## 👤 Autore

Development Team  
**Last Updated:** October 19, 2025

---

## 🔗 Links Utili

- [PyQt6 Documentation](https://www.riverbankcomputing.com/static/Docs/PyQt6/)
- [FFmpeg Documentation](https://ffmpeg.org/documentation.html)
- [Python Virtual Environments](https://docs.python.org/3/tutorial/venv.html)

---

**Per maggiori dettagli tecnici, consultare `TECHNICAL_DOCUMENTATION.md`**  
**Per la storia completa dello sviluppo, consultare `COMPLETE_HISTORY.md`**
  - v1.2: Preview frame, sincronizzazione timeline automatica
  - v1.3: Colori attenuati, rilevamento FPS automatico
- **Fase 2**: Sincronizzazione avanzata ✅ CORE COMPLETATO
  - v2.0: SyncManager, FPS personalizzato, offset logic, drift detection
- **Fase 3**: Controlli individuali e analisi frame-by-frame (IN ATTESA)
- **Fase 4**: Timeline, marker ed esportazione (IN ATTESA)

## Log
- `syncview_log.txt`: Log di runtime dell'applicazione
- `DEVELOPER_LOG.md`: Log di sviluppo e modifiche

## Licenza
Tutti i diritti riservati.
