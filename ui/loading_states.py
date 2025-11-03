"""
Loading States & Skeleton UI per SyncView.
Fornisce placeholder animati durante operazioni asincrone.
"""

from PyQt6.QtWidgets import QWidget, QLabel, QVBoxLayout, QProgressBar
from PyQt6.QtCore import Qt, QTimer
from PyQt6.QtGui import QPainter, QColor, QLinearGradient, QPaintEvent
from typing import Optional, Sequence


class PulsingLabel(QLabel):
    """Label con effetto pulsing per stati di caricamento.
    
    Animazione smooth che attira l'attenzione senza essere invadente.
    """
    
    def __init__(self, text: str = "", parent: Optional[QWidget] = None):
        super().__init__(text, parent)
        self._opacity = 1.0
        
        # Setup animation con timer manuale invece di QPropertyAnimation
        self.animation_timer = QTimer(self)
        self.animation_timer.setInterval(30)  # ~33fps
        self.animation_timer.timeout.connect(self._update_opacity)
        self.animation_phase = 0.0
        self.animation_direction = 1  # 1 = increasing, -1 = decreasing
    
    def start_pulsing(self) -> None:
        """Avvia l'animazione pulsing."""
        self.animation_timer.start()
    
    def stop_pulsing(self) -> None:
        """Ferma l'animazione e ripristina opacità."""
        self.animation_timer.stop()
        self._opacity = 1.0
        self.setStyleSheet("")
        self.update()
    
    def _update_opacity(self) -> None:
        """Aggiorna l'opacità con effetto sinusoidale."""
        # Incrementa fase
        self.animation_phase += 0.05
        
        # Calcola opacità con sine wave (0.3 to 1.0)
        import math
        self._opacity = 0.65 + 0.35 * math.sin(self.animation_phase)
        
        # Applica stile
        self.setStyleSheet(f"color: rgba(192, 154, 107, {self._opacity});")
        self.update()


class SkeletonWidget(QWidget):
    """Widget skeleton con shimmer effect.
    
    Mostra un placeholder animato durante il caricamento.
    """
    
    def __init__(self, parent: Optional[QWidget] = None):
        super().__init__(parent)
        self.shimmer_position = 0.0
        self.base_color = QColor(40, 40, 40)  # Grigio scuro
        self.highlight_color = QColor(60, 60, 60)  # Grigio chiaro
        
        # Timer per animazione shimmer
        self.shimmer_timer = QTimer(self)
        self.shimmer_timer.timeout.connect(self._update_shimmer)
        self.shimmer_timer.setInterval(16)  # ~60fps
    
    def start_shimmer(self) -> None:
        """Avvia l'effetto shimmer."""
        self.shimmer_timer.start()
    
    def stop_shimmer(self) -> None:
        """Ferma l'effetto shimmer."""
        self.shimmer_timer.stop()
        self.shimmer_position = 0.0
        self.update()
    
    def _update_shimmer(self) -> None:
        """Aggiorna la posizione del shimmer."""
        self.shimmer_position += 0.02  # Velocità shimmer
        if self.shimmer_position > 1.0:
            self.shimmer_position = -0.3  # Reset con overlap
        self.update()
    
    def paintEvent(self, a0: QPaintEvent) -> None:  # type: ignore[override]
        """Disegna il skeleton con shimmer effect."""
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        
        # Background base
        painter.fillRect(self.rect(), self.base_color)
        
        # Shimmer gradient
        gradient = QLinearGradient(
            self.width() * self.shimmer_position,
            0,
            self.width() * (self.shimmer_position + 0.3),
            0
        )
        gradient.setColorAt(0.0, self.base_color)
        gradient.setColorAt(0.5, self.highlight_color)
        gradient.setColorAt(1.0, self.base_color)
        
        painter.fillRect(self.rect(), gradient)


