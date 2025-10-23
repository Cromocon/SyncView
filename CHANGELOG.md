# SyncView - Changelog

**Note:** Questo changelog contiene solo le modifiche principali.

---

## [3.0] - 2025-10-19 - Timeline & Markers (Phase 3 Complete)

### 📍 Timeline Interattiva
- **TimelineWidget**: Barra timeline grafica sotto i video players
- **Visualizzazione Posizione**: Indicatore verde in tempo reale della posizione corrente
- **Marcatori Temporali**: Tacche ogni 10% con timestamp formattati (MM:SS)
- **Durata Dinamica**: Adattamento automatico alla durata del video caricato
- **Aggiornamento in Tempo Reale**: Refresh ogni 100ms per fluidità

### 🎯 Sistema Markers Completo
- **Marker Class**: Struttura dati con timestamp, label, color, description, category
- **MarkerManager**: Gestione CRUD completa (Create, Read, Update, Delete)
- **Colori Predefiniti**: 8 colori categorizzati (rosso, giallo, verde, blu, viola, arancio, ciano, rosa)
- **Categorie**: default, action, event, note, highlight, review
- **Statistiche**: Conteggi per categoria e colore

### 🖱️ Interazioni Timeline
- **Click Timeline**: Naviga istantaneamente al timestamp cliccato
- **Click Marker**: Salta al timestamp del marker
- **Drag Marker**: Trascina i markers per spostarli (aggiornamento in tempo reale)
- **Hover Effects**: Evidenziazione e tooltip su hover
- **Visual Feedback**: Markers colorati visibili sulla timeline

### ⌨️ Nuove Scorciatoie da Tastiera
- **Ctrl+M**: Aggiungi marker alla posizione corrente
- **P**: Vai al marker precedente
- **N**: Vai al marker successivo  
- **Ctrl+Shift+M**: Apri dialog gestione markers

### 💾 Persistenza Automatica
- **Auto-Load**: Carica markers da `syncview_markers.json` all'avvio
- **Auto-Save**: Salvataggio automatico ogni 30 secondi
- **Save on Close**: Salvataggio markers alla chiusura dell'app
- **Formato JSON**: Struttura versionata con metadati completi

### 🗂️ Dialog Gestione Markers
- **Tabella Completa**: Visualizzazione tutti i markers con dettagli
- **Filtri**: Per categoria e ricerca testuale
- **Statistiche**: Totali e raggruppamenti per categoria
- **Edit Inline**: Modifica label e descrizione (doppio click)
- **Eliminazione**: Singola o tutti i markers
- **Export CSV**: Esportazione completa in formato CSV
- **Tempo Formattato**: HH:MM:SS.mmm per leggibilità

### 🔄 Sincronizzazione Multi-Video
- **Markers Globali**: I markers sono condivisi tra tutti e 4 i video
- **Navigazione Sincronizzata**: Saltare a un marker sposta tutti i video
- **Consistenza**: Posizione timeline unica per tutti i player

### 📁 Nuovi File Phase 3
- `core/markers.py` (380 righe): Sistema gestione markers
- `ui/timeline_widget.py` (285 righe): Widget timeline e controlli
- `ui/marker_dialog.py` (345 righe): Dialog gestione avanzata
- **Totale Fase 3:** +1,010 righe di codice

---

## [2.5] - 2025-10-19 - Drag & Drop Fix

### 🖱️ Fixed Title Bar Dragging
- **Custom DraggableTitleBar Class**: Reimplementata gestione mouse events
- **Mouse Event Transparency**: QLabel impostati come trasparenti agli eventi mouse
- **Drag Funzionante**: Ora possibile trascinare da qualsiasi punto della title bar
- **Pulizia Codice**: Rimossi metodi obsoleti (eventFilter, title_bar_mouse_*)

### 📁 Documentation Consolidation
- Creato `COMPLETE_HISTORY.md`: Storia completa fasi 0-2
- Creato `TECHNICAL_DOCUMENTATION.md`: Documentazione tecnica API e architettura
- Archiviati file obsoleti in `archive/phase1/` e `archive/phase2/`

---

## [2.4b] - 2025-10-19 - Smart Controls Visibility

