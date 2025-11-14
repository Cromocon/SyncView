"""
Finestra principale di SyncView.
Gestisce la griglia video 2x2 e i controlli globali.
"""

from PyQt6.QtWidgets import (QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
                             QGridLayout, QPushButton, QLabel, QComboBox,
                             QGroupBox, QCheckBox, QMessageBox, QFileDialog, QSizePolicy,
                             QApplication)
from PyQt6.QtCore import Qt, QTimer, QEvent, QThread
from PyQt6.QtGui import QKeySequence, QMouseEvent
from pathlib import Path
from typing import Optional, List, Dict, Any
import sys

from ui.video_player import VideoPlayerWidget
from ui.fps_dialog import FPSDialog
from ui.timeline_widget import TimelineWidget, TimelineControlWidget
from ui.simple_export_dialog import SimpleExportDialog
from core.advanced_exporter import AdvancedVideoExporter
from ui.styles import get_main_stylesheet
from config.settings import DEFAULT_FPS_OPTIONS, SUPPORTED_VIDEO_FORMATS, THEME_COLORS
from config.user_paths import user_path_manager
from core.logger import logger
from core.sync_manager import SyncManager
from core.markers import MarkerManager, Marker
from core.video_loader import AsyncVideoLoader
from core.frame_cache import FrameCacheManager
from core.utils import check_dependencies, generate_dependency_tooltip, format_time
from ui.loading_states import ModalStateManager
from ui.debug_manager import DebugManager, DebugLayoutManager
import gc

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
        self.custom_fps = 25.0
        self.focused_video_index = 0
        self.video_players = []
        
        self.debug_manager = DebugManager(self)

        # Sync Manager
        self.sync_manager = SyncManager()

        # Marker Manager con file di progetto
        project_dir = Path.cwd()
        markers_file = project_dir / "syncview_markers.json"
        self.marker_manager = MarkerManager(project_path=markers_file)

        # Pulisci tutti i marker all'avvio
        self.marker_manager.clear_all()
        logger.log_user_action("Marker puliti", "Avvio applicazione")
        
        # Async Video Loader (lazy loading)
        self.async_loader = AsyncVideoLoader()
        
        # Frame Cache Manager (performance)
        self.cache_manager = FrameCacheManager(cache_size_per_video=50)
        logger.log_user_action("Frame cache manager creato", "Cache abilitata per tutti i video")
        
        # Modal State Manager per operazioni lunghe
        self.modal_manager = ModalStateManager()
        
        # Opzione auto-load
        self.auto_load_enabled = True  # Può essere resa configurabile

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
        
        self.export_thread: Optional[QThread] = None
        self.exporter: Optional[AdvancedVideoExporter] = None

        # Setup UI
        self.setup_ui()
        self.setup_shortcuts()

        # Imposta visibilità iniziale dei controlli (sincronizzazione attiva di default)
        # I controlli globali partono nascosti, si mostrano solo con sincronizzazione attiva
        # Verrà gestito dal primo toggle_sync chiamato all'avvio

        # Auto-load video dalle cartelle Feed (opzionale)
        # Aumentato il delay per dare tempo all'UI di inizializzarsi completamente
        if self.auto_load_enabled:
            QTimer.singleShot(1000, self.auto_load_videos)
            logger.log_user_action("Auto-load abilitato", "Caricamento video in 1 secondo")
        else:
            logger.log_user_action("Auto-load disabilitato", "I video vanno caricati manualmente")

        # Inizializza i controlli in modalità windowed (compatta) all'avvio
        QTimer.singleShot(100, lambda: self._initialize_window_mode())

        logger.log_user_action("Finestra principale creata", "Fase 1 avviata")

    def setup_ui(self):
        """Configura l'interfaccia utente."""
        # Applica stile globale
        self.setStyleSheet(get_main_stylesheet())

        central_widget = QWidget()
        central_widget.setProperty("nickname", "Widget Centrale Principale")
        self.setCentralWidget(central_widget)

        # Layout principale con margini per il bordo
        main_layout = QVBoxLayout(central_widget)
        main_layout.setContentsMargins(2, 2, 2, 2)  # Spazio per il bordo giallo
        DebugLayoutManager().add_layout(main_layout)
        main_layout.setSpacing(0)

        # Stile del central widget con bordo
        central_widget.setObjectName("centralWidget")

        # Container interno (tutto il contenuto)
        inner_container = QWidget()
        inner_container.setProperty("nickname", "Container Interno (Contenuto App)")
        inner_container.setObjectName("innerContainer")

        inner_layout = QVBoxLayout(inner_container)
        inner_layout.setContentsMargins(0, 0, 0, 0)
        DebugLayoutManager().add_layout(inner_layout)
        inner_layout.setSpacing(0)

        # Barra del titolo personalizzata
        title_bar = self.create_custom_title_bar()
        inner_layout.addWidget(title_bar)

        # Contenuto principale - Layout orizzontale: sidebar sinistra + contenuto destro
        content_widget = QWidget()
        content_widget.setProperty("nickname", "Layout Contenuto Principale (Sidebar + Video)")
        content_layout = QHBoxLayout(content_widget)
        content_layout.setContentsMargins(10, 10, 10, 10)
        content_layout.setSpacing(10)
        DebugLayoutManager().add_layout(content_layout)

        # === SIDEBAR SINISTRA CON CONTROLLI (25% della larghezza) ===
        sidebar_widget = QWidget()
        sidebar_widget.setProperty("nickname", "Sidebar Sinistra")
        sidebar_layout = QVBoxLayout(sidebar_widget)
        sidebar_layout.setContentsMargins(0, 0, 0, 0)
        sidebar_layout.setSpacing(10)
        DebugLayoutManager().add_layout(sidebar_layout)
        # Rimosso setFixedWidth per usare stretch factor

        # Controlli globali in sidebar
        controls = self.create_global_controls()
        sidebar_layout.addWidget(controls)

        # Controlli frame-by-frame in sidebar (nascosti inizialmente)
        self.frame_controls_widget = self.create_frame_controls()
        self.frame_controls_widget.hide()
        sidebar_layout.addWidget(self.frame_controls_widget)

        # Timeline controls in sidebar
        self.timeline_controls = TimelineControlWidget()
        self.timeline_controls.setProperty("nickname", "Controlli Timeline (Export/Marker)")
        self.timeline_controls.add_marker_requested.connect(self.add_marker_at_current_position)
        self.timeline_controls.prev_marker_requested.connect(self.go_to_previous_marker)
        self.timeline_controls.next_marker_requested.connect(self.go_to_next_marker)
        self.timeline_controls.export_markers_requested.connect(self.start_export_process)
        sidebar_layout.addWidget(self.timeline_controls)

        sidebar_layout.addStretch()  # Push controls to top

        # Aggiungi sidebar al layout principale con stretch 1 (25% se right ha stretch 3)
        content_layout.addWidget(sidebar_widget, 1)

        # === CONTENUTO DESTRO (VIDEO + TIMELINE) (75% della larghezza) ===
        right_widget = QWidget()
        right_widget.setProperty("nickname", "Area Contenuto Destra (Video e Timeline)")
        right_layout = QVBoxLayout(right_widget)
        right_layout.setContentsMargins(0, 0, 0, 0)
        right_layout.setSpacing(10)
        DebugLayoutManager().add_layout(right_layout)

        # Griglia video 2x2 - ora occupa più spazio verticale
        video_grid = self.create_video_grid()
        right_layout.addLayout(video_grid, 3)  # Stretch factor aumentato

        # Timeline widget con markers
        self.timeline_widget = TimelineWidget()
        self.timeline_widget.setProperty("nickname", "Timeline Globale Sincronizzata")
        self.timeline_widget.set_marker_manager(self.marker_manager)
        self.timeline_widget.timeline_clicked.connect(self.seek_to_timestamp)
        self.timeline_widget.marker_clicked.connect(self.on_marker_clicked)
        self.timeline_widget.marker_remove_requested.connect(self.on_marker_remove_requested)
        right_layout.addWidget(self.timeline_widget)

        # Aggiungi contenuto destro al layout principale con stretch 3 (75%)
        content_layout.addWidget(right_widget, 3)

        # Aggiungi content al container interno
        inner_layout.addWidget(content_widget)

        # Aggiungi container interno al layout principale (con bordo)
        main_layout.addWidget(inner_container)

    def create_custom_title_bar(self):
        """Crea una barra del titolo personalizzata."""
        title_bar = DraggableTitleBar(self)
        title_bar.setObjectName("customTitleBar")
        title_bar.setFixedHeight(40)
        title_bar.setProperty("nickname", "Barra del Titolo Personalizzata")

        layout = QHBoxLayout(title_bar)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)
        DebugLayoutManager().add_layout(layout)

        # Icona e titolo (QLabel normale - event filter gestirà il drag)
        title_label = QLabel("⚡ SYNCVIEW - TACTICAL MULTI-VIDEO ANALYSIS")
        title_label.setObjectName("titleLabel")
        title_label.setProperty("nickname", "Etichetta Titolo App")
        # Installa event filter della title bar su questo label
        title_label.installEventFilter(title_bar)
        layout.addWidget(title_label)

        # Spazio draggable centrale (QWidget normale - event filter gestirà il drag)
        from PyQt6.QtWidgets import QSizePolicy
        drag_spacer = QWidget()
        drag_spacer.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed)
        drag_spacer.setProperty("nickname", "Spazio per Drag")
        # Installa event filter della title bar su questo spacer
        drag_spacer.installEventFilter(title_bar)
        layout.addWidget(drag_spacer, 1)

        # Pulsante Guida (compatto, nella title bar)
        self.help_button_title = QPushButton("❓")
        self.help_button_title.setObjectName("helpButtonTitle")
        self.help_button_title.setFixedSize(35, 40)
        self.help_button_title.setToolTip("Mostra Guida (F1)")
        self.help_button_title.setProperty("nickname", "Pulsante Aiuto (Titolo)")
        self.help_button_title.clicked.connect(self.show_help)
        layout.addWidget(self.help_button_title)

        # Status indicator (QLabel normale - event filter gestirà il drag)
        self.status_indicator = QLabel("● SISTEMA OPERATIVO")
        self.status_indicator.setProperty("nickname", "Indicatore di Stato Sistema")
        self.status_indicator.setObjectName("statusIndicator")
        # Imposta tooltip dinamico con stato dipendenze
        self.status_indicator.setToolTip(generate_dependency_tooltip())
        # Installa event filter della title bar su questo label
        self.status_indicator.installEventFilter(title_bar)
        layout.addWidget(self.status_indicator)

        # Pulsanti controllo finestra
        # Minimizza
        self.minimize_button = QPushButton("−")
        self.minimize_button.setObjectName("minimizeButton")
        self.minimize_button.setFixedSize(45, 40)
        self.minimize_button.setToolTip("Minimizza")
        self.minimize_button.setProperty("nickname", "Pulsante Minimizza")
        self.minimize_button.clicked.connect(self.showMinimized)
        layout.addWidget(self.minimize_button)

        # Massimizza/Ripristina
        self.maximize_button = QPushButton("□")
        self.maximize_button.setObjectName("maximizeButton")
        self.maximize_button.setFixedSize(45, 40)
        self.maximize_button.setToolTip("Massimizza/Ripristina")
        self.maximize_button.setProperty("nickname", "Pulsante Massimizza")
        self.maximize_button.clicked.connect(self.toggle_maximize)
        layout.addWidget(self.maximize_button)

        # Chiudi
        self.close_button = QPushButton("✕")
        self.close_button.setObjectName("closeButton")
        self.close_button.setFixedSize(45, 40)
        self.close_button.setToolTip("Chiudi")
        self.close_button.setProperty("nickname", "Pulsante Chiudi")
        self.close_button.clicked.connect(self.close)
        layout.addWidget(self.close_button)

        return title_bar

    def create_video_grid(self):
        """Crea la griglia 2x2 dei player video."""
        grid = QGridLayout()
        grid.setSpacing(10)
        DebugLayoutManager().add_layout(grid)
        
        # Imposta stretch uniforme per le colonne (50% ciascuna)
        grid.setColumnStretch(0, 1)
        grid.setColumnStretch(1, 1)
        
        # Imposta stretch uniforme per le righe (50% ciascuna)
        grid.setRowStretch(0, 1)
        grid.setRowStretch(1, 1)

        # Crea 4 player video in griglia 2x2
        for i in range(4):
            player = VideoPlayerWidget(i, self)
            player.setProperty("nickname", f"Player Video Slot {i+1}")
            self.video_players.append(player)

            # Imposta il marker manager su ogni player
            player.set_marker_manager(self.marker_manager)
            
            # Imposta l'async loader su ogni player
            player.set_async_loader(self.async_loader)

            # Collega il segnale di seek dell'utente per la sincronizzazione (SYNC OFF)
            player.user_seeked.connect(self.on_video_seeked)

            # Collega il segnale di focus per tracciare quale video è selezionato
            player.video_focused.connect(self.on_video_focused) # <--- Segnale per SYNC OFF

            # Collega il segnale di aggiunta marker dal singolo video
            player.add_marker_requested.connect(self.on_video_marker_requested)

            # Collega il segnale di rimozione marker (doppio click) dalla timeline *individuale*
            player.timeline_widget.marker_remove_requested.connect(self.on_marker_remove_requested)
            
            # Collega il segnale di cambio stato caricamento per aggiornare visibilità controlli
            player.video_load_state_changed.connect(self.on_video_load_state_changed)

            row = i // 2
            col = i % 2
            grid.addWidget(player, row, col)

        return grid

    def create_global_controls(self):
        """Crea i controlli globali per la sidebar."""
        # Crea un widget contenitore per i vari gruppi di controlli
        container_widget = QWidget()
        container_layout = QVBoxLayout(container_widget)
        container_layout.setContentsMargins(0, 0, 0, 0)
        container_layout.setSpacing(10)
        DebugLayoutManager().add_layout(container_layout)

        # === CONTROLLI RIPRODUZIONE ===
        self.playback_group = QGroupBox("Riproduzione")
        self.playback_group.setProperty("nickname", "Gruppo Controlli Riproduzione")
        playback_layout = QVBoxLayout(self.playback_group)
        playback_layout.setSpacing(8)

        # Pulsante Play/Pausa unificato (largo) - salvato in un contenitore per gestire il layout
        self.play_pause_container = QWidget()
        self.play_pause_container.setProperty("nickname", "Contenitore Play/Pausa Globale")
        play_pause_layout = QVBoxLayout(self.play_pause_container)
        play_pause_layout.setContentsMargins(0, 0, 0, 0)
        play_pause_layout.setSpacing(0)
        DebugLayoutManager().add_layout(play_pause_layout)
        
        self.play_pause_button = QPushButton("▶ PLAY")
        self.play_pause_button.setProperty("nickname", "Pulsante Play/Pausa Globale")
        self.play_pause_button.setObjectName("playButton")
        self.play_pause_button.setToolTip("Play/Pausa (Spazio)")
        self.play_pause_button.setMinimumHeight(40)
        self.play_pause_button.clicked.connect(self.toggle_play_pause_global)
        play_pause_layout.addWidget(self.play_pause_button)
        playback_layout.addWidget(self.play_pause_container) # Aggiunto per coerenza

        # Pulsanti navigazione (compatti) - contenitore per layout normale
        self.nav_normal_container = QWidget()
        self.nav_normal_container.setProperty("nickname", "Contenitore Navigazione Normale")
        nav_normal_layout = QHBoxLayout(self.nav_normal_container)
        nav_normal_layout.setContentsMargins(0, 0, 0, 0)
        nav_normal_layout.setSpacing(5)
        DebugLayoutManager().add_layout(nav_normal_layout)
        
        self.to_start_button = QPushButton("⏮")
        self.to_start_button.setProperty("nickname", "Pulsante Vai a Inizio")
        self.to_start_button.setToolTip("Vai all'inizio (Home)")
        self.to_start_button.clicked.connect(self.global_to_start)
        nav_normal_layout.addWidget(self.to_start_button)

        self.to_end_button = QPushButton("⏭")
        self.to_end_button.setProperty("nickname", "Pulsante Vai a Fine")
        self.to_end_button.setToolTip("Vai alla fine (End)")
        self.to_end_button.clicked.connect(self.global_to_end)
        nav_normal_layout.addWidget(self.to_end_button)
        playback_layout.addWidget(self.nav_normal_container)
        
        # Contenitore compatto per frame mode (play/pause e navigazione sulla stessa riga)
        self.nav_compact_container = QWidget()
        self.nav_compact_container.setProperty("nickname", "Contenitore Navigazione Compatta")
        nav_compact_layout = QHBoxLayout(self.nav_compact_container)
        nav_compact_layout.setContentsMargins(0, 0, 0, 0)
        nav_compact_layout.setSpacing(5)
        DebugLayoutManager().add_layout(nav_compact_layout)
        
        self.to_start_button_compact = QPushButton("⏮")
        self.to_start_button_compact.setProperty("nickname", "Pulsante Vai a Inizio (Compatto)")
        self.to_start_button_compact.setObjectName("compactNavButton")
        self.to_start_button_compact.setToolTip("Vai all'inizio (Home)")
        self.to_start_button_compact.setFixedSize(75, 35)
        self.to_start_button_compact.clicked.connect(self.global_to_start)
        nav_compact_layout.addWidget(self.to_start_button_compact)
        
        self.play_pause_button_compact = QPushButton("▶")
        self.play_pause_button_compact.setProperty("nickname", "Pulsante Play/Pausa (Compatto)")
        self.play_pause_button_compact.setObjectName("compactPlayButton")
        self.play_pause_button_compact.setToolTip("Play/Pausa (Spazio)")
        self.play_pause_button_compact.setFixedSize(75, 35)
        self.play_pause_button_compact.clicked.connect(self.toggle_play_pause_global)
        nav_compact_layout.addWidget(self.play_pause_button_compact)
        
        self.to_end_button_compact = QPushButton("⏭")
        self.to_end_button_compact.setProperty("nickname", "Pulsante Vai a Fine (Compatto)")
        self.to_end_button_compact.setObjectName("compactNavButton")
        self.to_end_button_compact.setToolTip("Vai alla fine (End)")
        self.to_end_button_compact.setFixedSize(75, 35)
        self.to_end_button_compact.clicked.connect(self.global_to_end)
        nav_compact_layout.addWidget(self.to_end_button_compact)
        
        playback_layout.addWidget(self.nav_compact_container)
        self.nav_compact_container.hide()

        # Audio master
        self.master_mute_button = QPushButton("🔊 AUDIO MASTER")
        self.master_mute_button.setProperty("nickname", "Pulsante Audio Master")
        self.master_mute_button.setCheckable(True)
        self.master_mute_button.setToolTip("Audio Master (M)")
        self.master_mute_button.clicked.connect(self.toggle_master_audio)
        playback_layout.addWidget(self.master_mute_button)

        container_layout.addWidget(self.playback_group)

        # === IMPOSTAZIONI ===
        settings_group = QGroupBox("Impostazioni")
        settings_group.setProperty("nickname", "Gruppo Impostazioni")
        settings_layout = QVBoxLayout(settings_group)
        settings_layout.setSpacing(8)
        DebugLayoutManager().add_layout(settings_layout)

        # FPS selector (salvato come attributo per nascondere in frame mode)
        self.fps_selector_widget = QWidget()
        self.fps_selector_widget.setProperty("nickname", "Contenitore Selettore FPS")
        fps_layout = QVBoxLayout(self.fps_selector_widget)
        fps_layout.setContentsMargins(0, 0, 0, 0)
        DebugLayoutManager().add_layout(fps_layout)
        fps_label = QLabel("FPS:")
        fps_label.setProperty("nickname", "Etichetta 'FPS:'") # Stile spostato in styles.py
        fps_layout.addWidget(fps_label)

        self.fps_combo = QComboBox()
        self.fps_combo.addItems(DEFAULT_FPS_OPTIONS)
        self.fps_combo.setProperty("nickname", "Selettore FPS")
        self.fps_combo.setCurrentText("Auto")
        self.fps_combo.setToolTip("Seleziona FPS target per riproduzione (adatta la velocità mantenendo la durata)")
        self.fps_combo.currentTextChanged.connect(self.on_fps_changed)
        fps_layout.addWidget(self.fps_combo)
        settings_layout.addWidget(self.fps_selector_widget)

        # Sync checkbox
        self.sync_checkbox = QCheckBox("Sincronizzazione Attiva")
        self.sync_checkbox.setProperty("nickname", "Checkbox Sincronizzazione")
        self.sync_checkbox.setChecked(True)
        self.sync_checkbox.setToolTip("Attiva/Disattiva Sincronizzazione (Ctrl+S)")
        self.sync_checkbox.stateChanged.connect(self.toggle_sync)
        settings_layout.addWidget(self.sync_checkbox)

        # Inizializza la visibilità dei controlli in base allo stato della checkbox
        QTimer.singleShot(0, lambda: self.toggle_sync(Qt.CheckState.Checked.value))

        # Resync button (visibile solo quando sync è off)
        self.resync_button = QPushButton("🔄 SINCRONIZZA")
        self.resync_button.setProperty("nickname", "Pulsante Risincronizza")
        self.resync_button.clicked.connect(self.resync_all)
        self.resync_button.hide()
        settings_layout.addWidget(self.resync_button)
        
        # Fit video button (sempre visibile)
        self.fit_video_button = QPushButton("📐 FIT VIDEO")
        self.fit_video_button.setProperty("nickname", "Pulsante Adatta Video")
        self.fit_video_button.setToolTip("Adatta i video ai container (Ctrl+R)")
        self.fit_video_button.clicked.connect(self.fit_all_videos)
        settings_layout.addWidget(self.fit_video_button)

        # Frame mode checkbox
        self.frame_mode_checkbox = QCheckBox("Modalità Frame")
        self.frame_mode_checkbox.setProperty("nickname", "Checkbox Modalità Frame")
        self.frame_mode_checkbox.setToolTip("Attiva/Disattiva Modalità Frame (Ctrl+F)")
        self.frame_mode_checkbox.stateChanged.connect(self.toggle_frame_mode)
        settings_layout.addWidget(self.frame_mode_checkbox)

        container_layout.addWidget(settings_group)

        return container_widget

    def create_frame_controls(self):
        """Crea i controlli per la modalità frame-by-frame (sidebar)."""
        group_box = QGroupBox("🎞 FRAME-BY-FRAME")
        group_box.setProperty("nickname", "Gruppo Controlli Frame")
        layout = QVBoxLayout(group_box)
        layout.setContentsMargins(10, 15, 10, 10)
        layout.setSpacing(8)
        DebugLayoutManager().add_layout(layout)

        # Info label
        info_label = QLabel("Modalità Frame Attiva")
        info_label.setObjectName("frameModeActiveLabel")
        info_label.setProperty("nickname", "Etichetta Modalità Frame Attiva")
        layout.addWidget(info_label)

        # Frame step selector
        step_layout = QHBoxLayout()
        DebugLayoutManager().add_layout(step_layout)
        step_label = QLabel("Step:")
        step_label.setProperty("nickname", "Etichetta 'Step:'")
        step_layout.addWidget(step_label)

        self.frame_step_combo = QComboBox()
        self.frame_step_combo.addItems(["40ms (25fps)", "33ms (30fps)", "100ms", "200ms"])
        self.frame_step_combo.setProperty("nickname", "Selettore Step Frame")
        self.frame_step_combo.setCurrentIndex(0)
        self.frame_step_combo.setToolTip("Dimensione dello step per frame")
        step_layout.addWidget(self.frame_step_combo)
        layout.addLayout(step_layout)

        # Pulsanti grandi: -10, -1, +1, +10
        buttons_layout = QVBoxLayout()
        DebugLayoutManager().add_layout(buttons_layout)
        buttons_layout.setSpacing(5)

        # Riga 1: -10 e +10
        row1 = QHBoxLayout()
        DebugLayoutManager().add_layout(row1)
        self.back_10_frames_btn = QPushButton("◀◀ -10")
        self.back_10_frames_btn.setProperty("nickname", "Pulsante -10 Frame")
        self.back_10_frames_btn.setToolTip("Torna indietro di 10 frame (Shift + ←)")
        self.back_10_frames_btn.clicked.connect(lambda: self.step_frames(-10))
        row1.addWidget(self.back_10_frames_btn)

        self.forward_10_frames_btn = QPushButton("+10 ▶▶")
        self.forward_10_frames_btn.setProperty("nickname", "Pulsante +10 Frame")
        self.forward_10_frames_btn.setToolTip("Avanza di 10 frame (Shift + →)")
        self.forward_10_frames_btn.clicked.connect(lambda: self.step_frames(10))
        row1.addWidget(self.forward_10_frames_btn)
        buttons_layout.addLayout(row1)

        # Riga 2: -1 e +1
        row2 = QHBoxLayout()
        DebugLayoutManager().add_layout(row2)
        self.back_1_frame_btn = QPushButton("◀ -1")
        self.back_1_frame_btn.setProperty("nickname", "Pulsante -1 Frame")
        self.back_1_frame_btn.setToolTip("Torna indietro di 1 frame (←)")
        self.back_1_frame_btn.clicked.connect(lambda: self.step_frames(-1))
        row2.addWidget(self.back_1_frame_btn)

        self.forward_1_frame_btn = QPushButton("+1 ▶")
        self.forward_1_frame_btn.setProperty("nickname", "Pulsante +1 Frame")
        self.forward_1_frame_btn.setToolTip("Avanza di 1 frame (→)")
        self.forward_1_frame_btn.clicked.connect(lambda: self.step_frames(1))
        row2.addWidget(self.forward_1_frame_btn)
        buttons_layout.addLayout(row2)

        layout.addLayout(buttons_layout)

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
        
        # Ctrl+R per fit video
        fit_shortcut = QShortcut(QKeySequence("Ctrl+R"), self)
        fit_shortcut.activated.connect(self.fit_all_videos)

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
        export_markers_shortcut.activated.connect(self.start_export_process)
        
        # Ctrl+0 per reset zoom su tutti i video
        reset_zoom_shortcut = QShortcut(QKeySequence("Ctrl+0"), self)
        reset_zoom_shortcut.activated.connect(self.reset_all_zoom)

    def reset_all_zoom(self):
        """Resetta lo zoom e pan di tutti i video player."""
        for player in self.video_players:
            if player.is_loaded:
                player.reset_zoom()
        logger.log_user_action("Zoom reset globale", "Tutti i video resettati a 100%")

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
        
        # Aggiorna anche il colore in base allo stato (tramite property per lo stylesheet)
        deps = check_dependencies()
        if deps['all_ok']:
            self.status_indicator.setProperty("status", "ok")
        else:
            self.status_indicator.setProperty("status", "warning")

    def auto_load_videos(self):
        """Carica automaticamente i video dalle ultime path utilizzate."""
        logger.log_user_action("Auto-caricamento video", "Tentativo di carica da ultime path usate")

        # Log di tutti i percorsi salvati (prima del filtro)
        all_paths = [user_path_manager.get_video_path(i) for i in range(4)]
        logger.log_user_action(
            "Percorsi salvati trovati",
            f"Slot 0: {all_paths[0]}, Slot 1: {all_paths[1]}, Slot 2: {all_paths[2]}, Slot 3: {all_paths[3]}"
        )

        # Ottieni le path valide (che esistono ancora)
        valid_paths = user_path_manager.get_valid_video_paths()
        
        if not valid_paths:
            logger.log_user_action("Nessun video da auto-caricare", "Nessun file video trovato o tutti i percorsi sono null")
            # Mostra un messaggio informativo all'utente
            self.status_indicator.setText("● PRONTO - NESSUN VIDEO")
            self.status_indicator.setProperty("status", "ready")
            return
        
        logger.log_user_action(
            f"Video validi trovati: {len(valid_paths)}",
            f"Slot: {list(valid_paths.keys())}"
        )
        
        loaded_count = 0
        for index, video_path in valid_paths.items():
            # Carica il video in modo asincrono
            self.video_players[index].load_video(video_path, async_load=True)
            loaded_count += 1
            logger.log_user_action(
                f"Video auto-caricato (async)",
                f"Player {index+1}: {video_path.name}"
            )
        
        # Aggiorna status indicator con info sul caricamento
        self.status_indicator.setText(f"● CARICAMENTO {loaded_count} VIDEO…")
        self.status_indicator.setProperty("status", "loading")
        
        # Dopo 2 secondi, reimposta lo status normale
        QTimer.singleShot(2000, lambda: self._reset_status_after_autoload(loaded_count))
        
        logger.log_user_action(f"Auto-load completato", f"{loaded_count} video in caricamento")

    def _reset_status_after_autoload(self, loaded_count: int):
        """Reimposta lo status indicator dopo il caricamento automatico."""
        self.status_indicator.setText("● SISTEMA PRONTO")
        self.status_indicator.setProperty("status", "ok")
        logger.log_user_action(f"Auto-load status reset", f"{loaded_count} video caricati")

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
            self.play_pause_button_compact.setText("▶")
            self.play_pause_button_compact.setObjectName("compactPlayButton")
        else:
            # Play tutti
            logger.log_user_action("Play globale")
            for player in self.video_players:
                if player.is_loaded:
                    # Questo previene che una selezione FPS precedente (ora scollegata)
                    # rimanga attiva se il rate era diverso da 1.0
                    player.set_playback_rate(1.0)
                    player.play()
            self.play_pause_button.setText("⏸ PAUSA")
            self.play_pause_button.setObjectName("pauseButton")
            self.play_pause_button_compact.setText("⏸")
            self.play_pause_button_compact.setObjectName("compactPauseButton")
            # Aggiorna anche current_playback_rate interno
            self.current_playback_rate = 1.0
            # Resetta il combo box a "Auto" senza emettere segnale
            self.fps_combo.blockSignals(True)
            self.fps_combo.setCurrentText("Auto")
            self.fps_combo.blockSignals(False)

        # Riapplica lo stile a entrambi i pulsanti
        style = self.play_pause_button.style()
        if style:
            style.unpolish(self.play_pause_button)  # type: ignore[attr-defined]
            style.polish(self.play_pause_button)  # type: ignore[attr-defined]
        style_compact = self.play_pause_button_compact.style()
        if style_compact:
            style_compact.unpolish(self.play_pause_button_compact)  # type: ignore[attr-defined]
            style_compact.polish(self.play_pause_button_compact)  # type: ignore[attr-defined]

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

        self.master_mute_button.setText("🔇 MUTO" if is_muted else "🔊 AUDIO")

    def get_selected_fps(self):
        """Ottiene il valore FPS attualmente selezionato nel combo box.
        
        Returns:
            float: FPS selezionato, o None se "Auto"
        """
        fps_text = self.fps_combo.currentText()
        
        if fps_text == "Auto":
            return None
        elif fps_text == "Personalizzato":
            return self.custom_fps
        else:
            # Estrae il numero dalla stringa (es. "24 fps" -> 24.0)
            try:
                return float(fps_text.replace(" fps", ""))
            except ValueError:
                return None
    
    def on_fps_changed(self, fps_text):
        """Gestisce il cambio di selezione FPS e applica il playback rate appropriato."""
        logger.log_user_action("Selezione FPS cambiata", fps_text)

        # Se si seleziona "Personalizzato", apri il dialog
        if fps_text == "Personalizzato":
            custom_fps = FPSDialog.get_custom_fps(self.custom_fps, self)
            if custom_fps is not None:
                self.custom_fps = custom_fps
                logger.log_user_action("FPS Personalizzato selezionato", f"{custom_fps:.3f} fps")
            else:
                # L'utente ha annullato, reimposta il combo box a "Auto"
                self.fps_combo.blockSignals(True)
                self.fps_combo.setCurrentText("Auto")
                self.fps_combo.blockSignals(False)
                return

        # Applica il playback rate a tutti i video caricati
        target_fps = self.get_selected_fps()
        
        for player in self.video_players:
            if player.is_loaded and player.detected_fps > 0:
                if target_fps is None:
                    # Auto: playback rate = 1.0 (velocità normale)
                    player.set_playback_rate(1.0)
                    logger.log_video_action(player.video_index, "Playback rate", "1.0x (Auto)")
                else:
                    # Calcola il playback rate: native_fps / target_fps
                    # Es: video 24fps, target 60fps -> rate = 24/60 = 0.4 (rallenta)
                    # Es: video 60fps, target 24fps -> rate = 60/24 = 2.5 (accelera)
                    rate = player.detected_fps / target_fps
                    player.set_playback_rate(rate)
                    logger.log_video_action(
                        player.video_index, 
                        "Playback rate adattato per FPS",
                        f"{rate:.3f}x (nativo: {player.detected_fps:.2f}fps -> target: {target_fps:.2f}fps)"
                    )

    def on_video_load_state_changed(self, video_index: int, is_loaded: bool):
        """Gestisce il cambio di stato di caricamento di un video.
        
        Aggiorna la visibilità dei controlli individuali in base allo stato di sync.
        Applica il playback rate basato sul FPS selezionato se il video è appena stato caricato.
        """
        player = self.video_players[video_index]
        self._update_ui_for_state()

        # Se il video è appena stato caricato, applica il playback rate basato sul FPS selezionato
        if is_loaded and player.detected_fps > 0:
            target_fps = self.get_selected_fps()
            if target_fps is None:
                # Auto: mantieni velocità normale
                player.set_playback_rate(1.0)
            else:
                # Applica il rate calcolato
                rate = player.detected_fps / target_fps
                player.set_playback_rate(rate)
                logger.log_video_action(
                    video_index,
                    "Playback rate applicato al caricamento",
                    f"{rate:.3f}x (nativo: {player.detected_fps:.2f}fps -> target: {target_fps:.2f}fps)"
                )
        
        logger.log_video_action(
            video_index,
            "Stato caricamento cambiato",
            f"is_loaded={is_loaded}, sync_enabled={self.sync_enabled}"
        )

    def resync_all(self):
        """Risincronizza tutti i video alla posizione del video master."""
        logger.log_user_action("Risincronizzazione manuale")

        master_index = self.sync_manager.get_master_video_index()
        master_player = self.video_players[master_index]

        if master_player.is_loaded:
            master_position = master_player.get_position()
            self.sync_manager.sync_all_to_master(master_position, self.video_players)

            self._force_timeline_updates(master_position, master_index)

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

                    self._force_timeline_updates(reference_position, i)

                    QMessageBox.information(self, "Risincronizzazione",
                                          f"Video master impostato a Video {i + 1}. Tutti sincronizzati!")
                    break # Esci dal loop dopo aver trovato il fallback

            if not fallback_success:
                 QMessageBox.warning(self, "Errore", "Nessun video caricato da usare come riferimento!")
    
    def fit_all_videos(self):
        """Adatta i container video alle dimensioni native dei video (100% zoom).
        
        Calcola le dimensioni che il video avrebbe al 100% di zoom (senza zoom)
        e adatta il container a quelle dimensioni mantenendo l'aspect ratio.
        """
        logger.log_user_action("Fit video manuale", "Adattamento container alle dimensioni native")
        
        # Forza processamento eventi pending per stabilizzare layout
        QApplication.processEvents()
        
        fitted_count = 0
        for player in self.video_players:
            if player.is_loaded and player.video_width > 0 and player.video_height > 0:
                # Log dimensioni PRIMA dell'adattamento
                widget_width_before = player.video_widget.width()
                widget_height_before = player.video_widget.height()
                viewport_width_before = player.video_widget.viewport().rect().width()
                viewport_height_before = player.video_widget.viewport().rect().height()
                container_height_before = player.video_container.height()
                
                # Calcola la larghezza disponibile per il video (rispettando i margini)
                available_width = player.video_widget.width()
                
                # Calcola l'altezza ideale mantenendo l'aspect ratio del video nativo
                video_aspect_ratio = player.video_width / player.video_height if player.video_height > 0 else 1.0
                ideal_container_height = int(available_width / video_aspect_ratio)
                
                if not self.sync_enabled:
                    # L'altezza dinamica (.height()) può essere 0 se il layout non è finalizzato.
                    # Usiamo i valori fissi definiti nella UI.
                    CONTROLS_FIXED_HEIGHT = 38  # Altezza dei pulsanti di controllo individuali
                    TIMELINE_FIXED_HEIGHT = 80  # Altezza della timeline individuale
                    
                    total_controls_height = CONTROLS_FIXED_HEIGHT + TIMELINE_FIXED_HEIGHT
                    
                    # Sottrai l'altezza dei controlli dall'altezza ideale
                    ideal_container_height -= total_controls_height
                    
                    logger.log_video_action(player.video_index, "Fit video (SYNC OFF)", f"Altezza controlli (fissa): {total_controls_height}px, Altezza video corretta: {ideal_container_height}px")
                
                # Imposta SOLO il minimum height, lascia che il maximum si adatti ai controlli
                player.video_container.setMinimumHeight(ideal_container_height)
                player.video_container.setMaximumHeight(ideal_container_height)
                
                # Rimuovi vincoli dal widget interno per permettere adattamento
                player.video_widget.setMinimumHeight(0)
                player.video_widget.setMaximumHeight(16777215)
                
                # Invalida il layout per forzare il ricalcolo
                player.layout().invalidate()
                player.layout().activate()
                
                # Aggiorna geometry
                player.video_widget.updateGeometry()
                player.video_container.updateGeometry()
                player.updateGeometry()
                
                # Process events per permettere al layout di ricalcolare
                QApplication.processEvents()
                
                # Reset zoom per mostrare il video al 100% (senza zoom)
                player.video_widget.reset_zoom_pan()
                
                # Se i controlli sono visibili (SYNC OFF), ridimensionali
                if player.controls_widget.isVisible():
                    QTimer.singleShot(100, player.resize_controls_to_video)
                
                fitted_count += 1
                
                # Log dimensioni dopo l'adattamento
                widget_width_after = player.video_widget.width()
                widget_height_after = player.video_widget.height()
                viewport_width_after = player.video_widget.viewport().rect().width()
                viewport_height_after = player.video_widget.viewport().rect().height()
                container_height_after = player.video_container.height()
                
                # Calcola percentuale di copertura del container (quanto il viewport copre il container)
                coverage_before = (viewport_height_before / container_height_before * 100) if container_height_before > 0 else 0
                coverage_after = (viewport_height_after / container_height_after * 100) if container_height_after > 0 else 0
                
                logger.log_video_action(
                    player.video_index,
                    "Video adattato",
                    f"Video nativo: {player.video_width}x{player.video_height}, "
                    f"Altezza ideale calcolata: {ideal_container_height}px (aspect ratio: {video_aspect_ratio:.2f}), "
                    f"Container: h{container_height_before}→h{container_height_after}, "
                    f"Widget: {widget_width_before}x{widget_height_before}→{widget_width_after}x{widget_height_after}, "
                    f"Viewport: {viewport_width_before}x{viewport_height_before}→{viewport_width_after}x{viewport_height_after}, "
                    f"Copertura: {coverage_before:.1f}%→{coverage_after:.1f}%"
                )
        
        if fitted_count > 0:
            logger.log_user_action(
                "Fit video completato",
                f"{fitted_count} video adattati"
            )
        else:
            logger.log_user_action("Fit video", "Nessun video caricato da adattare")

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

        # perché questa funzione ora gestisce *solo* il caso SYNC OFF
        # (le timeline individuali sono nascoste in SYNC ON)

        # In modalità SYNC OFF, non facciamo nulla in automatico.
        # L'utente deve premere "SINCRONIZZA TUTTO" manualmente.
        # Il video master è già stato impostato da on_video_focused.
        pass

        # La vecchia logica (sbagliata) è stata rimossa:
        # if self.sync_enabled:
        #     logger.log_user_action(f"Sincronizzazione timeline", f"Video {video_index + 1} -> posizione {position}ms")

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
            step_ms = 40

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

    # ==================== GESTIONE STATO UI CENTRALIZZATA ====================

    def toggle_sync(self, state):
        """Attiva/disattiva la sincronizzazione e aggiorna l'UI."""
        self.sync_enabled = state == Qt.CheckState.Checked.value
        self.sync_manager.set_sync_enabled(self.sync_enabled)
        logger.log_user_action("Sincronizzazione", "ON" if self.sync_enabled else "OFF")
        self._update_ui_for_state()

    def toggle_frame_mode(self, state):
        """Attiva/disattiva la modalità frame e aggiorna l'UI."""
        self.frame_mode_enabled = state == Qt.CheckState.Checked.value
        logger.log_user_action("Modalità Frame", "ON" if self.frame_mode_enabled else "OFF")

        if self.frame_mode_enabled:
            # Pausa tutti i video quando si entra in modalità frame
            for player in self.video_players:
                if player.is_loaded:
                    player.pause()
            
            # Aggiorna stato pulsante compatto
            self.play_pause_button_compact.setText("▶")
            self.play_pause_button_compact.setObjectName("compactPlayButton")
            style = self.play_pause_button_compact.style()
            if style:
                style.unpolish(self.play_pause_button_compact)  # type: ignore[attr-defined]
                style.polish(self.play_pause_button_compact)  # type: ignore[attr-defined]

        self._update_ui_for_state()

    def _update_ui_for_state(self):
        """
        Funzione centralizzata per aggiornare la visibilità di tutti i controlli
        in base allo stato di `sync_enabled` e `frame_mode_enabled`.
        """
        sync_on = self.sync_enabled
        frame_mode_on = self.frame_mode_enabled

        self.timeline_widget.setVisible(sync_on)
        self.resync_button.setVisible(not sync_on)
        self.master_mute_button.setVisible(sync_on)

        # I controlli di timeline (marker ed export) sono visibili solo in sync on
        self.timeline_controls.setVisible(sync_on)

        for player in self.video_players:
            is_loaded = player.is_loaded
            player.show_controls(not sync_on and is_loaded)
            player.timeline_widget.setVisible(not sync_on and is_loaded)
            
            # In SYNC OFF, limita l'altezza del video per far spazio ai controlli
            if not sync_on and is_loaded:
                player_h = player.height()
                max_video_h = int(player_h * 0.8)
                player.video_container.setMaximumHeight(max_video_h)
            else:
                # Rimuovi il limite in SYNC ON
                player.video_container.setMaximumHeight(16777215)

        # Il gruppo Riproduzione è visibile solo se sync è ON
        self.playback_group.setVisible(sync_on)
        # Visibilità generale
        self.frame_controls_widget.setVisible(frame_mode_on)
        self.fps_selector_widget.setVisible(not frame_mode_on)

        # Forza un aggiornamento del layout per applicare le modifiche
        QApplication.processEvents()

    def show_help(self):
        """Mostra il dialogo della guida."""
        logger.log_user_action("Guida richiesta")
        QMessageBox.information(
            self,
            "SyncView - Guida Rapida",
            "<h3>SyncView v2.5 - Guida Rapida</h3>"
            "<p><b>Shortcuts Principali:</b></p>"
            "<ul>"
            "<li><b>Ctrl+O:</b> Carica video</li>"
            "<li><b>Ctrl+M:</b> Aggiungi marker globale</li>"
            "<li><b>Ctrl+E:</b> Esporta clip</li>"
            "<li><b>Ctrl+S:</b> Toggle sincronizzazione</li>"
            "<li><b>Ctrl+F:</b> Toggle modalità frame</li>"
            "<li><b>Ctrl+R:</b> Fit video ai container</li>"
            "<li><b>F11:</b> Toggle fullscreen</li>"
            "</ul>"
            "<p><b>Controlli Video:</b></p>"
            "<ul>"
            "<li><b>Spazio:</b> Play/Pausa</li>"
            "<li><b>Home/End:</b> Vai a Inizio/Fine</li>"
            "<li><b>← / →:</b> Frame Precedente/Successivo</li>"
            "<li><b>M:</b> Mute/Unmute audio</li>"
            "</ul>"
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

    def on_video_focused(self, video_index: int):
        """Gestisce quando un video viene selezionato/cliccato.
        
        Se SYNC è OFF, imposta questo video come master per il resync.

        Args:
            video_index: Indice del video selezionato (0-3)
        """
        self.focused_video_index = video_index
        logger.log_user_action("Video selezionato", f"Video {video_index + 1}")

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
        self.timeline_widget.refresh_markers()  # Rebuild index + repaint
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

        search_position = max(0, current_position - 1) # Cerca da 1ms prima
        marker = self.marker_manager.get_previous_marker(search_position)

        if marker:
            self.seek_to_timestamp(marker.timestamp)
            time_str = format_time(marker.timestamp)
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

        search_position = current_position + 1 # Cerca da 1ms dopo
        marker = self.marker_manager.get_next_marker(search_position)

        if marker:
            self.seek_to_timestamp(marker.timestamp)
            time_str = format_time(marker.timestamp)
            logger.log_user_action("Vai a marker successivo", f"@ {time_str}")
        else:
            logger.log_user_action("Nessun marker successivo")
            # Se non c'è successivo, vai alla fine (usando la durata massima)
            if duration > 0:
                self.seek_to_timestamp(duration)

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
        time_str = format_time(marker.timestamp)
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

        time_str = format_time(marker.timestamp)
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
                    self.timeline_widget.refresh_markers()
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
                self.timeline_widget.refresh_markers()
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
                        self.timeline_widget.refresh_markers()
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
                    
                    self.timeline_widget.refresh_markers()
                    for player in self.video_players:
                        player.update_markers()
                else:
                    logger.log_user_action("Rimozione marker ignorata", 
                                         f"Marker appartiene a un altro video (Video {marker.video_index + 1})")
            elif is_global_timeline:
                logger.log_user_action("Rimozione marker ignorata", 
                                     "SYNC OFF, impossibile rimuovere dalla timeline globale")

    # def export_video_from_markers(self): ...
    
    # ==================== GESTIONE ESPORTAZIONE (NUOVA) ====================

    def get_reference_player(self) -> Optional[VideoPlayerWidget]:
        """Trova il primo video player caricato da usare come riferimento."""
        for player in self.video_players:
            if player.is_loaded and player.video_path:
                return player
        return None

    def start_export_process(self):
        """
        Avvia il processo di esportazione video con sistema semplificato.
        Usa automaticamente export avanzato con FFmpeg e parallelizzazione.
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
        
        # 3.5 VALIDAZIONE MARKER: Controlla se ci sono marker con tempo insufficiente
        problematic_markers = self._validate_markers_for_export(
            markers, video_paths, sec_before, sec_after
        )
        
        if problematic_markers:
            # Mostra warning dialog con i marker problematici
            if not self._show_marker_validation_warning(problematic_markers, sec_before, sec_after):
                # L'utente ha scelto di annullare
                logger.log_export_action("Esportazione annullata - marker con tempo insufficiente")
                return
        
        # 4. Apri dialog semplificato (solo destinazione e qualità)
        config = SimpleExportDialog.get_export_config_simple(self)
        
        if config is None:
            logger.log_export_action("Esportazione annullata dall'utente")
            return
        
        # 5. Calcola numero automatico di worker (CPU count - 1, min 1)
        import multiprocessing as mp
        cpu_count = mp.cpu_count()
        max_workers = max(1, cpu_count - 1)
        
        # 6. Configura thread e worker avanzato con impostazioni automatiche
        self.export_thread = QThread()
        self.exporter = AdvancedVideoExporter(
            video_paths=video_paths,
            markers=markers,
            sec_before=sec_before,
            sec_after=sec_after,
            export_dir=config['directory'],
            quality=config['quality'],
            max_workers=max_workers,
            enable_hardware=True,
            max_retries=3
        )
        
        self.exporter.moveToThread(self.export_thread)
        
        # 7. Connetti segnali
        self.export_thread.started.connect(self.exporter.run)
        self.exporter.finished.connect(self.on_export_finished)
        self.exporter.error.connect(self.on_export_error)
        self.exporter.progress.connect(self.on_export_progress_advanced)
        self.exporter.job_completed.connect(self.on_export_job_completed)
        self.exporter.job_failed.connect(self.on_export_job_failed)
        
        # Pulisci al termine
        self.exporter.finished.connect(self.export_thread.quit)
        self.exporter.error.connect(self.export_thread.quit)
        self.exporter.destroyed.connect(self.export_thread.deleteLater)
        self.export_thread.finished.connect(self.export_thread.deleteLater)

        # 8. Avvia
        self.export_thread.start()
        
        # 9. Disabilita controlli
        self._enter_export_modal_state()
        
        logger.log_export_action(
            "Export avviato",
            f"FFmpeg, {max_workers} workers, {config['quality'].name}, HW: Enabled, Retry: 3"
        )
    
    def _validate_markers_for_export(
        self, 
        markers: List[Marker], 
        video_paths: Dict[int, Path], 
        sec_before: float, 
        sec_after: float
    ) -> List[Dict[str, Any]]:
        """Valida i marker per l'export e ritorna una lista di marker problematici.
        
        Args:
            markers: Lista di marker da validare
            video_paths: Dict di video paths {video_index: Path}
            sec_before: Secondi richiesti prima del marker
            sec_after: Secondi richiesti dopo il marker
            
        Returns:
            Lista di dict con informazioni sui marker problematici:
            {
                'marker': Marker,
                'video_index': int,
                'issue': str ('before'|'after'|'both'),
                'position_sec': float,
                'duration_sec': float,
                'available_before': float,
                'available_after': float
            }
        """
        problematic = []
        
        for marker in markers:
            # Per marker globali (video_index is None), controlla tutti i video
            videos_to_check = video_paths.keys() if marker.video_index is None else [marker.video_index]
            
            for video_idx in videos_to_check:
                if video_idx not in video_paths:
                    continue
                
                # Ottieni durata del video
                player = self.video_players[video_idx]
                if not player.is_loaded:
                    continue
                
                duration_ms = player.get_duration()
                if duration_ms <= 0:
                    continue
                
                duration_sec = duration_ms / 1000.0
                marker_pos_sec = marker.timestamp / 1000.0
                
                # Calcola tempo disponibile prima e dopo
                available_before = marker_pos_sec
                available_after = duration_sec - marker_pos_sec
                
                # Determina il problema
                issue = None
                if available_before < sec_before and available_after < sec_after:
                    issue = 'both'
                elif available_before < sec_before:
                    issue = 'before'
                elif available_after < sec_after:
                    issue = 'after'
                
                if issue:
                    problematic.append({
                        'marker': marker,
                        'video_index': video_idx,
                        'issue': issue,
                        'position_sec': marker_pos_sec,
                        'duration_sec': duration_sec,
                        'available_before': available_before,
                        'available_after': available_after
                    })
        
        return problematic
    
    def _show_marker_validation_warning(
        self, 
        problematic_markers: List[Dict[str, Any]], 
        sec_before: float, 
        sec_after: float
    ) -> bool:
        """Mostra un dialog di warning per i marker problematici.
        
        Args:
            problematic_markers: Lista di marker problematici dal metodo validate
            sec_before: Secondi richiesti prima del marker
            sec_after: Secondi richiesti dopo del marker
            
        Returns:
            True se l'utente vuole procedere comunque, False se annulla
        """
        from PyQt6.QtWidgets import QMessageBox, QTextEdit
        
        # Costruisci messaggio dettagliato
        msg = QMessageBox(self)
        msg.setIcon(QMessageBox.Icon.Warning)
        msg.setWindowTitle("⚠ Marker con Tempo Insufficiente")
        msg.setStandardButtons(
            QMessageBox.StandardButton.Yes | 
            QMessageBox.StandardButton.No
        )
        
        # Testo principale
        count = len(problematic_markers)
        msg.setText(
            f"Trovati {count} marker con tempo insufficiente per i requisiti di export.\n\n"
            f"Requisiti: {sec_before:.1f}s prima + {sec_after:.1f}s dopo del marker\n\n"
            "I clip esportati saranno più corti del previsto."
        )
        
        # Dettagli dei marker problematici
        details = []
        for item in problematic_markers:
            marker = item['marker']
            video_idx = item['video_index']
            issue = item['issue']
            pos_sec = item['position_sec']
            available_before = item['available_before']
            available_after = item['available_after']
            
            # Formatta posizione
            pos_str = self._format_time_for_display(pos_sec)
            
            # Descrizione problema
            if issue == 'both':
                problem = f"Tempo insufficiente PRIMA ({available_before:.2f}s disponibili) e DOPO ({available_after:.2f}s disponibili)"
            elif issue == 'before':
                problem = f"Tempo insufficiente PRIMA del marker (disponibili: {available_before:.2f}s, richiesti: {sec_before:.1f}s)"
            else:  # after
                problem = f"Tempo insufficiente DOPO del marker (disponibili: {available_after:.2f}s, richiesti: {sec_after:.1f}s)"
            
            # Descrizione marker o posizione
            marker_label = marker.description if marker.description else f"Marker @ {pos_str}"
            feed_name = "Tutti i feed" if marker.video_index is None else f"Feed-{video_idx + 1}"
            
            details.append(f"• {marker_label} ({feed_name})\n  Posizione: {pos_str}\n  {problem}\n")
        
        details_text = "\n".join(details)
        msg.setInformativeText("Dettagli marker problematici:")
        msg.setDetailedText(details_text)
        
        # Personalizza pulsanti
        yes_btn = msg.button(QMessageBox.StandardButton.Yes)
        no_btn = msg.button(QMessageBox.StandardButton.No)
        
        if yes_btn:
            yes_btn.setText("Procedi Comunque")
        if no_btn:
            no_btn.setText("Annulla Export")
        
        # Applica stile globale per coerenza
        msg.setStyleSheet(get_main_stylesheet())
        
        logger.log_export_action(
            "Marker validation warning",
            f"{count} marker con tempo insufficiente"
        )
        
        # Mostra dialog e ritorna la scelta
        result = msg.exec()
        return result == QMessageBox.StandardButton.Yes
    
    def _format_time_for_display(self, seconds: float) -> str:
        """Formatta i secondi in formato HH:MM:SS.mmm
        
        Args:
            seconds: Tempo in secondi (può avere decimali)
            
        Returns:
            Stringa formattata come HH:MM:SS.mmm
        """
        hours = int(seconds // 3600)
        minutes = int((seconds % 3600) // 60)
        secs = seconds % 60
        return f"{hours:02d}:{minutes:02d}:{secs:06.3f}"
    
    def _enter_export_modal_state(self):
        """Disabilita controlli durante export."""
        # Disabilita timeline controls che contiene export button e altri controlli
        widgets_to_disable: list[QWidget] = [self.timeline_controls]
        
        # Aggiungi anche i frame step buttons
        widgets_to_disable.append(self.back_10_frames_btn)
        widgets_to_disable.append(self.back_1_frame_btn)
        widgets_to_disable.append(self.forward_1_frame_btn)
        widgets_to_disable.append(self.forward_10_frames_btn)
        
        # Aggiungi anche load button dai video player
        for player in self.video_players:
            widgets_to_disable.append(player.load_button)
            widgets_to_disable.append(player.remove_button)
        
        self.modal_manager.enter_modal_state(
            widgets_to_disable,
            "Export in corso"
        )
        logger.log_user_action("Modal state", "Export iniziato - controlli disabilitati")
        
        # Notifica l'utente
        self.status_indicator.setText(f"● ESPORTAZIONE…")
        self.status_indicator.setProperty("status", "exporting")

    def on_export_progress(self, message: str):
        """Aggiorna lo status indicator con il progresso."""
        logger.log_export_action("Progresso", message)
        self.status_indicator.setText(f"● {message.upper()}")
        self.status_indicator.setProperty("status", "exporting")

    def on_export_finished(self, message: str):
        """Chiamato al termine dell'esportazione."""
        logger.log_export_action("Esportazione completata", message)
        
        # Esci da modal state - riabilita controlli
        self.modal_manager.exit_modal_state()
        logger.log_user_action("Modal state", "Export completato - controlli riabilitati")
        
        self.status_indicator.setText("● SISTEMA PRONTO")
        self.status_indicator.setProperty("status", "ok")
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
        
        # Esci da modal state anche in caso di errore
        self.modal_manager.exit_modal_state()
        logger.log_user_action("Modal state", "Export fallito - controlli riabilitati")
        
        self.status_indicator.setText("● ERRORE EXPORT")
        self.status_indicator.setProperty("status", "error")
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
    
    def on_export_progress_advanced(self, message: str, current: int, total: int):
        """Callback per progresso export avanzato."""
        logger.log_export_action("Progresso", f"{message} ({current}/{total})")
        self.status_indicator.setText(f"● {message}")
        self.status_indicator.setProperty("status", "exporting")
        
    def on_export_job_completed(self, job_id: str):
        """Callback quando un job di export completa."""
        logger.log_export_action("Job completato", job_id)
    
    def on_export_job_failed(self, job_id: str, error_message: str):
        """Callback quando un job di export fallisce."""
        logger.log_export_action(f"Job fallito: {job_id}", error_message)
        # Ripristina status dopo 5 secondi
        QTimer.singleShot(5000, self.reset_status_indicator)

    def reset_status_indicator(self):
        """Resetta lo status indicator allo stato base."""
        # Controlla che non sia in corso un'altra esportazione
        if not (self.export_thread and self.export_thread.isRunning()): # type: ignore
            self.status_indicator.setText("● SISTEMA PRONTO")
            self.status_indicator.setProperty("status", "ok")

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

    def keyPressEvent(self, event):  # type: ignore[override]
        """Gestisce eventi tastiera, incluso F11 per fullscreen."""
        if (event.key() == Qt.Key.Key_D and 
            event.modifiers() == (Qt.KeyboardModifier.ControlModifier | Qt.KeyboardModifier.ShiftModifier)):
            self.debug_manager.toggle()
            event.accept()
        elif event.key() == Qt.Key.Key_F11:
            # Toggle fullscreen
            is_fullscreen_now = self.isFullScreen()
            logger.log_user_action(
                "Toggle Fullscreen (F11)",
                "Uscita da fullscreen" if is_fullscreen_now else "Entrata in fullscreen"
            )
            
            # Aggiorna i controlli dei video player in base al nuovo stato
            for player in self.video_players:
                # Passa lo stato futuro (l'opposto di quello attuale)
                player.update_controls_for_fullscreen(not is_fullscreen_now)
            
            if self.isFullScreen():
                self.showNormal()
            else:
                self.showFullScreen()
            event.accept()
        else:
            super().keyPressEvent(event)
    
    def _initialize_window_mode(self):
        """Inizializza i controlli dei video player in modalità windowed all'avvio."""
        for player in self.video_players:
            player.update_controls_for_fullscreen(False)  # False = windowed mode

    def changeEvent(self, event):  # type: ignore[override]
        """Gestisce i cambiamenti di stato della finestra (es. fullscreen/maximized)."""
        super().changeEvent(event)
        
        # Rileva cambio di stato fullscreen/windowed/maximized
        if event.type() == QEvent.Type.WindowStateChange:
            # Considera "fullscreen" sia WindowFullScreen che WindowMaximized
            is_fullscreen = bool(
                (self.windowState() & Qt.WindowState.WindowFullScreen) or
                (self.windowState() & Qt.WindowState.WindowMaximized)
            )
            
            # Aggiorna i controlli di tutti i video player
            for player in self.video_players:
                player.update_controls_for_fullscreen(is_fullscreen)
            
            logger.log_user_action(
                "Window state changed",
                f"Fullscreen/Maximized: {is_fullscreen}"
            )

    def closeEvent(self, event):  # type: ignore[override]
        """Gestisce la chiusura dell'applicazione con cleanup deterministico."""
        logger.log_user_action("Chiusura applicazione")
        
        if self.cache_manager:
            stats = self.cache_manager.get_global_stats()
            logger.log_user_action(
                "Frame cache stats finali",
                f"Hit rate: {stats['global_hit_rate']:.1f}%, "
                f"Hits: {stats['total_hits']}, Misses: {stats['total_misses']}, "
                f"Cached positions: {stats['total_cached_positions']}"
            )
            self.cache_manager.clear_all()
        
        if self.async_loader:
            logger.log_user_action("Pulizia async loader", "Chiusura thread in corso")
            self.async_loader.cleanup_all()
        
        if self.export_thread and self.export_thread.isRunning():
            logger.log_export_action("Interruzione esportazione per chiusura app")
            if self.exporter:
                self.exporter.stop()
            self.export_thread.quit()
            self.export_thread.wait(2000) # Attendi max 2 sec

        # Salva markers prima di chiudere
        if self.marker_manager.is_modified:
            self.marker_manager.save()
            logger.log_user_action("Markers salvati", f"{self.marker_manager.count} markers")

        for player in self.video_players:
            if player.is_loaded:
                player.stop()
                # Usa cleanup_video(remove_path=False) per mantenere i percorsi salvati
                player.cleanup_video(remove_path=False)
        logger.log_user_action("Cleanup video completato", "Percorsi salvati mantenuti per prossimo avvio")
        
        # Garbage collection finale
        collected = gc.collect()
        logger.log_user_action("GC finale", f"{collected} oggetti raccolti")

        event.accept()
        logger.log_user_action("Applicazione chiusa")