class LoadingOverlay(QWidget):
    """Overlay semitrasparente con indicatore di caricamento.
    
    Si posiziona sopra il widget parent per indicare operazioni in corso.
    """
    
    def __init__(self, parent: Optional[QWidget] = None, message: str = "Caricamento..."):
        super().__init__(parent)
        self.setObjectName("loadingOverlay")
        
        # Semitrasparente
        self.setStyleSheet("""
            QWidget#loadingOverlay {
                background-color: rgba(0, 0, 0, 0.7);
            }
            QLabel {
                color: #C19A6B;
                font-size: 14px;
                font-weight: bold;
            }
            QProgressBar {
                border: 2px solid #C19A6B;
                border-radius: 5px;
                text-align: center;
                background-color: #1a1a1a;
            }
            QProgressBar::chunk {
                background-color: #C19A6B;
            }
        """)
        
        # Layout
        layout = QVBoxLayout(self)
        layout.setAlignment(Qt.AlignmentFlag.AlignCenter)
        
        # Label con messaggio
        self.message_label = PulsingLabel(message)
        self.message_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(self.message_label)
        
        # Progress bar (opzionale)
        self.progress_bar = QProgressBar()
        self.progress_bar.setMinimum(0)
        self.progress_bar.setMaximum(0)  # Indeterminate
        self.progress_bar.setFixedWidth(300)
        self.progress_bar.hide()  # Nascosto di default
        layout.addWidget(self.progress_bar)
        
        # Nascondi di default
        self.hide()
    
    def show_loading(self, message: str = "", show_progress: bool = False) -> None:
        """Mostra l'overlay con messaggio opzionale.
        
        Args:
            message: Messaggio da mostrare (opzionale)
            show_progress: Se True, mostra la progress bar
        """
        if message:
            self.message_label.setText(message)
        
        if show_progress:
            self.progress_bar.show()
        else:
            self.progress_bar.hide()
        
        self.message_label.start_pulsing()
        self.show()
        self.raise_()  # Porta in primo piano
    
    def hide_loading(self) -> None:
        """Nasconde l'overlay."""
        self.message_label.stop_pulsing()
        self.hide()
    
    def update_message(self, message: str) -> None:
        """Aggiorna il messaggio senza nascondere l'overlay."""
        self.message_label.setText(message)
    
    def set_progress(self, value: int, maximum: int = 100) -> None:
        """Imposta progresso determinato.
        
        Args:
            value: Valore corrente
            maximum: Valore massimo
        """
        self.progress_bar.setMaximum(maximum)
        self.progress_bar.setValue(value)
        if not self.progress_bar.isVisible():
            self.progress_bar.show()


class ModalStateManager:
    """Gestisce lo stato modale dell'applicazione durante operazioni lunghe.
    
    Disabilita selettivamente i controlli per prevenire azioni durante
    operazioni critiche, mantenendo l'interfaccia responsiva.
    """
    
    def __init__(self):
        self.disabled_widgets = []
        self.original_enabled_states = {}
    
    def enter_modal_state(self, widgets_to_disable: Sequence[QWidget], 
                          message: str = "Operazione in corso...") -> None:
        """Entra in stato modale disabilitando widget specifici.
        
        Args:
            widgets_to_disable: Lista di widget da disabilitare
            message: Messaggio descrittivo (per logging)
        """
        self.disabled_widgets = list(widgets_to_disable)
        
        # Salva stato originale e disabilita
        for widget in self.disabled_widgets:
            self.original_enabled_states[id(widget)] = widget.isEnabled()
            widget.setEnabled(False)
    
    def exit_modal_state(self) -> None:
        """Esce dallo stato modale ripristinando i widget."""
        # Ripristina stato originale
        for widget in self.disabled_widgets:
            original_state = self.original_enabled_states.get(id(widget), True)
            widget.setEnabled(original_state)
        
        # Pulisci
        self.disabled_widgets.clear()
        self.original_enabled_states.clear()
    
    def is_modal(self) -> bool:
        """Verifica se siamo in stato modale.
        
        Returns:
            True se ci sono widget disabilitati
        """
        return len(self.disabled_widgets) > 0


class VideoLoadingSkeleton(QWidget):
    """Skeleton specifico per il loading di video players.
    
    Mostra un placeholder stilizzato durante il caricamento asincrono.
    """
    
    def __init__(self, parent: Optional[QWidget] = None):
        super().__init__(parent)
        self.setObjectName("videoSkeleton")
        
        # Style
        self.setStyleSheet("""
            QWidget#videoSkeleton {
                background-color: #1a1a1a;
                border: 2px solid #333333;
                border-radius: 8px;
            }
            QLabel {
                color: #666666;
                font-size: 12px;
            }
        """)
        
        layout = QVBoxLayout(self)
        layout.setAlignment(Qt.AlignmentFlag.AlignCenter)
        
        # Icona animata
        self.icon_label = PulsingLabel("🎬")
        self.icon_label.setStyleSheet("font-size: 48px;")
        self.icon_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(self.icon_label)
        
        # Messaggio
        self.status_label = QLabel("Caricamento video in corso...")
        self.status_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(self.status_label)
        
        # Dettagli (opzionale)
        self.details_label = QLabel("")
        self.details_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.details_label.hide()
        layout.addWidget(self.details_label)
    
    def show_loading(self, video_name: str = "") -> None:
        """Mostra il skeleton con nome video opzionale."""
        if video_name:
            self.details_label.setText(f"📁 {video_name}")
            self.details_label.show()
        self.icon_label.start_pulsing()
        self.show()
    
    def hide_loading(self) -> None:
        """Nasconde il skeleton."""
        self.icon_label.stop_pulsing()
        self.hide()