### 🎮 Context-Aware Controls
- **Sync ON**: Controlli globali visibili, individuali nascosti
- **Sync OFF**: Controlli individuali visibili, globali nascosti
- **Button Reorganization**: `[⏮] [▶ PLAY] [⏭]` ordine logico temporale
- **Icon-Only Buttons**: Rimosso testo da inizio/fine (solo icone con tooltip)

---

## [2.4] - 2025-10-19 - Per-Video Controls

### 📁 Individual Video Management
- **Removed Global Load Button**: Eliminato bottone globale carica video
- **Per-Video Load Button**: Ogni video ha proprio bottone "📁 CARICA"
- **Per-Video Remove Button**: Ogni video ha proprio bottone "✕ RIMUOVI"
- **Smart Button Visibility**: Load visibile quando vuoto, Remove quando caricato
- **Unload Method**: Nuovo metodo per rimuovere video e ripristinare stato

---

## [2.3] - 2025-10-19 - Title Bar Unificata (UI Revision)

### 🎨 UI Completamente Riprogettata
- **Menu Rimossi**: Eliminati menu "File", "Visualizza", "Aiuto"
- **Pulsanti Integrati nella Title Bar**:
  - 📁 Carica Video (da menu File)
  - ❓ Guida (da menu Aiuto)
  - ℹ Info (da menu Aiuto)
- **Status Indicator Spostato**: "● SISTEMA OPERATIVO" ora nella title bar (rimosso header duplicato)

### 🖱️ Drag & Drop Migliorato
- **Drag su Tutta la Title Bar**: Ora è possibile trascinare la finestra cliccando ovunque sulla barra del titolo
- **Event Filter Implementato**: I pulsanti non interferiscono con il drag
- **Doppio Click Funziona Ovunque**: Doppio click su qualsiasi punto della title bar (tranne pulsanti) massimizza/ripristina

### ⌨️ Scorciatoie Mantenute
- **Ctrl+O** → Carica Video
- **F1** → Guida Completa
- **Ctrl+S** → Toggle Sincronizzazione
- **Ctrl+F** → Toggle Modalità Frame
- Tutte le altre scorciatoie esistenti mantenute

### 📊 Spazio Recuperato
- **+55px verticali** risparmiati (menu bar + header)
- Più spazio per la griglia video
- UI più compatta e professionale

### 🎨 Hover Effects Colorati
- Pulsante "Carica Video": Hover verde `#2a4a2a`
- Pulsante "Guida": Hover ciano `#2a4a4a`
- Pulsante "Info": Hover blu `#2a3a4a`
- Pulsante "Chiudi": Hover rosso `#cc5555` (invariato)

---

## [2.2] - 2025-10-19 - Finestra Personalizzata Qt (UI Revision)

