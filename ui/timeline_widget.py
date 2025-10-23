"""
Widget timeline per visualizzazione e gestione markers video.
Versione 2.9 - Ruler abbassata
"""

from PyQt6.QtWidgets import QWidget, QHBoxLayout, QLabel, QPushButton, QToolTip
from PyQt6.QtCore import Qt, pyqtSignal, QPoint, QRect, QRectF, QPointF
from PyQt6.QtGui import (QPainter, QColor, QPen, QBrush, QFont, QMouseEvent,
                         QPaintEvent, QPolygonF, QFontMetrics)

from core.markers import Marker, MarkerManager
from typing import Optional, List, Tuple


class TimelineWidget(QWidget):
    """Widget timeline interattiva con markers (Nuova UI Tattica)."""

    # Segnali (API invariata)
    marker_clicked = pyqtSignal(Marker)
    timeline_clicked = pyqtSignal(int)
    marker_remove_requested = pyqtSignal(Marker)
    marker_moved = pyqtSignal(Marker, int)  # Mantenuto per compatibilità, ma non usato

    def __init__(self, parent=None):
        super().__init__(parent)

        self.marker_manager: Optional[MarkerManager] = None
        self.duration_ms = 0
        self.current_position_ms = 0

        # Stato interazione
        self.hover_marker: Optional[Marker] = None

        # Configurazione visuale
        # --- MODIFICA: Abbassato il righello ---
        self.ruler_y_pos = 35 # Era 25
        # --- FINE MODIFICA ---
        self.ruler_height = 24
        self.marker_height = 10  # Altezza del triangolo
        self.marker_width = 12   # Larghezza del triangolo

        # Setup UI
        self.setMinimumHeight(80) # Aumentato per accogliere i timestamp sotto
        self.setMaximumHeight(80)
        self.setMouseTracking(True)
        self.setStyleSheet("background-color: #1a1a1a;")

    def set_marker_manager(self, manager: MarkerManager):
        """Imposta il manager dei markers."""
        self.marker_manager = manager
        self.update()

    def set_duration(self, duration_ms: int):
        """Imposta la durata totale della timeline."""
        self.duration_ms = duration_ms
        self.update()

    def set_position(self, position_ms: int):
        """Imposta la posizione corrente."""
        self.current_position_ms = position_ms
        self.update()

    # ===================================================================
    #   PAINTING (Logica di disegno)
    # ===================================================================

    def paintEvent(self, event: QPaintEvent):  # type: ignore[override]
        """Disegna la nuova UI della timeline."""
        super().paintEvent(event)

        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)

        width = self.width()
        height = self.height()

        # 1. Disegna la base del righello
        ruler_rect = QRect(10, self.ruler_y_pos, width - 20, self.ruler_height)
        painter.fillRect(ruler_rect, QColor("#2d2d2d")) # Grigio scuro

        if self.duration_ms == 0:
            painter.setPen(QColor("#d4a356")) # Giallo bordo
            font = QFont('Arial', 10, QFont.Weight.Bold)
            painter.setFont(font)
            painter.drawText(ruler_rect, Qt.AlignmentFlag.AlignCenter, "NO VIDEO")
            return

        # 2. Disegna la barra di progresso
        current_x = self._timestamp_to_x(self.current_position_ms, width)
        progress_width = max(0, current_x - 10)
        progress_rect = QRect(10, self.ruler_y_pos, progress_width, self.ruler_height)
        painter.fillRect(progress_rect, QColor("#3d7a4a")) # Verde militare

        # 3. Disegna tacche e timestamp
        self._draw_ticks_and_labels(painter, width, ruler_rect)

        # 4. Disegna markers e i loro timestamp sotto
        if self.marker_manager:
            self._draw_markers_and_timestamps(painter, width)

        # 5. Disegna il Playhead (Indicatore di posizione)
        self._draw_playhead(painter, current_x, height)

    def _draw_ticks_and_labels(self, painter: QPainter, width: int, ruler_rect: QRect):
        """Disegna le tacche e i timestamp sul righello."""
        if self.duration_ms == 0:
            return

        interval_ms = 10000  # Default 10 secondi

        font = QFont('Arial', 8)
        painter.setFont(font)
        metrics = QFontMetrics(font)
        
        # Calcola la larghezza media del testo "MM:SS"
        # Se 10 secondi sono troppo vicini, aumenta l'intervallo
        min_display_px = metrics.horizontalAdvance("00:00") + 15 # min pixel tra le etichette
        
        # Calcola quanti intervalli da 10s ci sono sulla timeline
        num_10s_intervals = (width - 20) / (self.duration_ms / 10000) if self.duration_ms > 0 else 0
        
        if num_10s_intervals < min_display_px: # Se le etichette da 10s si sovrappongono
            if num_10s_intervals * 2 < min_display_px: # Prova 30s
                if num_10s_intervals * 3 < min_display_px: # Prova 60s
                    interval_ms = 60000 # 1 minuto
                else:
                    interval_ms = 30000 # 30 secondi
            else:
                interval_ms = 20000 # 20 secondi


        for t in range(0, self.duration_ms + 1, interval_ms):
            x = self._timestamp_to_x(t, width)

            # Tacca grande
            painter.setPen(QColor("#ffffff")) # Bianca
            painter.drawLine(x, self.ruler_y_pos, x, self.ruler_y_pos + self.ruler_height)

            # Etichetta MM:SS
            time_str = self._format_time_short(t)
            # Centra il testo sopra la tacca
            text_rect_width = metrics.horizontalAdvance(time_str)
            text_rect = QRect(x - text_rect_width // 2, self.ruler_y_pos, text_rect_width, self.ruler_height)
            painter.setPen(QColor("#dddddd"))
            painter.drawText(text_rect, Qt.AlignmentFlag.AlignVCenter | Qt.AlignmentFlag.AlignCenter, time_str)

            # Tacche piccole (ogni secondo) tra le tacche grandi, solo se l'intervallo è sufficientemente grande
            if interval_ms >= 10000: 
                for ts in range(1000, interval_ms, 1000):
                    small_t = t + ts
                    if small_t >= self.duration_ms: # Evita di disegnare tacche oltre la durata
                        break
                    small_x = self._timestamp_to_x(small_t, width)
                    painter.setPen(QColor("#777777")) # Grigio scuro
                    painter.drawLine(small_x, self.ruler_y_pos + int(self.ruler_height * 0.7),
                                     small_x, self.ruler_y_pos + self.ruler_height)

    def _draw_markers_and_timestamps(self, painter: QPainter, width: int):
        """Disegna i marker (triangoli sopra) con linea verticale e timestamp sotto."""
        if not self.marker_manager:
            return

        # Colore Giallo/Oro del bordo della finestra
        marker_color = QColor("#d4a356") 

        font = QFont('Arial', 8)
        painter.setFont(font)
        metrics = QFontMetrics(font)

        # Per evitare sovrapposizioni dei timestamp
        drawn_timestamps_rects: List[QRect] = []

        for marker in self.marker_manager.markers:
            x = self._timestamp_to_x(marker.timestamp, width)

            # --- Disegna il triangolo del marker sopra la timeline ---
            color = marker_color
            if marker == self.hover_marker:
                color = color.lighter(130) # Più luminoso su hover
                current_width = int(self.marker_width * 1.5)
                current_height = int(self.marker_height * 1.5)
            else:
                current_width = self.marker_width
                current_height = self.marker_height

            # La base (lato superiore) ora combacia con il bordo della timeline
            base_y = self.ruler_y_pos # Y della base (combacia con il top della ruler)
            point_y = self.ruler_y_pos + current_height # Y della punta (all'interno della ruler)

            triangle = QPolygonF([
                QPointF(x - current_width / 2, base_y), # Base-left
                QPointF(x + current_width / 2, base_y), # Base-right
                QPointF(x, point_y)                     # Punta in basso
            ])

            painter.setBrush(color)
            painter.setPen(QPen(color.darker(150), 1))
            painter.drawPolygon(triangle)

            # --- Disegna la linea verticale dal marker alla timeline ---
            painter.setPen(QPen(color, 1, Qt.PenStyle.DotLine)) # Linea puntinata
            # La linea ora parte dalla base_y (che è self.ruler_y_pos)
            painter.drawLine(x, base_y, x, self.ruler_y_pos + self.ruler_height)
            
            # --- Disegna il timestamp sotto la timeline ---
            time_str = self._format_time_adaptive(marker.timestamp)
            text_width = metrics.horizontalAdvance(time_str)
            text_height = metrics.height()
            
            # Posizione del timestamp sotto la timeline
            text_y = self.ruler_y_pos + self.ruler_height + 5 # 5px sotto la timeline
            text_x = x - text_width // 2
            
            marker_timestamp_rect = QRect(text_x, text_y, text_width, text_height)

            # Controlla sovrapposizioni
            can_draw_text = True
            for existing_rect in drawn_timestamps_rects:
                if marker_timestamp_rect.intersects(existing_rect):
                    can_draw_text = False
                    break
            
            if can_draw_text:
                painter.setPen(marker_color.lighter(120)) # Colore più chiaro del bordo
                painter.drawText(marker_timestamp_rect, Qt.AlignmentFlag.AlignCenter, time_str)
                drawn_timestamps_rects.append(marker_timestamp_rect.adjusted(-3, 0, 3, 0)) # Aggiungi buffer


    def _draw_playhead(self, painter: QPainter, x: int, height: int):
        """Disegna l'indicatore di posizione corrente (playhead)."""
        
        playhead_color = QColor("#d4a356") # Giallo Bordo
        playhead_width = 3

        # --- FASE 1: Calcola tutte le coordinate ---
        # (Calcoli aggiornati per la nuova ruler_y_pos)

        # 2. Triangolo (caret)
        caret_size = 8
        # La punta (bottom) ora è 4px *dentro* il righello
        caret_bottom_y = self.ruler_y_pos + 4
        # La base (top) è 8px sopra la punta
        caret_top_y = caret_bottom_y - caret_size
        
        caret = QPolygonF([
            QPointF(x - caret_size / 2, caret_top_y), # Top-left
            QPointF(x + caret_size / 2, caret_top_y), # Top-right
            QPointF(x, caret_bottom_y)                # Bottom point (sulla timeline)
        ])
        
        # 3. Riquadro con il tempo corrente
        time_str = self._format_time_adaptive(self.current_position_ms)
        font = QFont('Arial', 9, QFont.Weight.Bold)
        painter.setFont(font) # Imposta il font PRIMA di misurare
        metrics = QFontMetrics(font)
        text_width = metrics.horizontalAdvance(time_str) + 10 # padding
        text_height = metrics.height() + 4

        # Posiziona il riquadro sopra il caret
        # 2px di spazio sopra il caret
        rect_y = caret_top_y - text_height - 2
        
        # Assicura che il riquadro non esca dai bordi (ora c'è più spazio)
        rect_x = x - (text_width // 2)
        if rect_x < 5: rect_x = 5
        if rect_x + text_width > self.width() - 5: rect_x = self.width() - 5 - text_width

        time_rect = QRectF(rect_x, rect_y, text_width, text_height)

        # 1. Linea verticale
        # Inizia dalla parte superiore del riquadro del tempo
        line_top_y = rect_y 
        # Finisce alla parte inferiore del righello
        line_bottom_y = self.ruler_y_pos + self.ruler_height

        # --- FASE 2: Disegna gli elementi ---

        # 1. Linea verticale (usa le coordinate calcolate)
        painter.setPen(QPen(playhead_color, playhead_width))
        painter.drawLine(x, int(line_top_y), x, int(line_bottom_y)) # Cast a int per drawLine

        # 2. Triangolo (caret)
        painter.setBrush(playhead_color)
        painter.setPen(Qt.PenStyle.NoPen)
        painter.drawPolygon(caret)

        # 3. Riquadro con il tempo corrente
        painter.setBrush(QColor("#0d0d0d")) # Sfondo nero
        painter.setPen(QPen(playhead_color, 2)) # Bordo (ora giallo)
        painter.drawRoundedRect(time_rect, 3, 3)
        painter.setPen(QColor("#ffffff")) # Testo bianco
        painter.drawText(time_rect, Qt.AlignmentFlag.AlignCenter, time_str)


    # ===================================================================
    #   EVENTI MOUSE
    # ===================================================================

    def mousePressEvent(self, event: QMouseEvent):  # type: ignore[override]
        """Gestisce click del mouse."""
        if event.button() == Qt.MouseButton.LeftButton:
            x, y = event.pos().x(), event.pos().y()

            # Check click su marker
            marker = self._get_marker_at_position(event.pos())

            if marker:
                # Click su marker: naviga
                self.marker_clicked.emit(marker)
            else:
                # Click su timeline vuota: naviga
                timestamp = self._x_to_timestamp(x, self.width())
                self.timeline_clicked.emit(timestamp)

    def mouseDoubleClickEvent(self, event: QMouseEvent):  # type: ignore[override]
        """Gestisce doppio click per rimozione marker."""
        if event.button() == Qt.MouseButton.LeftButton:
            marker = self._get_marker_at_position(event.pos())
            if marker:
                self.marker_remove_requested.emit(marker)

    def mouseMoveEvent(self, event: QMouseEvent):  # type: ignore[override]
        """Gestisce movimento del mouse per hover."""
        # Non c'è drag (rimosso)
        # Aggiorna hover
        marker = self._get_marker_at_position(event.pos())

        if marker != self.hover_marker:
            self.hover_marker = marker
            self.update()

            # Mostra tooltip
            if marker:
                # Il tempo è già visualizzato sotto, quindi il tooltip mostra solo descrizione/categoria
                time_str_long = self._format_time_long(marker.timestamp)
                tooltip_text = f"<b>MARKER</b><br><span style='font-size:11pt; color:#d4a356;'>{time_str_long}</span>"
                if marker.description:
                    tooltip_text += f"<br><i>{marker.description}</i>"
                if marker.category != 'default':
                    tooltip_text += f"<br>📂 {marker.category.title()}"
                QToolTip.showText(event.globalPosition().toPoint(), tooltip_text, self)
            else:
                QToolTip.hideText()

    def mouseReleaseEvent(self, event: QMouseEvent):  # type: ignore[override]
        """Gestisce rilascio del mouse."""
        # Logica drag rimossa
        pass

    def leaveEvent(self, event):  # type: ignore[override]
        """Mouse esce dal widget."""
        self.hover_marker = None
        self.update()
        QToolTip.hideText()

    # ===================================================================
    #   FUNZIONI HELPER
    # ===================================================================

    def _timestamp_to_x(self, timestamp_ms: int, width: int) -> int:
        """Converte timestamp in coordinata X."""
        if self.duration_ms == 0:
            return 10
        ratio = timestamp_ms / self.duration_ms
        # Mappa sul range [10, width - 10]
        return int(10 + (width - 20) * ratio)

    def _x_to_timestamp(self, x: int, width: int) -> int:
        """Converte coordinata X in timestamp."""
        if width - 20 == 0: return 0
        ratio = (x - 10) / (width - 20)
        ratio = max(0.0, min(1.0, ratio))  # Clamp 0-1
        return int(self.duration_ms * ratio)

    def _format_time_short(self, ms: int) -> str:
        """Formatta millisecondi in MM:SS (per il righello)."""
        total_seconds = ms / 1000
        minutes = int(total_seconds // 60)
        seconds = int(total_seconds % 60)
        return f"{minutes:02d}:{seconds:02d}"

    def _format_time_adaptive(self, ms: int) -> str:
        """Formatta millisecondi in [HH:][MM:]SS.mmm (per i marker E playhead)."""
        total_seconds = ms / 1000
        hours = int(total_seconds // 3600)
        minutes = int((total_seconds % 3600) // 60)
        seconds = int(total_seconds % 60)
        millis = int(ms % 1000)

        if hours > 0:
            return f"{hours:02d}:{minutes:02d}:{seconds:02d}.{millis:03d}"
        elif minutes > 0:
            return f"{minutes:02d}:{seconds:02d}.{millis:03d}"
        else:
            return f"{seconds:02d}.{millis:03d}"

    def _format_time_long(self, ms: int) -> str:
        """Formatta millisecondi in HH:MM:SS.mmm (usato solo per tooltip ora)."""
        total_seconds = ms / 1000
        hours = int(total_seconds // 3600)
        minutes = int((total_seconds % 3600) // 60)
        seconds = int(total_seconds % 60)
        millis = int(ms % 1000)
        return f"{hours:02d}:{minutes:02d}:{seconds:02d}.{millis:03d}"

    def _get_marker_at_position(self, pos: QPoint) -> Optional[Marker]:
        """Trova marker (triangolo) sotto il cursore."""
        if not self.marker_manager:
            return None

        width = self.width()
        
        # Le coordinate ora sono relative alla nuova ruler_y_pos
        hit_y_start = self.ruler_y_pos - 5 # 5px buffer sopra la base
        hit_y_end = self.ruler_y_pos + self.marker_height + 5 # 5px buffer sotto la punta

        if not (hit_y_start <= pos.y() <= hit_y_end):
             return None # Cursore non è sulla riga dei marker

        for marker in self.marker_manager.markers:
            marker_x = self._timestamp_to_x(marker.timestamp, width)
            
            # Area di "hit" orizzontale
            hit_x_start = marker_x - (self.marker_width // 2) - 3 # 3px buffer
            hit_x_end = marker_x + (self.marker_width // 2) + 3

            if hit_x_start <= pos.x() <= hit_x_end:
                return marker
        
        return None


# ===================================================================
#   WIDGET CONTROLLI (INVARIATO)
# ===================================================================

class TimelineControlWidget(QWidget):
    """Widget controlli timeline (aggiungi marker, zoom, etc)."""

    add_marker_requested = pyqtSignal()  # Richiesta aggiunta marker
    prev_marker_requested = pyqtSignal()  # Vai a marker precedente
    next_marker_requested = pyqtSignal()  # Vai a marker prossimo
    export_markers_requested = pyqtSignal()  # Esporta video da markers

    def __init__(self, parent=None):
        super().__init__(parent)

        layout = QHBoxLayout()
        layout.setContentsMargins(0, 5, 0, 5)
        layout.setSpacing(5)

        # Label
        label = QLabel("📍 TIMELINE:")
        label.setStyleSheet("color: #4a9eff; font-weight: bold;")
        layout.addWidget(label)

        # Pulsante aggiungi marker
        self.btn_add = QPushButton("+ Marker")
        self.btn_add.setToolTip("Aggiungi marker alla posizione corrente (Ctrl+M)")
        self.btn_add.clicked.connect(self.add_marker_requested.emit)
        self.btn_add.setStyleSheet("""
            QPushButton {
                background-color: #2a4a2a;
                color: #ffffff;
                border: 1px solid #3a5a3a;
                border-radius: 3px;
                padding: 5px 10px;
                font-weight: bold;
            }
            QPushButton:hover {
                background-color: #3a5a3a;
            }
        """)
        layout.addWidget(self.btn_add)

        # Pulsante marker precedente
        self.btn_prev = QPushButton("◀ Prec")
        self.btn_prev.setToolTip("Vai al marker precedente (P)")
        self.btn_prev.clicked.connect(self.prev_marker_requested.emit)
        layout.addWidget(self.btn_prev)

        # Pulsante marker successivo
        self.btn_next = QPushButton("Succ ▶")
        self.btn_next.setToolTip("Vai al marker successivo (N)")
        self.btn_next.clicked.connect(self.next_marker_requested.emit)
        layout.addWidget(self.btn_next)

        layout.addStretch()

        # Pulsante esportazione video da markers
        self.btn_export = QPushButton("📤 Esporta")
        self.btn_export.setToolTip("Esporta video da markers (Ctrl+E)")
        self.btn_export.clicked.connect(self.export_markers_requested.emit)
        self.btn_export.setStyleSheet("""
            QPushButton {
                background-color: #2a3a4a;
                color: #ffffff;
                border: 1px solid #3a4a5a;
                border-radius: 3px;
                padding: 5px 10px;
                font-weight: bold;
            }
            QPushButton:hover {
                background-color: #3a4a5a;
            }
        """)
        layout.addWidget(self.btn_export)

        # Stile generale
        self.setStyleSheet("""
            QPushButton {
                background-color: #2a2a2a;
                color: #cccccc;
                border: 1px solid #3a3a3a;
                border-radius: 3px;
                padding: 5px 10px;
            }
            QPushButton:hover {
                background-color: #3a3a3a;
                color: #ffffff;
            }
        """)

        self.setLayout(layout)

