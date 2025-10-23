"""
Finestra principale di SyncView.
Gestisce la griglia video 2x2 e i controlli globali.
"""

from PyQt6.QtWidgets import (QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
                             QGridLayout, QPushButton, QLabel, QComboBox,
                             QGroupBox, QCheckBox, QMessageBox, QFileDialog)
# --- MODIFICHE IMPORT ---
from PyQt6.QtCore import Qt, QTimer, QEvent, QObject, QThread
# ------------------------
from PyQt6.QtGui import QKeySequence, QMouseEvent
from pathlib import Path
from typing import Optional
import sys
import subprocess
import shutil

from ui.video_player import VideoPlayerWidget
from ui.fps_dialog import FPSDialog
from ui.timeline_widget import TimelineWidget, TimelineControlWidget
# --- NUOVI IMPORT PER EXPORT ---
from ui.export_dialog import ExportDialog
from core.exporter import VideoExporter
# -------------------------------
from ui.styles import get_main_stylesheet
from config.settings import SOURCE_DIRS, DEFAULT_FPS_OPTIONS, SUPPORTED_VIDEO_FORMATS, EXPORT_DIR
from core.logger import logger
from core.sync_manager import SyncManager
from core.markers import MarkerManager, Marker


class DraggableTitleBar(QWidget):
    """Widget personalizzato per la title bar con supporto drag."""

    def __init__(self, parent: Optional[QMainWindow] = None):
        super().__init__(parent)
        self.parent_window: Optional[QMainWindow] = parent
        self.installEventFilter(self)

    def eventFilter(self, obj, event):  # type: ignore[override]
        """Intercetta eventi mouse da widget figli per gestire il drag."""
        # Solo se l'oggetto è un figlio di questa title bar
        if obj != self and (obj.parent() == self or obj == self):
            # Ignora eventi dai pulsanti
            if isinstance(obj, QPushButton):
                return False

            # Drag con click sinistro
            if event.type() == QEvent.Type.MouseButtonPress:
                if event.button() == Qt.MouseButton.LeftButton and self.parent_window:
                    window_handle = self.parent_window.windowHandle()
                    if window_handle and hasattr(window_handle, 'startSystemMove'):
                        window_handle.startSystemMove()  # type: ignore[attr-defined]
                        return True

            # Doppio click per massimizzare/ripristinare
            elif event.type() == QEvent.Type.MouseButtonDblClick:
                if event.button() == Qt.MouseButton.LeftButton and self.parent_window:
                    if self.parent_window.isMaximized():
                        self.parent_window.showNormal()
                    else:
                        self.parent_window.showMaximized()
                    return True

        return super().eventFilter(obj, event)

    def mousePressEvent(self, event: QMouseEvent):  # type: ignore[override]
        """Gestisce il click del mouse sulla title bar stessa."""
        if event.button() == Qt.MouseButton.LeftButton:
            widget = self.childAt(event.pos())

            if isinstance(widget, QPushButton):
                super().mousePressEvent(event)
                return

            if self.parent_window:
                window_handle = self.parent_window.windowHandle()
                if window_handle and hasattr(window_handle, 'startSystemMove'):
                    window_handle.startSystemMove()  # type: ignore[attr-defined]
                    event.accept()

    def mouseDoubleClickEvent(self, event: QMouseEvent):  # type: ignore[override]
        """Gestisce il doppio click per massimizzare/ripristinare."""
        if event.button() == Qt.MouseButton.LeftButton:
            widget = self.childAt(event.pos())

            if isinstance(widget, QPushButton):
                super().mouseDoubleClickEvent(event)
                return

            if self.parent_window:
                if self.parent_window.isMaximized():
                    self.parent_window.showNormal()
                else:
                    self.parent_window.showMaximized()
                event.accept()


def check_dependencies():
    """
    Controlla lo stato delle dipendenze del sistema.
    Ritorna un dizionario con lo stato di ogni dipendenza.
    """
    status = {
        'all_ok': True,
        'python_version': '',
        'pyqt6': False,
        'moviepy': False,
        'numpy': False,
        'pillow': False,
        'ffmpeg': False,
        'errors': []
    }
    
    # Controlla versione Python
    python_version = f"{sys.version_info.major}.{sys.version_info.minor}.{sys.version_info.micro}"
    status['python_version'] = python_version
    
    # Controlla PyQt6
    try:
        import PyQt6
        status['pyqt6'] = True
    except ImportError:
        status['all_ok'] = False
        status['errors'].append('PyQt6 non trovato')
    
    # Controlla moviepy
    try:
        import moviepy
        status['moviepy'] = True
    except ImportError:
        status['all_ok'] = False
        status['errors'].append('moviepy non trovato')
    
    # Controlla numpy
    try:
        import numpy
        status['numpy'] = True
    except ImportError:
        status['all_ok'] = False
        status['errors'].append('numpy non trovato')
    
    # Controlla Pillow
    try:
        import PIL
        status['pillow'] = True
    except ImportError:
        status['all_ok'] = False
        status['errors'].append('Pillow non trovato')
    
    # Controlla FFmpeg
    if shutil.which('ffmpeg') is not None:
        status['ffmpeg'] = True
    else:
        status['all_ok'] = False
        status['errors'].append('FFmpeg non trovato (raccomandato)')
    
    return status


def generate_dependency_tooltip():
    """Genera il tooltip dinamico per l'indicatore di sistema."""
    deps = check_dependencies()
    
    if deps['all_ok']:
        tooltip = "✅ TUTTE LE DIPENDENZE INSTALLATE CORRETTAMENTE\n\n"
        tooltip += f"🐍 Python: {deps['python_version']}\n"
        tooltip += "✓ PyQt6: Installato\n"
        tooltip += "✓ moviepy: Installato\n"
        tooltip += "✓ numpy: Installato\n"
        tooltip += "✓ Pillow: Installato\n"
        tooltip += "✓ FFmpeg: Disponibile\n"
        tooltip += "\nSistema pronto per l'uso completo!"
    else:
        tooltip = "⚠️ ATTENZIONE: ALCUNE DIPENDENZE MANCANTI\n\n"
        tooltip += f"🐍 Python: {deps['python_version']}\n"
        
        # PyQt6
        if deps['pyqt6']:
            tooltip += "✓ PyQt6: Installato\n"
        else:
            tooltip += "✗ PyQt6: NON TROVATO\n"
        
        # moviepy
        if deps['moviepy']:
            tooltip += "✓ moviepy: Installato\n"
        else:
            tooltip += "✗ moviepy: NON TROVATO (export video non disponibile)\n"
        
        # numpy
        if deps['numpy']:
            tooltip += "✓ numpy: Installato\n"
        else:
            tooltip += "✗ numpy: NON TROVATO\n"
        
        # Pillow
        if deps['pillow']:
            tooltip += "✓ Pillow: Installato\n"
        else:
            tooltip += "✗ Pillow: NON TROVATO\n"
        
        # FFmpeg
        if deps['ffmpeg']:
            tooltip += "✓ FFmpeg: Disponibile\n"
        else:
            tooltip += "⚠ FFmpeg: NON TROVATO (raccomandato per FPS detection)\n"
        
        tooltip += "\n❌ PROBLEMI RILEVATI:\n"
        for error in deps['errors']:
            tooltip += f"  • {error}\n"
        
        tooltip += "\n💡 SOLUZIONE:\n"
        tooltip += "Esegui: pip install -r requirements.txt\n"
        if not deps['ffmpeg']:
            tooltip += "Per FFmpeg: sudo apt install ffmpeg (Linux)\n"
            tooltip += "           brew install ffmpeg (macOS)\n"
            tooltip += "           winget install FFmpeg (Windows)"
    
    return tooltip


