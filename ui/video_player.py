"""
Widget del player video per SyncView.
Supporta caricamento drag & drop e gestione errori.
"""

from PyQt6.QtWidgets import (QWidget, QVBoxLayout, QLabel, QPushButton,
                             QHBoxLayout, QFrame)
from PyQt6.QtCore import Qt, QUrl, pyqtSignal, QTimer
from PyQt6.QtMultimedia import QMediaPlayer, QAudioOutput
from PyQt6.QtMultimediaWidgets import QVideoWidget
from pathlib import Path
from core.logger import logger
# --- FIX IMPORTAZIONE ---
from ui.timeline_widget import TimelineWidget
# --- FINE FIX ---
from core.markers import MarkerManager
from typing import TYPE_CHECKING


class VideoPlayerWidget(QWidget):
    """Widget per la riproduzione di un singolo video."""

    # Segnali
    position_changed = pyqtSignal(int)  # Posizione corrente
    duration_changed = pyqtSignal(int)  # Durata totale
    user_seeked = pyqtSignal(int, int)  # (video_index, posizione) - quando l'utente sposta la timeline manually
    video_focused = pyqtSignal(int)  # (video_index) - quando il video viene cliccato/selezionato
    add_marker_requested = pyqtSignal(int, int)  # (video_index, posizione) - richiesta aggiunta marker da questo video

    def __init__(self, video_index, parent=None):
        super().__init__(parent)
        self.video_index = video_index
        self.video_path = None
        self.is_loaded = False
        self.detected_fps = 0.0  # FPS rilevato dal video
        self.marker_manager: MarkerManager | None = None  # Sarà impostato dal main window

        # Media player
        self.media_player = QMediaPlayer(self)
        self.audio_output = QAudioOutput(self)
        self.media_player.setAudioOutput(self.audio_output)

        # Video widget
        self.video_widget = QVideoWidget()
        self.media_player.setVideoOutput(self.video_widget)

        # Connessioni
        self.media_player.positionChanged.connect(self.on_position_changed)
        self.media_player.durationChanged.connect(self.on_duration_changed)
        self.media_player.errorOccurred.connect(self.on_error)

        # Setup UI
        self.setup_ui()

        # Abilita drag & drop
        self.setAcceptDrops(True)

    def setup_ui(self):
        """Configura l'interfaccia utente."""
        layout = QVBoxLayout(self)
        layout.setContentsMargins(5, 5, 5, 5)
        layout.setSpacing(5)

        # Header con titolo e controlli
        header_layout = QHBoxLayout()
        # Etichetta titolo (es. "FEED-1")
        self.title_label = QLabel(f"FEED-{self.video_index + 1}")
        self.title_label.setObjectName("headerLabel")
        self.title_label.setAlignment(Qt.AlignmentFlag.AlignLeft)
        header_layout.addWidget(self.title_label)

        header_layout.addStretch()

        # FPS label (inizialmente nascosto)
        self.fps_label = QLabel("")
        self.fps_label.setStyleSheet("color: #C19A6B; font-size: 11px; font-weight: normal;")
        self.fps_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.fps_label.hide()
        header_layout.addWidget(self.fps_label)

        # Bottone carica video (visibile quando nessun video è caricato)
        self.load_button = QPushButton("📁 CARICA")
        self.load_button.setObjectName("loadVideoButton")
        self.load_button.setToolTip("Carica un video per questo slot")
        self.load_button.clicked.connect(self.on_load_clicked)
        header_layout.addWidget(self.load_button)

        # Bottone rimuovi video (visibile quando video è caricato)
        self.remove_button = QPushButton("✕ RIMUOVI")
        self.remove_button.setObjectName("removeVideoButton")
        self.remove_button.setToolTip("Rimuovi il video da questo slot")
        self.remove_button.clicked.connect(self.unload_video)
        self.remove_button.hide()
        header_layout.addWidget(self.remove_button)

        # Status label
        self.status_label = QLabel("NO VIDEO")
        self.status_label.setObjectName("statusLabel")
        self.status_label.setAlignment(Qt.AlignmentFlag.AlignRight)
        header_layout.addWidget(self.status_label)

        layout.addLayout(header_layout)

        # Container per il video
        self.video_container = QFrame()
        self.video_container.setObjectName("videoFrame")
        # Imposta altezza minima sul container
        self.video_container.setMinimumHeight(200)
        container_layout = QVBoxLayout(self.video_container)
        container_layout.setContentsMargins(0, 0, 0, 0)

        # Placeholder o video widget
        self.placeholder_label = QLabel("Trascina un video qui\no\nClicca per selezionare")
        self.placeholder_label.setObjectName("placeholderLabel")
        self.placeholder_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.placeholder_label.setWordWrap(True)
        # Rimuovi l'altezza minima da qui
        # self.placeholder_label.setMinimumHeight(200)

        container_layout.addWidget(self.placeholder_label)
        container_layout.addWidget(self.video_widget)
        self.video_widget.hide()

        layout.addWidget(self.video_container, 1)

        # Controlli individuali (nascosti inizialmente)
        self.controls_widget = self.create_controls()
        self.controls_widget.hide()
        layout.addWidget(self.controls_widget)

        # Timeline widget con markers
        self.timeline_widget = TimelineWidget()
        self.timeline_widget.setMinimumHeight(80) # Altezza nuova UI
        self.timeline_widget.setMaximumHeight(80) # Altezza nuova UI
        self.timeline_widget.timeline_clicked.connect(self.on_timeline_clicked)
        layout.addWidget(self.timeline_widget)

        # Info label
        self.info_label = QLabel("00:00:00 / 00:00:00")
        self.info_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(self.info_label)

    def create_controls(self):
        """Crea i controlli individuali del player."""
        controls = QWidget()
        layout = QHBoxLayout(controls)
        layout.setContentsMargins(0, 0, 0, 0)

        # Play/Pause
        self.play_btn = QPushButton("▶ Play")
        self.play_btn.clicked.connect(self.toggle_play_pause)
        layout.addWidget(self.play_btn)

        # Mute
        self.mute_btn = QPushButton("🔊 Audio")
        self.mute_btn.setCheckable(True)
        self.mute_btn.clicked.connect(self.toggle_mute)
        layout.addWidget(self.mute_btn)

        # Spacer
        layout.addStretch()

        # Pulsante Marker (per modalità singola)
        self.add_marker_btn = QPushButton("📍 + Marker")
        self.add_marker_btn.setToolTip("Aggiungi marker alla posizione corrente (Ctrl+M)")
        self.add_marker_btn.clicked.connect(self.add_marker_at_position)
        layout.addWidget(self.add_marker_btn)

        layout.addStretch()

        # Frame controls
        self.prev_frame_btn = QPushButton("◀◀ Frame Prec.")
        self.prev_frame_btn.setToolTip("Torna indietro di 10 frame (Shift + ←)")
        self.prev_frame_btn.clicked.connect(lambda: self.step_frames(-10))
        layout.addWidget(self.prev_frame_btn)
        
        self.next_frame_btn = QPushButton("Frame Succ. ▶▶")
        self.next_frame_btn.setToolTip("Avanza di 10 frame (Shift + →)")
        self.next_frame_btn.clicked.connect(lambda: self.step_frames(10))
        layout.addWidget(self.next_frame_btn)

        # Rinomina i pulsanti -1 e +1 per coerenza con la shortcut
        self.prev_1_frame_btn = QPushButton("◀ -1 Frame")
        self.prev_1_frame_btn.setToolTip("Torna indietro di 1 frame (←)")
        self.prev_1_frame_btn.clicked.connect(lambda: self.step_frames(-1))
        layout.addWidget(self.prev_1_frame_btn)

        self.next_1_frame_btn = QPushButton("+1 Frame ▶")
        self.next_1_frame_btn.setToolTip("Avanza di 1 frame (→)")
        self.next_1_frame_btn.clicked.connect(lambda: self.step_frames(1))
        layout.addWidget(self.next_1_frame_btn)


        return controls

    def load_video(self, video_path):
        """Carica un video nel player."""
        try:
            video_path = Path(video_path)
            if not video_path.exists():
                raise FileNotFoundError(f"File non trovato: {video_path}")

            self.video_path = video_path
            self.media_player.setSource(QUrl.fromLocalFile(str(video_path)))

            # Rileva gli FPS del video
            self.detect_video_fps(video_path)

            # Aggiorna UI
            self.placeholder_label.hide()
            self.video_widget.show()
            self.status_label.setText("✓ PRONTO")
            self.status_label.setStyleSheet("color: #5F6F52;")
            self.is_loaded = True

            # Aggiorna visibilità bottoni
            self.load_button.hide()
            self.remove_button.show()

            # Carica il terzo frame come preview (dopo che la durata è nota)
            QTimer.singleShot(100, self.load_preview_frame)

            logger.log_video_action(self.video_index, "Video caricato", f"{video_path.name} - {self.detected_fps:.2f} fps")

        except Exception as e:
            self.show_error(f"Errore caricamento: {str(e)}")
            logger.log_error(f"Errore caricamento video Feed-{self.video_index + 1}: {str(e)}", e)

    def detect_video_fps(self, video_path):
        """Rileva gli FPS del video usando ffprobe o metodo alternativo."""
        import subprocess
        import json

        try:
            # Prova a usare ffprobe (parte di FFmpeg)
            result = subprocess.run(
                ['ffprobe', '-v', 'error', '-select_streams', 'v:0',
                 '-show_entries', 'stream=r_frame_rate', '-of', 'json',
                 str(video_path)],
                capture_output=True,
                text=True,
                timeout=5
            )

            if result.returncode == 0:
                data = json.loads(result.stdout)
                if data.get('streams') and len(data['streams']) > 0:
                    fps_str = data['streams'][0].get('r_frame_rate', '0/1')
                    # Converte frazione a decimale (es. "30000/1001" -> 29.97)
                    num, den = map(int, fps_str.split('/'))
                    if den > 0:
                        self.detected_fps = num / den
                        self.fps_label.setText(f"📹 {self.detected_fps:.2f} fps")
                        self.fps_label.show()
                        return

        except (subprocess.TimeoutExpired, FileNotFoundError, json.JSONDecodeError, ValueError) as e:
            # Log solo per debug, non è un errore critico
            pass

        # Metodo alternativo: stima basata su metadati PyQt6
        try:
            # Usa un approccio semplificato - segna come "Auto"
            self.detected_fps = 0.0
            self.fps_label.setText("📹 Auto")
            self.fps_label.show()
        except Exception:
            self.detected_fps = 0.0
            self.fps_label.hide()

    def load_preview_frame(self):
        """Carica il terzo frame del video come preview."""
        if self.is_loaded:
            # Calcola la posizione del terzo frame (~120ms a 25fps)
            preview_position = 120  # millisecondi
            self.media_player.setPosition(preview_position)
            # Mette in pausa per mostrare il frame
            self.media_player.pause()

    def show_error(self, message):
        """Mostra un messaggio di errore."""
        self.placeholder_label.setText(f"⚠ ERRORE\n\n{message}")
        self.placeholder_label.setObjectName("errorLabel")
        self.placeholder_label.show()
        self.video_widget.hide()
        self.status_label.setText("✗ ERRORE")
        self.status_label.setStyleSheet("color: #B80F0A;")
        self.fps_label.hide()
        self.is_loaded = False

    def play(self):
        """Avvia la riproduzione."""
        if self.is_loaded:
            self.media_player.play()
            self.play_btn.setText("⏸ Pausa")
            logger.log_playback(self.video_index, "PLAY")

    def pause(self):
        """Mette in pausa la riproduzione."""
        if self.is_loaded:
            self.media_player.pause()
            self.play_btn.setText("▶ Play")
            logger.log_playback(self.video_index, "PAUSA")

    def stop(self):
        """Ferma la riproduzione."""
        if self.is_loaded:
            self.media_player.stop()
            self.play_btn.setText("▶ Play")
            logger.log_playback(self.video_index, "STOP")

    def toggle_play_pause(self):
        """Alterna tra play e pausa."""
        if self.media_player.playbackState() == QMediaPlayer.PlaybackState.PlayingState:
            self.pause()
        else:
            self.play()

    def toggle_mute(self):
        """Attiva/disattiva l'audio."""
        is_muted = self.audio_output.isMuted()
        self.audio_output.setMuted(not is_muted)
        self.mute_btn.setText("🔇 Muto" if not is_muted else "🔊 Audio")
        logger.log_video_action(self.video_index, "Audio", "MUTO" if not is_muted else "ATTIVO")

    def set_muted(self, muted):
        """Imposta lo stato mute."""
        self.audio_output.setMuted(muted)
        self.mute_btn.setText("🔇 Muto" if muted else "🔊 Audio")
        self.mute_btn.setChecked(muted)

    def seek_position(self, position, emit_signal=False):
        """Salta a una posizione specifica."""
        if self.is_loaded:
            self.media_player.setPosition(position)
            if emit_signal:
                logger.log_timeline_seek(self.video_index, position)
    
    def on_timeline_clicked(self, position):
        """Gestisce quando l'utente clicca sulla timeline."""
        if self.is_loaded:
            self.media_player.setPosition(position)
            logger.log_timeline_seek(self.video_index, position)
            # Emetti segnale per sincronizzazione (gestito da main_window se SYNC OFF)
            self.user_seeked.emit(self.video_index, position)

    def get_position(self):
        """Ritorna la posizione corrente in millisecondi."""
        return self.media_player.position()

    def get_duration(self):
        """Ritorna la durata totale in millisecondi."""
        return self.media_player.duration()

    def set_playback_rate(self, rate):
        """Imposta la velocità di riproduzione."""
        if self.is_loaded:
            self.media_player.setPlaybackRate(rate)
    
    # --- MODIFICA: Funzione per gestire lo step dei frame ---
    def step_frames(self, frame_count: int):
        """Avanza o retrocede di N frame.
        
        Usa 40ms (25fps) come step di default.
        """
        if self.is_loaded:
            # Mette in pausa se si usa lo step
            if self.media_player.playbackState() == QMediaPlayer.PlaybackState.PlayingState:
                self.pause()
                
            step_ms = 40 # 40ms = 1 frame a 25fps
            total_step = step_ms * frame_count
            
            current_pos = self.media_player.position()
            duration = self.media_player.duration()
            
            new_pos = max(0, min(duration, current_pos + total_step))
            
            self.media_player.setPosition(new_pos)
            logger.log_video_action(self.video_index, f"Step Frame ({frame_count})", f"Posizione: {new_pos}ms")


    def add_marker_at_position(self):
        """Aggiunge un marker alla posizione corrente di questo video."""
        if self.is_loaded:
            position = self.media_player.position()
            # Emetti segnale per far gestire al MainWindow
            self.add_marker_requested.emit(self.video_index, position)
            logger.log_video_action(self.video_index, "Marker richiesto", f"Posizione: {position}ms")

    def on_position_changed(self, position):
        """Gestisce il cambio di posizione."""
        # Aggiorna timeline widget
        self.timeline_widget.set_position(position)

        # Aggiorna info label
        self.update_time_label(position, self.media_player.duration())

        # Emetti segnale
        self.position_changed.emit(position)

    def on_duration_changed(self, duration):
        """Gestisce il cambio di durata."""
        # Aggiorna durata timeline widget
        self.timeline_widget.set_duration(duration)
        self.update_time_label(self.media_player.position(), duration)
        self.duration_changed.emit(duration)

    def on_error(self, error, error_string):
        """Gestisce gli errori del media player."""
        self.show_error(error_string)
        logger.log_error(f"Errore media player Feed-{self.video_index + 1}", error_string)

    def update_time_label(self, position, duration):
        """Aggiorna il label con i tempi."""
        pos_str = self.format_time(position)
        dur_str = self.format_time(duration)
        self.info_label.setText(f"{pos_str} / {dur_str}")

    @staticmethod
    def format_time(milliseconds):
        """Formatta il tempo in HH:MM:SS."""
        seconds = milliseconds // 1000
        hours = seconds // 3600
        minutes = (seconds % 3600) // 60
        seconds = seconds % 60
        return f"{hours:02d}:{minutes:02d}:{seconds:02d}"

    def show_controls(self, show):
        """Mostra/nasconde i controlli individuali."""
        if show:
            self.controls_widget.show()
        else:
            self.controls_widget.hide()

    def hide_timeline(self):
        """Nasconde la timeline del video."""
        self.timeline_widget.hide()
        self.info_label.hide() # Nascondi anche l'info label

    def show_timeline(self):
        """Mostra la timeline del video."""
        self.timeline_widget.show()
        self.info_label.show() # Mostra anche l'info label

    def set_marker_manager(self, marker_manager: MarkerManager):
        """Imposta il marker manager e crea una vista filtrata per questo video.

        Args:
            marker_manager: Il marker manager globale
        """
        self.marker_manager = marker_manager
        # Crea una vista filtrata dei marker per questo video
        self.update_markers()

    def update_markers(self):
        """Aggiorna i marker visualizzati sulla timeline di questo video."""
        if self.marker_manager:
            # Filtra marker per questo video (include marker globali video_index=None)
            filtered_markers = self.marker_manager.get_markers_for_video(self.video_index)

            # Crea un manager temporaneo con solo i marker filtrati
            from core.markers import MarkerManager
            temp_manager = MarkerManager()
            temp_manager.markers = filtered_markers

            # Imposta il manager filtrato sulla timeline
            self.timeline_widget.set_marker_manager(temp_manager)
            self.timeline_widget.update()

    def on_load_clicked(self):
        """Apre il dialogo per caricare un video."""
        from PyQt6.QtWidgets import QFileDialog
        from config.settings import SUPPORTED_VIDEO_FORMATS

        file_filter = "Video Files (" + " ".join(f"*{fmt}" for fmt in SUPPORTED_VIDEO_FORMATS) + ")"
        file_path, _ = QFileDialog.getOpenFileName(
            self,
            f"Seleziona video per Feed-{self.video_index + 1}",
            str(Path.home()),
            file_filter
        )

        if file_path:
            self.load_video(file_path)
            # Aggiorna visibilità bottoni
            self.load_button.hide()
            self.remove_button.show()

    def unload_video(self):
        """Rimuove il video dal player."""
        logger.log_video_action(self.video_index, "Video rimosso",
                                f"{self.video_path.name if self.video_path else 'N/A'}")

        # Ferma la riproduzione
        self.media_player.stop()

        # Cancella la sorgente
        self.media_player.setSource(QUrl())

        # Reset stato
        self.video_path = None
        self.is_loaded = False
        self.detected_fps = 0.0

        # Reset UI
        self.video_widget.hide()
        self.placeholder_label.show()
        self.timeline_widget.set_position(0)
        self.timeline_widget.set_duration(0)
        self.status_label.setText("NO VIDEO")
        self.status_label.setStyleSheet("color: #808080;")
        self.info_label.setText("00:00:00 / 00:00:00")
        self.fps_label.hide()
        self.fps_label.setText("")

        # Aggiorna visibilità bottoni
        self.load_button.show()
        self.remove_button.hide()

    # Drag & Drop

    def dragEnterEvent(self, event):  # type: ignore[override]
        """Gestisce drag enter per drag & drop."""
        if event.mimeData().hasUrls():
            event.acceptProposedAction()

    def dropEvent(self, event):  # type: ignore[override]
        """Gestisce drop per drag & drop."""
        urls = event.mimeData().urls()
        if urls:
            file_path = urls[0].toLocalFile()
            from config.settings import SUPPORTED_VIDEO_FORMATS
            if any(file_path.lower().endswith(fmt) for fmt in SUPPORTED_VIDEO_FORMATS):
                self.load_video(file_path)
            else:
                self.show_error("Formato video non supportato")

    def mousePressEvent(self, event):  # type: ignore[override]
        """Gestisce il click per aprire il dialogo di selezione file."""
        # Emetti segnale di focus quando viene cliccato (sia con che senza video)
        self.video_focused.emit(self.video_index)

        if event.button() == Qt.MouseButton.LeftButton and not self.is_loaded:
            from PyQt6.QtWidgets import QFileDialog
            from config.settings import SUPPORTED_VIDEO_FORMATS

            file_filter = "Video Files (" + " ".join(f"*{fmt}" for fmt in SUPPORTED_VIDEO_FORMATS) + ")"
            file_path, _ = QFileDialog.getOpenFileName(
                self,
                f"Seleziona video per Feed-{self.video_index + 1}",
                str(Path.home()),
                file_filter
            )

            if file_path:
                self.load_video(file_path)

        super().mousePressEvent(event)