### 🪟 Nuova Interfaccia
- **Barra Titolo Personalizzata Qt**: Rimossa decorazione sistema operativo, creata barra titolo custom
  - Sfondo nero scuro (#0d0d0d) con bordo verde militare
  - Titolo "⚡ SYNCVIEW - TACTICAL MULTI-VIDEO ANALYSIS" in verde
  - Pulsanti controllo integrati: Minimizza (−), Massimizza (□/❐), Chiudi (✕)
  - Hover effects personalizzati (pulsante chiudi diventa rosso)
  - Altezza fissa 40px per look compatto e professionale

### 🎮 Interazioni Finestra
- **Drag & Drop Finestra**: Click e trascina sulla barra titolo per muovere la finestra
- **Doppio Click Massimizza**: Doppio click su barra titolo alterna massimizza/ripristina
- **Bordo Finestra**: Bordo verde militare 2px intorno a tutta la finestra
- **Resize Nascosto**: FramelessWindowHint applicato (nessuna decorazione OS)

### 🎨 Miglioramenti Visivi
- **Tema Coerente**: Barra titolo segue completamente il tema tattico/militare
- **Cross-Platform**: Stesso aspetto su Windows/Linux/Mac
- **Professionalità**: Aspetto enterprise integrato

### 📋 Dettagli Tecnici
- Qt.WindowType.FramelessWindowHint per rimuovere decorazioni native
- Eventi mouse custom (mousePressEvent, mouseMoveEvent, mouseDoubleClickEvent)
- Variabili drag state (dragging, drag_position)
- Toggle maximize con cambio icona dinamico (□ ↔ ❐)

---

## [2.1] - 2025-10-19 - Controlli Frame-by-Frame Visibili

### ✨ Nuove Funzionalità
- **Pannello Controlli Frame**: Quando "Modalità Frame" è attiva, appare un pannello dedicato con:
  - Pulsante "◀◀ -10 Frame" - Torna indietro di 10 frame
  - Pulsante "◀ -1 Frame" - Torna indietro di 1 frame
  - ComboBox "Step" - Seleziona dimensione frame (40ms/33ms/100ms/200ms)
  - Pulsante "+1 Frame ▶" - Avanza di 1 frame
  - Pulsante "+10 Frame ▶▶" - Avanza di 10 frame
- **Scorciatoie Tastiera Frame**:
  - **←** (Freccia Sinistra) → -1 Frame
  - **→** (Freccia Destra) → +1 Frame
  - **Shift + ←** → -10 Frame
  - **Shift + →** → +10 Frame
- **Step Configurabile**: 4 opzioni (40ms per 25fps, 33ms per 30fps, 100ms, 200ms)

### 🔧 Miglioramenti
- **Toggle Automatico**: Pannello appare/scompare quando Modalità Frame cambia stato
- **Pausa Automatica**: Tutti i video vengono messi in pausa quando Modalità Frame si attiva
- **Sincronizzazione Frame**: Tutti i video si muovono insieme frame per frame
- **Logging Frame Step**: Ogni movimento frame viene loggato con direzione e millisecondi

### 📋 Comportamento
- Pannello nascosto di default
- Appare quando "Modalità Frame" checkbox è selezionata
- Scompare quando "Modalità Frame" checkbox è deselezionata
- Scorciatoie tastiera funzionano solo quando Modalità Frame è attiva

---

## [2.0] - 2025-10-19 - Sincronizzazione Avanzata e FPS Personalizzato

### 🎯 Nuove Funzionalità
- **SyncManager**: Sistema avanzato di gestione sincronizzazione
  - Offset individuali per ogni video (correzione desincronizzazione)
  - Video master designato per risincronizzazione
  - Drift tolerance con rilevamento automatico
  - Calcolo preciso posizioni sincronizzate con formula offset
- **FPS Dialog Personalizzato**: Dialog modale per inserire FPS custom
  - Range: 1.0 - 240.0 fps con 3 decimali di precisione
  - Preset rapidi: 23.976, 24, 25, 29.97, 30, 50, 60 fps
  - Calcolo automatico playback rate
- **Opzione FPS "Personalizzato"**: Aggiunta al menu velocità

### 🔧 Miglioramenti
- **Gestione FPS Evoluta**: Supporto FPS frazionali (29.97, 23.976)
- **Risincronizzazione Migliorata**: Usa video master e calcola offset
- **Sync Timeline Precisa**: Considera offset individuali nella sincronizzazione
- **Logging Avanzato**: Traccia offset, drift, master video

### 📋 File Nuovi
- `ui/fps_dialog.py` - Dialog FPS personalizzato
- `core/sync_manager.py` - Gestione sincronizzazione avanzata
- `PHASE_2_DOCUMENTATION.md` - Documentazione completa Fase 2

### 🎯 Stato
- ✅ Fase 2 Core: Completata
- 🔄 Fase 2.1 UI Offset Management: Pianificata per futuro

---

## [1.3] - 2025-10-19 - Colori Attenuati e Rilevamento FPS

### 🎨 Miglioramenti Visivi
- **Colori Meno Vibranti**: Palette di colori attenuati per ridurre l'affaticamento visivo durante l'uso prolungato
  - Accent: `#00ff00` → `#4a9f5e` (verde militare opaco)
  - Text: `#e0e0e0` → `#c0c0c0` (grigio chiaro meno brillante)
  - Error: `#ff4444` → `#cc5555` (rosso attenuato)
  - Warning: `#ffaa00` → `#d4a356` (arancione attenuato)
  - Success: `#00ff00` → `#5fa373` (verde successo attenuato)

### 📹 Nuove Funzionalità
- **Rilevamento FPS Automatico**: Ogni video mostra i suoi FPS rilevati nell'header
  - Usa FFprobe (parte di FFmpeg) quando disponibile
  - Mostra "📹 Auto" come fallback se FFprobe non è disponibile
  - FPS inclusi nei log di caricamento video
  - Supporto per FPS frazionali (es. 29.97, 23.976)

### 🔧 Opzioni FPS Aggiornate
- Nuove opzioni velocità: `Auto, 24 fps, 25 fps, 29.97 fps, 30 fps, 50 fps, 59.94 fps, 60 fps`
- Sostituite le opzioni moltiplicatori generici con FPS standard professionali

---

## [1.2] - 2025-10-19 - Preview e Sincronizzazione Timeline

### ✨ Nuove Funzionalità
- **Preview Frame**: Il player mostra automaticamente il terzo frame del video come preview al caricamento (invece di un frame nero)
- **Sincronizzazione Timeline**: Con Sync ON, spostare la timeline di un video sincronizza automaticamente tutti gli altri video alla stessa posizione

### 🔧 Miglioramenti
- **Seek Position Ottimizzato**: Aggiunto parametro `emit_signal` per evitare loop di sincronizzazione
- **Logging Sincronizzazione**: Ogni sincronizzazione timeline viene loggata con dettagli

### 📋 Dettagli Tecnici
- Nuovo segnale `user_seeked` per gestire lo spostamento manuale della timeline
- Funzione `load_preview_frame()` carica il terzo frame (120ms) come anteprima
- Funzione `on_video_seeked()` gestisce la sincronizzazione automatica in MainWindow

---

## [1.1] - 2025-10-19 - Fase 1 Raffinata

### ✨ Miglioramenti UI
- **Pulsante Play/Pausa Unificato**: I pulsanti separati sono stati unificati in un unico toggle button per un'interfaccia più pulita
- **Rimozione Popup Modalità Frame**: La Modalità Frame si attiva silenziosamente senza interrompere il workflow

### 📋 Sistema di Logging Migliorato
- **Pulizia Automatica**: Il file log viene cancellato e ricreato ad ogni avvio
- **Categorizzazione**: Log organizzati con `[AZIONE UTENTE]` e `[VIDEO X]`
- **Dettagli Aumentati**: 
  - Play/Pausa individuali per ogni video
  - Spostamenti timeline con posizione formattata (MM:SS)
  - Toggle audio per ogni video
  - Frame step con posizione
  - Errori dettagliati del media player

### 🐛 Bug Fix
- Migliorata gestione stato pulsante Play/Pausa
- Aggiornamento dinamico stile pulsante

---

## [1.0] - 2025-10-19 - Fase 1 Iniziale

### 🎉 Funzionalità Principali
- Griglia video 2x2 (fino a 4 video simultanei)
- Tema tattico/militare professionale
- Caricamento video: drag & drop, selezione manuale, auto-load
- Controlli globali: Play/Pausa, Inizio/Fine, Audio Master
- Selezione velocità: 0.25x, 0.5x, 1x, 2x, 4x
- Toggle Sincronizzazione ON/OFF
- Modalità Frame (preparata per Fase 3)
- Timeline interattive
- Guida integrata completa in italiano
- Gestione errori video
- Sistema di logging

### 🎨 UI/UX
- Design ispirato a sistemi di controllo militari
- Colori: nero, grigio, verde (#00ff00)
- Menu bar completo
- Scorciatoie da tastiera
- Localizzazione italiana completa

### 📦 Struttura Progetto
- Architettura modulare (config, core, ui)
- Ambiente virtuale Python
- Dipendenze gestite con requirements.txt
- Script di avvio automatico
- Documentazione completa

---

## [0.1] - 2025-10-19 - Setup Iniziale

### 🛠️ Setup
- Creazione struttura progetto
- Configurazione ambiente virtuale
- Sistema di gestione dipendenze
- Logging di runtime e sviluppo
- Script di setup automatico

---

*Per dettagli completi, vedi DEVELOPER_LOG.md*