class MainWindow(QMainWindow):
    """Finestra principale dell'applicazione SyncView."""

    def __init__(self):
        super().__init__()

        # Rimuove la barra del titolo del sistema operativo e abilita il movimento
        self.setWindowFlags(
            Qt.WindowType.FramelessWindowHint |
            Qt.WindowType.Window
        )
        # Abilita il supporto per il movimento su window manager X11/Wayland
        self.setAttribute(Qt.WidgetAttribute.WA_X11NetWmWindowTypeDesktop, False)

        self.setWindowTitle("SyncView - Tactical Multi-Video Analysis")
        self.setMinimumSize(1400, 900)

        # Stato dell'applicazione
        self.sync_enabled = True
        self.frame_mode_enabled = False
        self.current_playback_rate = 1.0 # Mantenuto a 1.0x
        self.custom_fps = 25.0  # Mantenuto per FPSDialog (se usato in futuro)
        self.focused_video_index = 0  # Indice del video attualmente selezionato/cliccato

        # Player video
        self.video_players = []

        # Sync Manager
        self.sync_manager = SyncManager()

        # Marker Manager con file di progetto
        project_dir = Path.cwd()
        markers_file = project_dir / "syncview_markers.json"
        self.marker_manager = MarkerManager(project_path=markers_file)

        # Pulisci tutti i marker all'avvio
        self.marker_manager.clear_all()
        logger.log_user_action("Marker puliti", "Avvio applicazione")

        # Timer aggiornamento timeline
        self.timeline_update_timer = QTimer()
        self.timeline_update_timer.setInterval(100)  # Aggiorna ogni 100ms
        self.timeline_update_timer.timeout.connect(self.update_timeline_position)
        self.timeline_update_timer.start()

        # Timer auto-save markers (ogni 30 secondi)
        self.marker_autosave_timer = QTimer()
        self.marker_autosave_timer.setInterval(30000)  # 30 secondi
        self.marker_autosave_timer.timeout.connect(self.autosave_markers)
        self.marker_autosave_timer.start()
        
        # --- Gestione Thread Esportazione ---
        self.export_thread: Optional[QThread] = None
        self.exporter: Optional[VideoExporter] = None
        # ------------------------------------

        # Setup UI
        self.setup_ui()
        self.setup_shortcuts()

        # Applica stile
        self.setStyleSheet(get_main_stylesheet())

        # Imposta visibilità iniziale dei controlli (sincronizzazione attiva di default)
        # I controlli globali partono nascosti, si mostrano solo con sincronizzazione attiva
        # Verrà gestito dal primo toggle_sync chiamato all'avvio

        # Auto-load video dalle cartelle Feed
        QTimer.singleShot(500, self.auto_load_videos)

        logger.log_user_action("Finestra principale creata", "Fase 1 avviata")

    def setup_ui(self):
        """Configura l'interfaccia utente."""
        central_widget = QWidget()
        self.setCentralWidget(central_widget)

        # Layout principale con margini per il bordo
        main_layout = QVBoxLayout(central_widget)
        main_layout.setContentsMargins(2, 2, 2, 2)  # Spazio per il bordo giallo
        main_layout.setSpacing(0)

        # Stile del central widget con bordo
        central_widget.setStyleSheet("""
            QWidget {
                background-color: #d4a356;
            }
        """)

        # Container interno (tutto il contenuto)
        inner_container = QWidget()
        inner_container.setStyleSheet("""
            QWidget {
                background-color: #0d0d0d;
            }
        """)
        inner_layout = QVBoxLayout(inner_container)
        inner_layout.setContentsMargins(0, 0, 0, 0)
        inner_layout.setSpacing(0)

        # Barra del titolo personalizzata
        title_bar = self.create_custom_title_bar()
        inner_layout.addWidget(title_bar)

        # Contenuto principale
        content_widget = QWidget()
        content_layout = QVBoxLayout(content_widget)
        content_layout.setContentsMargins(10, 10, 10, 10)
        content_layout.setSpacing(10)

        # Griglia video 2x2 (header rimosso, ora è nella title bar)
        video_grid = self.create_video_grid()
        content_layout.addLayout(video_grid, 1)

        # Timeline widget con markers
        self.timeline_controls = TimelineControlWidget()
        self.timeline_controls.add_marker_requested.connect(self.add_marker_at_current_position)
        self.timeline_controls.prev_marker_requested.connect(self.go_to_previous_marker)
        self.timeline_controls.next_marker_requested.connect(self.go_to_next_marker)
        # --- MODIFICA CONNESSIONE ESPORTA ---
        self.timeline_controls.export_markers_requested.connect(self.start_export_process)
        # ------------------------------------
        content_layout.addWidget(self.timeline_controls)

        self.timeline_widget = TimelineWidget()
        self.timeline_widget.set_marker_manager(self.marker_manager)
        self.timeline_widget.timeline_clicked.connect(self.seek_to_timestamp) # <--- Segnale per SYNC ON
        self.timeline_widget.marker_clicked.connect(self.on_marker_clicked)
        # self.timeline_widget.marker_moved.connect(self.on_marker_moved) # Rimosso (no drag)
        # Connetti il nuovo segnale per la rimozione dalla timeline globale
        self.timeline_widget.marker_remove_requested.connect(self.on_marker_remove_requested)
        content_layout.addWidget(self.timeline_widget)

        # Controlli globali
        controls = self.create_global_controls()
        content_layout.addWidget(controls)

        # Controlli frame-by-frame (nascosti inizialmente)
        self.frame_controls_widget = self.create_frame_controls()
        self.frame_controls_widget.hide()
        content_layout.addWidget(self.frame_controls_widget)

        # Aggiungi content al container interno
        inner_layout.addWidget(content_widget)

        # Aggiungi container interno al layout principale (con bordo)
        main_layout.addWidget(inner_container)

    def create_custom_title_bar(self):
        """Crea una barra del titolo personalizzata."""
        title_bar = DraggableTitleBar(self)
        title_bar.setObjectName("customTitleBar")
        title_bar.setFixedHeight(40)
        title_bar.setStyleSheet("""
            QWidget#customTitleBar {
                background-color: #0d0d0d;
                border-bottom: 2px solid #4a9f5e;
            }
            QPushButton {
                background-color: transparent;
                border: none;
                color: #c0c0c0;
                font-size: 14px;
                font-weight: bold;
                padding: 0px 12px;
            }
            QPushButton:hover {
                background-color: #2d2d2d;
            }
            QPushButton#closeButton:hover {
                background-color: #cc5555;
                color: #ffffff;
            }
            QLabel#titleLabel {
                color: #4a9f5e;
                font-size: 14px;
                font-weight: bold;
                padding-left: 15px;
            }
        """)

        layout = QHBoxLayout(title_bar)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        # Icona e titolo (QLabel normale - event filter gestirà il drag)
        title_label = QLabel("⚡ SYNCVIEW - TACTICAL MULTI-VIDEO ANALYSIS")
        title_label.setObjectName("titleLabel")
        # Installa event filter della title bar su questo label
        title_label.installEventFilter(title_bar)
        layout.addWidget(title_label)

        # Spazio draggable centrale (QWidget normale - event filter gestirà il drag)
        from PyQt6.QtWidgets import QSizePolicy
        drag_spacer = QWidget()
        drag_spacer.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed)
        # Installa event filter della title bar su questo spacer
        drag_spacer.installEventFilter(title_bar)
        layout.addWidget(drag_spacer, 1)

        # Status indicator (QLabel normale - event filter gestirà il drag)
        self.status_indicator = QLabel("● SISTEMA OPERATIVO")
        self.status_indicator.setStyleSheet("color: #5fa373; font-weight: bold; font-size: 12px; padding-right: 15px;")
        # Imposta tooltip dinamico con stato dipendenze
        self.status_indicator.setToolTip(generate_dependency_tooltip())
        # Installa event filter della title bar su questo label
        self.status_indicator.installEventFilter(title_bar)
        layout.addWidget(self.status_indicator)

        # Pulsanti controllo finestra
        # Minimizza
        self.minimize_button = QPushButton("−")
        self.minimize_button.setFixedSize(45, 40)
        self.minimize_button.setToolTip("Minimizza")
        self.minimize_button.clicked.connect(self.showMinimized)
        layout.addWidget(self.minimize_button)

        # Massimizza/Ripristina
        self.maximize_button = QPushButton("□")
        self.maximize_button.setFixedSize(45, 40)
        self.maximize_button.setToolTip("Massimizza/Ripristina")
        self.maximize_button.clicked.connect(self.toggle_maximize)
        layout.addWidget(self.maximize_button)

        # Chiudi
        self.close_button = QPushButton("✕")
        self.close_button.setObjectName("closeButton")
        self.close_button.setFixedSize(45, 40)
        self.close_button.setToolTip("Chiudi")
        self.close_button.clicked.connect(self.close)
        layout.addWidget(self.close_button)

        return title_bar

    def create_video_grid(self):
        """Crea la griglia 2x2 dei player video."""
        grid = QGridLayout()
        grid.setSpacing(10)

        # Crea 4 player video in griglia 2x2
        for i in range(4):
            player = VideoPlayerWidget(i, self)
            self.video_players.append(player)

            # Imposta il marker manager su ogni player
            player.set_marker_manager(self.marker_manager)

            # Collega il segnale di seek dell'utente per la sincronizzazione (SYNC OFF)
            player.user_seeked.connect(self.on_video_seeked)

            # Collega il segnale di focus per tracciare quale video è selezionato
            player.video_focused.connect(self.on_video_focused) # <--- Segnale per SYNC OFF

            # Collega il segnale di aggiunta marker dal singolo video
            player.add_marker_requested.connect(self.on_video_marker_requested)

            # Collega il segnale di rimozione marker (doppio click) dalla timeline *individuale*
            player.timeline_widget.marker_remove_requested.connect(self.on_marker_remove_requested)

            row = i // 2
            col = i % 2
            grid.addWidget(player, row, col)

        return grid

    def create_global_controls(self):
        """Crea i controlli globali."""
        group_box = QGroupBox("Controlli Globali")
        layout = QVBoxLayout(group_box)

        # Prima riga: Controlli riproduzione
        playback_layout = QHBoxLayout()

        # Riorganizzazione: Inizio | Play/Pause | Fine
        self.to_start_button = QPushButton("⏮")
        self.to_start_button.setToolTip("Vai all'inizio (Home)")
        self.to_start_button.clicked.connect(self.global_to_start)
        playback_layout.addWidget(self.to_start_button)

        # Pulsante Play/Pausa unificato
        self.play_pause_button = QPushButton("▶ PLAY")
        self.play_pause_button.setObjectName("playButton")
        self.play_pause_button.setToolTip("Play/Pausa (Spazio)")
        self.play_pause_button.clicked.connect(self.toggle_play_pause_global)
        playback_layout.addWidget(self.play_pause_button)

        self.to_end_button = QPushButton("⏭")
        self.to_end_button.setToolTip("Vai alla fine (End)")
        self.to_end_button.clicked.connect(self.global_to_end)
        playback_layout.addWidget(self.to_end_button)

        playback_layout.addSpacing(20)

        # Audio master
        self.master_mute_button = QPushButton("🔊 AUDIO MASTER")
        self.master_mute_button.setCheckable(True)
        self.master_mute_button.setToolTip("Audio Master (M)")
        self.master_mute_button.clicked.connect(self.toggle_master_audio)
        playback_layout.addWidget(self.master_mute_button)

        playback_layout.addStretch()

        layout.addLayout(playback_layout)

        # Seconda riga: FPS e Sincronizzazione
        settings_layout = QHBoxLayout()

        # FPS selector
        fps_label = QLabel("Impostazione FPS:") # Etichetta modificata
        settings_layout.addWidget(fps_label)

        self.fps_combo = QComboBox()
        self.fps_combo.addItems(DEFAULT_FPS_OPTIONS)
        self.fps_combo.setCurrentText("Auto")
        self.fps_combo.setToolTip("Seleziona FPS (non influenza la velocità)") # Tooltip aggiornato
        self.fps_combo.currentTextChanged.connect(self.on_fps_changed)
        settings_layout.addWidget(self.fps_combo)

        settings_layout.addSpacing(20)

        # Sync checkbox
        self.sync_checkbox = QCheckBox("Sincronizzazione Attiva")
        self.sync_checkbox.setChecked(True)
        self.sync_checkbox.setToolTip("Attiva/Disattiva Sincronizzazione (Ctrl+S)")
        self.sync_checkbox.stateChanged.connect(self.toggle_sync)
        settings_layout.addWidget(self.sync_checkbox)

        # Inizializza la visibilità dei controlli in base allo stato della checkbox
        # Chiamiamo toggle_sync manually per impostare lo stato iniziale
        QTimer.singleShot(0, lambda: self.toggle_sync(Qt.CheckState.Checked.value))

        # Resync button (visibile solo quando sync è off)
        self.resync_button = QPushButton("🔄 SINCRONIZZA TUTTO")
        self.resync_button.clicked.connect(self.resync_all)
        self.resync_button.hide()
        settings_layout.addWidget(self.resync_button)

        settings_layout.addSpacing(20)

        # Frame mode checkbox
        self.frame_mode_checkbox = QCheckBox("Modalità Frame")
        self.frame_mode_checkbox.setToolTip("Attiva/Disattiva Modalità Frame (Ctrl+F)")
        self.frame_mode_checkbox.stateChanged.connect(self.toggle_frame_mode)
        settings_layout.addWidget(self.frame_mode_checkbox)

        settings_layout.addStretch()

        # Guida button
        help_button = QPushButton("❓ GUIDA")
        help_button.setToolTip("Mostra Guida (F1)")
        help_button.clicked.connect(self.show_help)
        settings_layout.addWidget(help_button)

        layout.addLayout(settings_layout)

        return group_box

    def create_frame_controls(self):
        """Crea i controlli per la modalità frame-by-frame."""
        group_box = QGroupBox("🎞 CONTROLLI FRAME-BY-FRAME")
        layout = QHBoxLayout(group_box)
        layout.setContentsMargins(10, 15, 10, 10)
        layout.setSpacing(10)

        # Info label
        info_label = QLabel("Modalità Frame Attiva - Usa i controlli per navigare frame per frame")
        info_label.setStyleSheet("color: #d4a356; font-size: 12px;")
        layout.addWidget(info_label)

        layout.addStretch()

        # Pulsanti frame
        # Indietro 10 frame
        self.back_10_frames_btn = QPushButton("◀◀ -10 Frame")
        self.back_10_frames_btn.setToolTip("Torna indietro di 10 frame (Shift + ←)")
        self.back_10_frames_btn.clicked.connect(lambda: self.step_frames(-10))
        layout.addWidget(self.back_10_frames_btn)

        # Indietro 1 frame
        self.back_1_frame_btn = QPushButton("◀ -1 Frame")
        self.back_1_frame_btn.setToolTip("Torna indietro di 1 frame (←)")
        self.back_1_frame_btn.clicked.connect(lambda: self.step_frames(-1))
        layout.addWidget(self.back_1_frame_btn)

        # Frame step selector
        step_label = QLabel("Step:")
        layout.addWidget(step_label)

        self.frame_step_combo = QComboBox()
        self.frame_step_combo.addItems(["40ms (25fps)", "33ms (30fps)", "100ms", "200ms"])
        self.frame_step_combo.setCurrentIndex(0)
        self.frame_step_combo.setToolTip("Dimensione dello step per frame")
        layout.addWidget(self.frame_step_combo)

        # Avanti 1 frame
        self.forward_1_frame_btn = QPushButton("+1 Frame ▶")
        self.forward_1_frame_btn.setToolTip("Avanza di 1 frame (→)")
        self.forward_1_frame_btn.clicked.connect(lambda: self.step_frames(1))
        layout.addWidget(self.forward_1_frame_btn)

        # Avanti 10 frame
        self.forward_10_frames_btn = QPushButton("+10 Frame ▶▶")
        self.forward_10_frames_btn.setToolTip("Avanza di 10 frame (Shift + →)")
        self.forward_10_frames_btn.clicked.connect(lambda: self.step_frames(10))
        layout.addWidget(self.forward_10_frames_btn)

        layout.addStretch()

        return group_box

    def setup_shortcuts(self):
        """Configura le scorciatoie da tastiera."""
        # Spazio per Play/Pause
        from PyQt6.QtGui import QShortcut

        play_pause_shortcut = QShortcut(QKeySequence(Qt.Key.Key_Space), self)
        play_pause_shortcut.activated.connect(self.toggle_play_pause)

        # Ctrl+O per caricare video
        load_shortcut = QShortcut(QKeySequence("Ctrl+O"), self)
        load_shortcut.activated.connect(self.load_videos_dialog)

        # F1 per guida
        help_shortcut = QShortcut(QKeySequence("F1"), self)
        help_shortcut.activated.connect(self.show_help)

        # Ctrl+S per toggle sync
        sync_shortcut = QShortcut(QKeySequence("Ctrl+S"), self)
        sync_shortcut.activated.connect(lambda: self.sync_checkbox.toggle())

        # Ctrl+F per toggle frame mode
        frame_shortcut = QShortcut(QKeySequence("Ctrl+F"), self)
        frame_shortcut.activated.connect(lambda: self.frame_mode_checkbox.toggle())

        # Rimosso duplicato per Spazio
        # play_pause_shortcut = QShortcut(QKeySequence(Qt.Key.Key_Space), self)
        # play_pause_shortcut.activated.connect(self.toggle_play_pause)

        # Home/End per inizio/fine
        home_shortcut = QShortcut(QKeySequence(Qt.Key.Key_Home), self)
        home_shortcut.activated.connect(self.global_to_start)

        end_shortcut = QShortcut(QKeySequence(Qt.Key.Key_End), self)
        end_shortcut.activated.connect(self.global_to_end)

        # M per audio master
        mute_shortcut = QShortcut(QKeySequence("M"), self)
        mute_shortcut.activated.connect(lambda: self.master_mute_button.click())

        # Frecce sinistra/destra per frame-by-frame (quando modalità frame è attiva)
        left_arrow_shortcut = QShortcut(QKeySequence(Qt.Key.Key_Left), self)
        left_arrow_shortcut.activated.connect(self.on_left_arrow_pressed)

        right_arrow_shortcut = QShortcut(QKeySequence(Qt.Key.Key_Right), self)
        right_arrow_shortcut.activated.connect(self.on_right_arrow_pressed)

        # Shift + Frecce per 10 frame
        shift_left_shortcut = QShortcut(QKeySequence("Shift+Left"), self)
        shift_left_shortcut.activated.connect(lambda: self.step_frames(-10) if self.frame_mode_enabled else None)

        shift_right_shortcut = QShortcut(QKeySequence("Shift+Right"), self)
        shift_right_shortcut.activated.connect(lambda: self.step_frames(10) if self.frame_mode_enabled else None)

        # ===== MARKER SHORTCUTS =====
        # Ctrl+M per aggiungere marker
        add_marker_shortcut = QShortcut(QKeySequence("Ctrl+M"), self)
        add_marker_shortcut.activated.connect(self.add_marker_at_current_position)

        # P per marker precedente
        prev_marker_shortcut = QShortcut(QKeySequence("P"), self)
        prev_marker_shortcut.activated.connect(self.go_to_previous_marker)

        # N per marker successivo
        next_marker_shortcut = QShortcut(QKeySequence("N"), self)
        next_marker_shortcut.activated.connect(self.go_to_next_marker)

        # Ctrl+E per export video da markers
        export_markers_shortcut = QShortcut(QKeySequence("Ctrl+E"), self)
        # --- MODIFICA CONNESSIONE ESPORTA ---
        export_markers_shortcut.activated.connect(self.start_export_process)
        # ------------------------------------

    def on_left_arrow_pressed(self):
        """Gestisce la pressione della freccia sinistra."""
        if self.frame_mode_enabled:
            self.step_frames(-1)

    def on_right_arrow_pressed(self):
        """Gestisce la pressione della freccia destra."""
        if self.frame_mode_enabled:
            self.step_frames(1)

    def update_dependency_status(self):
        """Aggiorna il tooltip del sistema con lo stato delle dipendenze."""
        self.status_indicator.setToolTip(generate_dependency_tooltip())
        
        # Aggiorna anche il colore in base allo stato
        deps = check_dependencies()
        if deps['all_ok']:
            self.status_indicator.setStyleSheet("color: #5fa373; font-weight: bold; font-size: 12px; padding-right: 15px;")
        else:
            self.status_indicator.setStyleSheet("color: #f5a623; font-weight: bold; font-size: 12px; padding-right: 15px;")

    def auto_load_videos(self):
        """Carica automaticamente i video dalle cartelle Feed-X."""
        logger.log_user_action("Auto-caricamento video", "Ricerca nelle cartelle Feed")

        for i, source_dir in enumerate(SOURCE_DIRS):
            if source_dir.exists():
                # Cerca il primo file video nella directory
                video_files = []
                for fmt in SUPPORTED_VIDEO_FORMATS:
                    video_files.extend(list(source_dir.glob(f"*{fmt}")))

                if video_files:
                    # Carica il primo video trovato
                    self.video_players[i].load_video(video_files[0])
                    logger.log_user_action(f"Video auto-caricato",
                                         f"Feed-{i+1}: {video_files[0].name}")

    def load_videos_dialog(self):
        """Apre un dialogo per caricare video manualmente."""
        file_filter = "Video Files (" + " ".join(f"*{fmt}" for fmt in SUPPORTED_VIDEO_FORMATS) + ")"
        files, _ = QFileDialog.getOpenFileNames(
            self,
            "Seleziona video da caricare",
            str(Path.home()),
            file_filter
        )

        if files:
            for i, file_path in enumerate(files[:4]):  # Max 4 video
                self.video_players[i].load_video(file_path)

    # Controlli globali
    def toggle_play_pause_global(self):
        """Alterna tra play e pausa per tutti i video."""
        # Verifica lo stato del primo player caricato
        is_playing = False
        for player in self.video_players:
            if player.is_loaded:
                from PyQt6.QtMultimedia import QMediaPlayer
                if player.media_player.playbackState() == QMediaPlayer.PlaybackState.PlayingState:
                    is_playing = True
                break

        if is_playing:
            # Pausa tutti
            logger.log_user_action("Pausa globale")
            for player in self.video_players:
                if player.is_loaded:
                    player.pause()
            self.play_pause_button.setText("▶ PLAY")
            self.play_pause_button.setObjectName("playButton")
        else:
            # Play tutti
            logger.log_user_action("Play globale")
            for player in self.video_players:
                if player.is_loaded:
                    # --- MODIFICA: Assicura che la velocità sia 1.0x quando si preme Play ---
                    # Questo previene che una selezione FPS precedente (ora scollegata)
                    # rimanga attiva se il rate era diverso da 1.0
                    player.set_playback_rate(1.0)
                    player.play()
            self.play_pause_button.setText("⏸ PAUSA")
            self.play_pause_button.setObjectName("pauseButton")
            # Aggiorna anche current_playback_rate interno
            self.current_playback_rate = 1.0
            # Resetta il combo box a "Auto" senza emettere segnale
            self.fps_combo.blockSignals(True)
            self.fps_combo.setCurrentText("Auto")
            self.fps_combo.blockSignals(False)


        # Riapplica lo stile
        style = self.play_pause_button.style()
        if style:
            style.unpolish(self.play_pause_button)  # type: ignore[attr-defined]
            style.polish(self.play_pause_button)  # type: ignore[attr-defined]

    def global_to_start(self):
        """Porta tutti i video all'inizio."""
        logger.log_user_action("Vai all'inizio")
        for player in self.video_players:
            if player.is_loaded:
                player.seek_position(0)

    def global_to_end(self):
        """Porta tutti i video alla fine."""
        logger.log_user_action("Vai alla fine")
        for player in self.video_players:
            if player.is_loaded:
                duration = player.get_duration()
                player.seek_position(duration)

    def toggle_play_pause(self):
        """Alterna tra play e pausa (chiamata da scorciatoia)."""
        self.toggle_play_pause_global()

    def toggle_master_audio(self):
        """Attiva/disattiva l'audio master."""
        is_muted = self.master_mute_button.isChecked()
        logger.log_user_action("Audio master", "Muto" if is_muted else "Attivo")

        for player in self.video_players:
            if player.is_loaded:
                player.set_muted(is_muted)

        self.master_mute_button.setText("🔇 AUDIO MUTO" if is_muted else "🔊 AUDIO MASTER")

    # --- MODIFICA: on_fps_changed ora non imposta il playbackRate ---
    def on_fps_changed(self, fps_text):
        """Gestisce il cambio di selezione FPS (non influenza la velocità)."""
        logger.log_user_action("Selezione FPS cambiata", fps_text)

        # Se si seleziona "Personalizzato", apri il dialog
        if fps_text == "Personalizzato":
            custom_fps = FPSDialog.get_custom_fps(self.custom_fps, self)
            if custom_fps is not None:
                self.custom_fps = custom_fps
                logger.log_user_action("FPS Personalizzato selezionato", f"{custom_fps:.3f} fps")
                # Qui potremmo salvare questo valore per usarlo altrove (es. export)
            else:
                # L'utente ha annullato, reimposta il combo box a "Auto"
                self.fps_combo.blockSignals(True)
                self.fps_combo.setCurrentText("Auto")
                self.fps_combo.blockSignals(False)

        # Altrimenti, semplicemente registra la selezione
        # Nessuna modifica al playbackRate
        # self.current_playback_rate rimane 1.0
        # for player in self.video_players:
        #     if player.is_loaded:
        #         player.set_playback_rate(1.0) # Assicura che sia sempre 1.0x


    def toggle_sync(self, state):
        """Attiva/disattiva la sincronizzazione."""
        self.sync_enabled = state == Qt.CheckState.Checked.value
        self.sync_manager.set_sync_enabled(self.sync_enabled)

        if self.sync_enabled:
            # Nascondi controlli individuali
            for player in self.video_players:
                player.show_controls(False)

            # Nascondi pulsante resync
            self.resync_button.hide()

            # Mostra controlli globali
            self.play_pause_button.show()
            self.to_start_button.show()
            self.to_end_button.show()
            self.master_mute_button.show()

            # Mostra timeline centrale, nascondi timeline individuali
            self.timeline_controls.show()
            self.timeline_widget.show()
            for player in self.video_players:
                player.hide_timeline()
        else:
            # Mostra controlli individuali
            for player in self.video_players:
                player.show_controls(True)

            # Mostra pulsante resync
            self.resync_button.show()

            # Nascondi controlli globali
            self.play_pause_button.hide()
            self.to_start_button.hide()
            self.to_end_button.hide()
            self.master_mute_button.hide()

            # Nascondi timeline centrale, mostra timeline individuali
            self.timeline_controls.hide()
            self.timeline_widget.hide()
            for player in self.video_players:
                player.show_timeline()

    # --- MODIFICA: Aggiunto aggiornamento forzato delle timeline ---
    def resync_all(self):
        """Risincronizza tutti i video alla posizione del video master."""
        logger.log_user_action("Risincronizzazione manuale")

        master_index = self.sync_manager.get_master_video_index()
        master_player = self.video_players[master_index]

        if master_player.is_loaded:
            master_position = master_player.get_position()
            self.sync_manager.sync_all_to_master(master_position, self.video_players)

            # --- CORREZIONE: Passa master_position a _force_timeline_updates ---
            self._force_timeline_updates(master_position, master_index)
            # -----------------------------------------------------------------

            QMessageBox.information(self, "Risincronizzazione",
                                  f"Tutti i video risincronizzati al Video {master_index + 1}!")
        else:
            # Cerca il primo video caricato come fallback
            fallback_success = False
            for i, player in enumerate(self.video_players):
                if player.is_loaded:
                    reference_position = player.get_position()
                    self.sync_manager.set_master_video(i)
                    self.sync_manager.sync_all_to_master(reference_position, self.video_players)
                    fallback_success = True

                    # --- CORREZIONE: Passa reference_position e indice i ---
                    self._force_timeline_updates(reference_position, i)
                    # --------------------------------------------------------

                    QMessageBox.information(self, "Risincronizzazione",
                                          f"Video master impostato a Video {i + 1}. Tutti sincronizzati!")
                    break # Esci dal loop dopo aver trovato il fallback

            if not fallback_success:
                 QMessageBox.warning(self, "Errore", "Nessun video caricato da usare come riferimento!")

    # --- MODIFICA: Accetta master_position e master_index ---
    def _force_timeline_updates(self, master_position: int, master_index: int):
        """Forza l'aggiornamento della timeline globale e di quelle individuali
           basandosi sulla posizione del master appena sincronizzata."""
        logger.log_user_action("Forzatura aggiornamento timeline post-sync")

        # 1. Aggiorna la timeline globale usando la posizione del master
        self.timeline_widget.set_position(master_position)
        self.timeline_widget.update() # Forza ridisegno

        # 2. Aggiorna le timeline individuali
        for i, player in enumerate(self.video_players):
            if player.is_loaded:
                # 3. Calcola la posizione sincronizzata target per questo player
                sync_position = self.sync_manager.calculate_sync_position(
                    source_position=master_position,
                    source_index=master_index,
                    target_index=i
                )
                # 4. Imposta la posizione calcolata sulla timeline individuale
                player.timeline_widget.set_position(sync_position)
                player.timeline_widget.update() # Forza ridisegno
                # 5. Aggiorna l'etichetta del tempo con la posizione calcolata
                player.update_time_label(sync_position, player.get_duration())


    def on_video_seeked(self, video_index, position):
        """
        Gestisce quando un utente sposta la timeline di un video.
        Se la sincronizzazione è attiva, sincronizza tutti gli altri video.
        """
        # Questa funzione è chiamata solo dalle timeline individuali (SYNC OFF)
        # La logica di SYNC ON è gestita da seek_to_timestamp

        # --- MODIFICA: Rimuoviamo il check 'if self.sync_enabled:' ---
        # perché questa funzione ora gestisce *solo* il caso SYNC OFF
        # (le timeline individuali sono nascoste in SYNC ON)

        # In modalità SYNC OFF, non facciamo nulla in automatico.
        # L'utente deve premere "SINCRONIZZA TUTTO" manualmente.
        # Il video master è già stato impostato da on_video_focused.
        pass

        # La vecchia logica (sbagliata) è stata rimossa:
        # if self.sync_enabled:
        #     logger.log_user_action(f"Sincronizzazione timeline", f"Video {video_index + 1} -> posizione {position}ms")
        #     for i, player in enumerate(self.video_players):
        #         if i != video_index and player.is_loaded:
        #             sync_position = self.sync_manager.calculate_sync_position(position, video_index, i)
        #             player.seek_position(sync_position, emit_signal=False)

    def toggle_frame_mode(self, state):
        """Attiva/disattiva la modalità frame."""
        self.frame_mode_enabled = state == Qt.CheckState.Checked.value
        logger.log_user_action("Modalità Frame", "ON" if self.frame_mode_enabled else "OFF")

        if self.frame_mode_enabled:
            # Pausa tutti i video
            for player in self.video_players:
                if player.is_loaded:
                    player.pause()
            self.play_pause_button.setText("▶ PLAY")
            self.play_pause_button.setObjectName("playButton")
            style = self.play_pause_button.style()
            if style:
                style.unpolish(self.play_pause_button)  # type: ignore[attr-defined]
                style.polish(self.play_pause_button)  # type: ignore[attr-defined]
            # Mostra controlli frame
            self.frame_controls_widget.show()
        else:
            # Nasconde controlli frame
            self.frame_controls_widget.hide()

    def step_frames(self, frame_count):
        """Muove tutti i video di un numero specifico di frame.

        Args:
            frame_count: Numero di frame da avanzare (positivo) o retrocedere (negativo)
        """
        # Determina lo step in millisecondi dal ComboBox
        step_text = self.frame_step_combo.currentText()
        if "40ms" in step_text:
            step_ms = 40
        elif "33ms" in step_text:
            step_ms = 33
        elif "100ms" in step_text:
            step_ms = 100
        elif "200ms" in step_text:
            step_ms = 200
        else:
            step_ms = 40  # Default

        # Calcola lo spostamento totale
        total_step = step_ms * frame_count

        # Applica a tutti i video caricati
        for i, player in enumerate(self.video_players):
            if player.is_loaded:
                current_pos = player.get_position()
                new_pos = max(0, min(player.get_duration(), current_pos + total_step))
                player.seek_position(new_pos, emit_signal=False)

        # Log azione
        direction = "Avanti" if frame_count > 0 else "Indietro"
        logger.log_user_action(
            f"Step Frame",
            f"{direction} {abs(frame_count)} frame ({abs(total_step)}ms)"
        )

    def show_help(self):
        """Mostra il dialogo della guida - Rimosso per debloat."""
        logger.log_user_action("Guida richiesta")
        QMessageBox.information(
            self,
            "SyncView - Guida",
            "<h3>SyncView v2.5</h3>"
            "<p><b>Controlli:</b></p>"
            "<ul>"
            "<li><b>Spazio:</b> Play/Pausa</li>"
            "<li><b>Home/End:</b> Inizio/Fine</li>"
            "<li><b>← / →:</b> Frame Prec/Succ</li>"
            "<li><b>M:</b> Mute/Unmute</li>"
            "</ul>"
            "<p><i>Per la documentazione completa, vedi README.md</i></p>"
        )

    def show_about(self):
        """Mostra informazioni sull'applicazione."""
        QMessageBox.about(self, "Informazioni su SyncView",
                         """<h2>SyncView</h2>
                         <h3>Tactical Multi-Video Analysis (T-MVA)</h3>
                         <p><b>Versione:</b> 1.0 - Fase 1</p>
                         <p><b>Sistema di analisi multi-video professionale</b></p>
                         <p>Progettato per l'analisi tattica sincronizzata
                         di fino a quattro flussi video.</p>
                         <p><i>© 2025 - Tutti i diritti riservati</i></p>
                         """)

    def toggle_maximize(self):
        """Alterna tra finestra massimizzata e normale."""
        if self.isMaximized():
            self.showNormal()
            self.maximize_button.setText("□")
            self.maximize_button.setToolTip("Massimizza")
        else:
            self.showMaximized()
            self.maximize_button.setText("❐")
            self.maximize_button.setToolTip("Ripristina")

    # --- MODIFICA: Aggiunta logica per impostare Master Video in SYNC OFF ---
    def on_video_focused(self, video_index: int):
        """Gestisce quando un video viene selezionato/cliccato.
        
        Se SYNC è OFF, imposta questo video come master per il resync.

        Args:
            video_index: Indice del video selezionato (0-3)
        """
        self.focused_video_index = video_index
        logger.log_user_action("Video selezionato", f"Video {video_index + 1}")

        # --- NUOVA FUNZIONALITÀ (Task 2) ---
        if not self.sync_enabled:
            # Controlla se il video cliccato è effettivamente caricato
            if self.video_players[video_index].is_loaded:
                # Imposta questo video come master nel sync manager
                self.sync_manager.set_master_video(video_index)
                # (Il log di set_master_video è già gestito da SyncManager)
            else:
                logger.log_user_action("Selezione master fallita", f"Video {video_index + 1} non caricato")


    def on_video_marker_requested(self, video_index: int, position: int):
        """Gestisce la richiesta di aggiunta marker da un singolo video.

        Args:
            video_index: Indice del video da cui arriva la richiesta (0-3)
            position: Posizione in millisecondi
        """
        # Usa colore default blu
        color = MarkerManager.DEFAULT_COLORS['blue']

        # Aggiungi marker specifico per questo video
        marker = self.marker_manager.add_marker(
            timestamp=position,
            color=color,
            category='default',
            video_index=video_index  # Marker specifico per questo video
        )

        logger.log_user_action("Marker aggiunto dal video",
                             f"Video {video_index + 1} @ {position}ms")

        # Aggiorna timeline globale e timeline di ogni video player
        self.timeline_widget.update()
        for player in self.video_players:
            player.update_markers()

    # ==================== MARKER MANAGEMENT ====================

    def add_marker_at_current_position(self):
        """Aggiunge un marker alla posizione corrente del video.

        Se la sincronizzazione è attiva, aggiunge marker su tutti i video.
        Altrimenti, aggiunge solo sul video selezionato/cliccato.
        """
        # Scegli colore (usa default blu)
        color = MarkerManager.DEFAULT_COLORS['blue']

        if self.sync_enabled:
            # Modalità SYNC: aggiungi UN SOLO marker globale
            # Trova il primo video caricato per ottenere la posizione
            position = 0
            has_video = False
            for player in self.video_players:
                # --- FIX: Controlla 'is_loaded' invece di 'source()' ---
                # 'source()' può restituire una QUrl() vuota che valuta a True
                if player.is_loaded:
                    position = player.media_player.position()
                    has_video = True
                    break  # Trovato il primo video caricato

            if has_video:
                # Aggiungi un SOLO marker con video_index=None (globale)
                marker = self.marker_manager.add_marker(
                    timestamp=position,
                    color=color,
                    category='default',
                    video_index=None  # None = marker globale (tutti i video)
                )
                logger.log_user_action("Marker aggiunto (SYNC)", f"Posizione {position}ms")
            else:
                QMessageBox.warning(self, "Attenzione", "Nessun video caricato.")
        else:
            # Modalità SINGOLO: aggiungi marker solo sul video selezionato
            focused_player = self.video_players[self.focused_video_index]

            # --- FIX: Controlla 'is_loaded' ---
            if focused_player.is_loaded:
                position = focused_player.media_player.position()
                marker = self.marker_manager.add_marker(
                    timestamp=position,
                    color=color,
                    category='default',
                    video_index=self.focused_video_index  # Marker specifico per questo video
                )

                logger.log_user_action("Marker aggiunto (SINGOLO)",
                                     f"Video {self.focused_video_index + 1} @ {position}ms")
            else:
                QMessageBox.warning(self, "Attenzione",
                                  f"Il Video {self.focused_video_index + 1} non ha un video caricato.\n"
                                  f"Clicca su un video caricato prima di aggiungere un marker.")

        # Aggiorna timeline globale e timeline di ogni video player
        self.timeline_widget.update()
        for player in self.video_players:
            player.update_markers()

    def go_to_previous_marker(self):
        """Naviga al marker precedente."""
        current_position = 0
        player_found = False
        for player in self.video_players:
            if player.is_loaded: # Controllo 'is_loaded'
                current_position = player.media_player.position()
                player_found = True
                break

        if not player_found:
             logger.log_user_action("Nessun video caricato per determinare la posizione")
             return

        # --- FIX: Aggiungi offset per gestire la posizione esatta del marker ---
        search_position = max(0, current_position - 1) # Cerca da 1ms prima
        marker = self.marker_manager.get_previous_marker(search_position)

        if marker:
            self.seek_to_timestamp(marker.timestamp)
            time_str = self._format_time(marker.timestamp)
            logger.log_user_action("Vai a marker precedente", f"@ {time_str}")
        else:
            logger.log_user_action("Nessun marker precedente")
            # Se non c'è precedente, vai all'inizio
            self.global_to_start()

    def go_to_next_marker(self):
        """Naviga al marker successivo."""
        current_position = 0
        player_found = False
        duration = 0 # Durata massima tra i video caricati

        for player in self.video_players:
            if player.is_loaded: # Controllo 'is_loaded'
                if not player_found:
                    current_position = player.media_player.position()
                    player_found = True
                duration = max(duration, player.get_duration()) # Trova durata massima

        if not player_found:
             logger.log_user_action("Nessun video caricato per determinare la posizione")
             return

        # --- FIX: Aggiungi offset per gestire la posizione esatta del marker ---
        search_position = current_position + 1 # Cerca da 1ms dopo
        marker = self.marker_manager.get_next_marker(search_position)

        if marker:
            self.seek_to_timestamp(marker.timestamp)
            time_str = self._format_time(marker.timestamp)
            logger.log_user_action("Vai a marker successivo", f"@ {time_str}")
        else:
            logger.log_user_action("Nessun marker successivo")
            # Se non c'è successivo, vai alla fine (usando la durata massima)
            if duration > 0:
                self.seek_to_timestamp(duration)


    # --- MODIFICA: Logica di SYNC ON (Task 1) corretta ---
    def seek_to_timestamp(self, timestamp_ms: int):
        """Naviga a un timestamp specifico su tutti i video, calcolando gli offset."""
        
        # 1. Trova il player di riferimento (il primo caricato, come in update_timeline_position)
        reference_player = None
        reference_index = -1
        for idx, player in enumerate(self.video_players):
            if player.is_loaded:
                reference_player = player
                reference_index = idx
                break
        
        # 2. Se non c'è video, non fare nulla
        if reference_index == -1:
            logger.log_user_action("Seek timeline fallito", "Nessun video di riferimento caricato")
            return

        logger.log_user_action(f"Seek timeline (SYNC Globale)", f"{timestamp_ms}ms (Rif: Video {reference_index + 1})")

        # 3. Itera e imposta la posizione calcolata per TUTTI i video
        for i, player in enumerate(self.video_players):
            if player.is_loaded:
                # 4. Calcola la posizione corretta usando il SyncManager
                # Il timestamp cliccato è relativo al player di riferimento
                sync_position = self.sync_manager.calculate_sync_position(
                    source_position=timestamp_ms,
                    source_index=reference_index,
                    target_index=i
                )
                # 5. Imposta la posizione (senza emettere segnale per evitare loop)
                player.seek_position(sync_position, emit_signal=False)


    def on_marker_clicked(self, marker):
        """Gestisce click su un marker."""
        time_str = self._format_time(marker.timestamp)
        logger.log_user_action("Marker cliccato", f"@ {time_str}")
        # Vai al timestamp del marker
        # Usiamo seek_to_timestamp per rispettare gli offset anche quando si clicca un marker
        self.seek_to_timestamp(marker.timestamp)

    # Funzione rimossa (no drag)
    # def on_marker_moved(self, marker, new_timestamp: int):
    #     """Gestisce spostamento di un marker."""
    #     if marker.id:
    #         self.marker_manager.update_marker(marker.id, timestamp=new_timestamp)
    #         self.timeline_widget.update()
    #         logger.log_user_action("Marker spostato", f"-> {new_timestamp}ms")

    def on_marker_remove_requested(self, marker: Marker):
        """Gestisce la richiesta di rimozione di un marker (doppio click).
        
        Comportamento:
        - SYNC ON + Timeline Globale: Rimuove marker globale da tutti i video
        - SYNC ON + Timeline Individuale: Rimuove marker solo da quel video (gli altri mantengono)
        - SYNC OFF + Timeline Individuale: Rimuove marker specifico del video
        - SYNC OFF + Timeline Globale: Operazione ignorata
        """
        if not marker or not marker.id:
            return

        time_str = self._format_time(marker.timestamp)
        sender_widget = self.sender()
        is_global_timeline = (sender_widget == self.timeline_widget)
        
        # Determina da quale video proviene il segnale (se timeline individuale)
        sender_video_index = None
        if not is_global_timeline:
            for i, player in enumerate(self.video_players):
                if player.timeline_widget == sender_widget:
                    sender_video_index = i
                    break
        
        if self.sync_enabled:
            # ============ SYNC ON ============
            if is_global_timeline and marker.video_index is None:
                # Rimozione dalla timeline globale: rimuovi marker globale da tutti i video
                success = self.marker_manager.remove_marker(marker.id)
                if success:
                    logger.log_user_action("Marker rimosso (SYNC ON, Globale)", 
                                         f"ID: {marker.id} @ {time_str} - Rimosso da tutti i video")
                    self.timeline_widget.update()
                    for player in self.video_players:
                        player.update_markers()
                else:
                    logger.log_error("Errore rimozione marker", f"ID: {marker.id} non trovato")
                    
            elif not is_global_timeline and marker.video_index is None and sender_video_index is not None:
                # Rimozione da timeline individuale di un marker globale:
                # Trasforma il marker globale in marker specifici per gli ALTRI video
                
                # Rimuovi il marker globale
                self.marker_manager.remove_marker(marker.id)
                
                # Crea marker specifici per tutti i video TRANNE quello da cui è stato rimosso
                for i in range(4):
                    if i != sender_video_index and self.video_players[i].is_loaded:
                        self.marker_manager.add_marker(
                            timestamp=marker.timestamp,
                            color=marker.color,
                            description=marker.description,
                            category=marker.category,
                            video_index=i  # Marker specifico per questo video
                        )
                
                logger.log_user_action(f"Marker rimosso (SYNC ON, Video {sender_video_index + 1})", 
                                     f"ID: {marker.id} @ {time_str} - Marker globale rimosso da Video {sender_video_index + 1}, mantenuto negli altri")
                
                # Aggiorna tutte le timeline
                self.timeline_widget.update()
                for player in self.video_players:
                    player.update_markers()
            else:
                logger.log_user_action("Rimozione marker ignorata", 
                                     "SYNC ON, marker specifico non rimovibile da timeline individuale")
        else:
            # ============ SYNC OFF ============
            if not is_global_timeline and sender_video_index is not None:
                # Rimuovi marker solo se appartiene a questo video specifico
                if marker.video_index == sender_video_index:
                    success = self.marker_manager.remove_marker(marker.id)
                    if success:
                        logger.log_user_action(f"Marker rimosso (SYNC OFF, Video {sender_video_index + 1})", 
                                             f"ID: {marker.id} @ {time_str}")
                        self.timeline_widget.update()
                        for player in self.video_players:
                            player.update_markers()
                    else:
                        logger.log_error("Errore rimozione marker", f"ID: {marker.id} non trovato")
                elif marker.video_index is None:
                    # Marker globale: stessa logica di SYNC ON + timeline individuale
                    # Rimuovi il marker globale e crea marker specifici per gli altri video
                    self.marker_manager.remove_marker(marker.id)
                    
                    for i in range(4):
                        if i != sender_video_index and self.video_players[i].is_loaded:
                            self.marker_manager.add_marker(
                                timestamp=marker.timestamp,
                                color=marker.color,
                                description=marker.description,
                                category=marker.category,
                                video_index=i
                            )
                    
                    logger.log_user_action(f"Marker globale rimosso (SYNC OFF, Video {sender_video_index + 1})", 
                                         f"ID: {marker.id} @ {time_str} - Rimosso da Video {sender_video_index + 1}, mantenuto negli altri")
                    
                    self.timeline_widget.update()
                    for player in self.video_players:
                        player.update_markers()
                else:
                    logger.log_user_action("Rimozione marker ignorata", 
                                         f"Marker appartiene a un altro video (Video {marker.video_index + 1})")
            elif is_global_timeline:
                logger.log_user_action("Rimozione marker ignorata", 
                                     "SYNC OFF, impossibile rimuovere dalla timeline globale")

    # --- FUNZIONE SOSTITUITA ---
    # def export_video_from_markers(self): ...
    # ---------------------------
    
    # ==================== GESTIONE ESPORTAZIONE (NUOVA) ====================

    def get_reference_player(self) -> Optional[VideoPlayerWidget]:
        """Trova il primo video player caricato da usare come riferimento."""
        for player in self.video_players:
            if player.is_loaded and player.video_path:
                return player
        return None

    def start_export_process(self):
        """
        Avvia il processo di esportazione video.
        Chiamato dal pulsante "Esporta" o shortcut Ctrl+E.
        """
        logger.log_user_action("Esportazione richiesta")

        # 0. Controlla se un'esportazione è già in corso
        if self.export_thread and self.export_thread.isRunning():
            QMessageBox.warning(
                self, 
                "Esportazione in Corso",
                "Un processo di esportazione è già attivo. Attendi il completamento."
            )
            return

        # 1. Controlla se ci sono marker
        if self.marker_manager.count == 0:
            QMessageBox.warning(
                self, 
                "Nessun Marker", 
                "Nessun marker trovato. Aggiungi markers (Ctrl+M) prima di esportare."
            )
            return

        # 2. Raccogli tutti i video caricati
        video_paths = {}  # Dict[int, Path]
        for i, player in enumerate(self.video_players):
            if player.is_loaded and player.video_path:
                video_paths[i] = player.video_path
        
        if not video_paths:
            QMessageBox.warning(
                self, 
                "Nessun Video", 
                "Nessun video caricato. Carica almeno un video prima di esportare."
            )
            return
        
        markers = list(self.marker_manager.markers) # Copia la lista
        
        # 3. Ottieni i secondi N e M dai controlli timeline
        sec_before, sec_after = self.timeline_controls.get_export_times()
        
        # 4. Chiedi all'utente dove salvare le clip
        export_dir = ExportDialog.get_export_directory(EXPORT_DIR, self)
        
        if export_dir is None:
            logger.log_export_action("Esportazione annullata dall'utente")
            return
        
        # 5. Configura il thread e il worker
        self.export_thread = QThread()
        self.exporter = VideoExporter(
            video_paths=video_paths,
            markers=markers,
            sec_before=sec_before,
            sec_after=sec_after,
            export_dir=export_dir
        )
        
        self.exporter.moveToThread(self.export_thread)
        
        # 5. Connetti segnali
        self.export_thread.started.connect(self.exporter.run)
        self.exporter.finished.connect(self.on_export_finished)
        self.exporter.error.connect(self.on_export_error)
        self.exporter.progress.connect(self.on_export_progress)
        
        # Pulisci al termine
        self.exporter.finished.connect(self.export_thread.quit)
        self.exporter.error.connect(self.export_thread.quit)
        self.exporter.destroyed.connect(self.export_thread.deleteLater)
        self.export_thread.finished.connect(self.export_thread.deleteLater)

        # 6. Avvia il thread
        self.export_thread.start()

        # 7. Calcola numero totale di clip (per info all'utente)
        total_clips = 0
        for marker in markers:
            if marker.video_index is None:
                # Marker globale: una clip per ogni video
                total_clips += len(video_paths)
            else:
                # Marker specifico: una clip solo per quel video
                if marker.video_index in video_paths:
                    total_clips += 1
        
        # 8. Notifica l'utente
        self.status_indicator.setText(f"● ESPORTAZIONE IN CORSO...")
        self.status_indicator.setStyleSheet("color: #d4a356;") # Giallo
        
        QMessageBox.information(
            self,
            "Esportazione Avviata",
            f"L'esportazione è iniziata in background.\n"
            f"Markers: {len(markers)}\n"
            f"Video caricati: {len(video_paths)}\n"
            f"Clip totali: ~{total_clips}\n\n"
            f"Le clip verranno salvate in '{export_dir.name}'."
        )

    def on_export_progress(self, message: str):
        """Aggiorna lo status indicator con il progresso."""
        logger.log_export_action("Progresso", message)
        self.status_indicator.setText(f"● {message.upper()}")
        self.status_indicator.setStyleSheet("color: #d4a356;") # Giallo

    def on_export_finished(self, message: str):
        """Chiamato al termine dell'esportazione."""
        logger.log_export_action("Esportazione completata", message)
        self.status_indicator.setText("● SISTEMA OPERATIVO")
        self.status_indicator.setStyleSheet("color: #5fa373;") # Verde
        self.update_dependency_status()  # Aggiorna tooltip
        
        QMessageBox.information(
            self,
            "Esportazione Completata",
            message
        )
        self.cleanup_export_thread()

    def on_export_error(self, error_message: str):
        """Chiamato in caso di errore di esportazione."""
        logger.log_error("Errore esportazione", error_message)
        self.status_indicator.setText("● ERRORE ESPORTAZIONE")
        self.status_indicator.setStyleSheet("color: #cc5555;") # Rosso
        self.update_dependency_status()  # Aggiorna tooltip
        
        QMessageBox.critical(
            self,
            "Errore Esportazione",
            f"Si è verificato un errore:\n\n{error_message}"
        )
        self.cleanup_export_thread()

    def cleanup_export_thread(self):
        """Pulisce i riferimenti al thread e al worker."""
        self.exporter = None
        self.export_thread = None
        # Ripristina status dopo 5 secondi
        QTimer.singleShot(5000, self.reset_status_indicator)

    def reset_status_indicator(self):
        """Resetta lo status indicator allo stato base."""
        # Controlla che non sia in corso un'altra esportazione
        if not (self.export_thread and self.export_thread.isRunning()):
            self.status_indicator.setText("● SISTEMA OPERATIVO")
            self.status_indicator.setStyleSheet("color: #5fa373;") # Verde

    # ===================================================================

    def update_timeline_position(self):
        """Aggiorna la posizione corrente sulla timeline basandosi sul primo video rilevato."""
        # Chiamato dal timer di aggiornamento ogni 100ms

        # Cerca il PRIMO video caricato disponibile nel loop (non sempre il primo slot)
        active_player = None
        active_player_index = -1

        for idx, player in enumerate(self.video_players):
            # Controlla se il player ha un video caricato
            if player.is_loaded: # Controllo 'is_loaded'
                active_player = player
                active_player_index = idx
                break  # Si ferma al primo video trovato

        # Se c'è un video attivo, aggiorna la timeline con i suoi dati
        if active_player:
            position = active_player.media_player.position()
            duration = active_player.media_player.duration()

            # Aggiorna posizione sulla timeline globale
            # Verifica se la timeline globale è visibile (SYNC ON) prima di aggiornare
            if self.sync_enabled:
                 self.timeline_widget.set_position(position)
            # Altrimenti (SYNC OFF), le timeline individuali vengono già aggiornate
            # dal segnale positionChanged del player

            # Imposta durata globale solo se è cambiata
            if duration > 0 and self.timeline_widget.duration_ms != duration:
                self.timeline_widget.set_duration(duration)
                # Log solo quando cambia la durata (evita spam nel log)
                logger.log_user_action(
                    "Durata Timeline Globale aggiornata",
                    f"Basata su Video {active_player_index + 1} ({duration}ms)"
                )

    def autosave_markers(self):
        """Salva automaticamente i markers se modificati."""
        if self.marker_manager.is_modified:
            success = self.marker_manager.save()
            if success:
                logger.log_user_action("Auto-save markers", f"{self.marker_manager.count} markers salvati")

    # ==========================================================

    def _format_time(self, ms: int) -> str:
        """Formatta millisecondi in HH:MM:SS.mmm."""
        total_seconds = ms / 1000
        hours = int(total_seconds // 3600)
        minutes = int((total_seconds % 3600) // 60)
        seconds = int(total_seconds % 60)
        millis = int(ms % 1000)
        return f"{hours:02d}:{minutes:02d}:{seconds:02d}.{millis:03d}"

    def closeEvent(self, event):  # type: ignore[override]
        """Gestisce la chiusura dell'applicazione."""
        logger.log_user_action("Chiusura applicazione")
        
        # --- Interrompi esportazione se in corso ---
        if self.export_thread and self.export_thread.isRunning():
            logger.log_export_action("Interruzione esportazione per chiusura app")
            if self.exporter:
                self.exporter.stop()
            self.export_thread.quit()
            self.export_thread.wait(2000) # Attendi max 2 sec
        # --------------------------------------------

        # Salva markers prima di chiudere
        if self.marker_manager.is_modified:
            self.marker_manager.save()
            logger.log_user_action("Markers salvati", f"{self.marker_manager.count} markers")

        # Ferma tutti i player
        for player in self.video_players:
            if player.is_loaded:
                player.stop()

        event.accept()
        logger.log_user_action("Applicazione chiusa")
