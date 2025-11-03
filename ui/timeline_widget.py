"""
Widget timeline per visualizzazione e gestione markers video.
Versione 3.0 - Ottimizzazioni: spatial indexing, viewport culling, debouncing
"""

from PyQt6.QtWidgets import QWidget, QHBoxLayout, QLabel, QPushButton, QToolTip
from PyQt6.QtCore import Qt, pyqtSignal, QPoint, QRect, QRectF, QPointF, QTimer
from PyQt6.QtGui import (QPainter, QColor, QPen, QBrush, QFont, QMouseEvent,
                         QPaintEvent, QPolygonF, QFontMetrics, QCursor)

from core.markers import Marker, MarkerManager
from core.spatial_index import MarkerSpatialIndex, ViewportCalculator
from core.debounce import Debouncer, UpdateScheduler
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
        
        # --- OTTIMIZZAZIONI ---
        # Spatial indexing per query O(log n)
        self.marker_index = MarkerSpatialIndex()
        
        # Viewport calculator per culling
        self.viewport = ViewportCalculator()
        
        # Update scheduler per debouncing/throttling
        self.update_scheduler = UpdateScheduler(throttle_ms=16, debounce_ms=50)
        
        # Cache per evitare ricalcoli
        self._cached_visible_markers: List[Marker] = []
        self._cache_valid = False
        # ----------------------

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
        self.setStyleSheet("background-color: #1C1C1E;")

    def set_marker_manager(self, manager: MarkerManager):
        """Imposta il manager dei markers."""
        self.marker_manager = manager
        self._rebuild_index()
        self._schedule_update('normal')

    def set_duration(self, duration_ms: int):
        """Imposta la durata totale della timeline."""
        self.duration_ms = duration_ms
        self.viewport.update_dimensions(self.width(), duration_ms)
        self._invalidate_cache()
        self._schedule_update('normal')

    def set_position(self, position_ms: int):
        """Imposta la posizione corrente."""
        self.current_position_ms = position_ms
        # Update position usa throttling per smooth playback
        self._schedule_update('fast')
    
    def _rebuild_index(self) -> None:
        """Ricostruisce l'indice spaziale dei markers."""
        if self.marker_manager:
            self.marker_index.update(self.marker_manager.markers)
            self._invalidate_cache()
    
    def _invalidate_cache(self) -> None:
        """Invalida la cache dei markers visibili."""
        self._cache_valid = False
    
    def _get_visible_markers(self) -> List[Marker]:
        """Ottiene i markers visibili nell'area corrente (con caching).
        
        Returns:
            Lista di markers visibili
        """
        if self._cache_valid:
            return self._cached_visible_markers
        
        # Query range visibile dall'indice spaziale
        start_ms, end_ms = self.viewport.get_visible_range()
        self._cached_visible_markers = self.marker_index.query_range(start_ms, end_ms)
        self._cache_valid = True
        
        return self._cached_visible_markers
    
    def _schedule_update(self, mode: str = 'normal') -> None:
        """Schedula un update con debouncing/throttling appropriato.
        
        Args:
            mode: 'fast' (throttle 60fps), 'normal' (debounce), 'immediate'
        """
        self.update_scheduler.schedule_update(self.update, mode)
    
    def resizeEvent(self, event) -> None:  # type: ignore[override]
        """Gestisce ridimensionamento del widget."""
        super().resizeEvent(event)
        self.viewport.update_dimensions(self.width(), self.duration_ms)
        self._invalidate_cache()
        self._schedule_update('normal')

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
        painter.fillRect(ruler_rect, QColor("#808080")) # Grigio Lupo

        if self.duration_ms == 0:
            painter.setPen(QColor("#C19A6B")) # Desert Tan
            font = QFont('Arial', 10, QFont.Weight.Bold)
            painter.setFont(font)
            painter.drawText(ruler_rect, Qt.AlignmentFlag.AlignCenter, "NO VIDEO")
            return

        # 2. Disegna la barra di progresso
        current_x = self._timestamp_to_x(self.current_position_ms, width)
        progress_width = max(0, current_x - 10)
        progress_rect = QRect(10, self.ruler_y_pos, progress_width, self.ruler_height)
        painter.fillRect(progress_rect, QColor("#5F6F52")) # Verde Ranger

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
        """Disegna i marker (triangoli sopra) con linea verticale e timestamp sotto.
        
        OTTIMIZZATO: Usa spatial index per disegnare solo markers visibili.
        """
        if not self.marker_manager:
            return

        # Ottimizzazione: disegna solo markers visibili
        visible_markers = self._get_visible_markers()
        
        if not visible_markers:
            return  # Early exit se nessun marker visibile

        # Colore Giallo/Oro del bordo della finestra
        marker_color = QColor("#C19A6B") 

        font = QFont('Arial', 8)
        painter.setFont(font)
        metrics = QFontMetrics(font)

        # Per evitare sovrapposizioni dei timestamp
        drawn_timestamps_rects: List[QRect] = []

        for marker in visible_markers:  # Solo markers visibili!
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
        
        playhead_color = QColor("#C19A6B") # Giallo Bordo
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
        """Gestisce movimento del mouse per hover.
        
        OTTIMIZZATO: Hover updates debounced per evitare repaint continui.
        """
        # Schedula verifica hover con debouncing
        self._schedule_hover_check(event.pos())

    def _schedule_hover_check(self, pos: QPoint) -> None:
        """Schedula verifica hover con debouncing.
        
        Evita repaint e tooltip updates continui durante movimento rapido del mouse.
        """
        if not hasattr(self, '_hover_timer'):
            self._hover_timer = QTimer(self)
            self._hover_timer.setSingleShot(True)
            self._hover_timer.timeout.connect(self._check_hover)
        
        # Salva posizione per check successivo
        self._pending_hover_pos = pos
        
        # Debounce a 50ms
        self._hover_timer.start(50)

    def _check_hover(self) -> None:
        """Verifica hover marker alla posizione salvata."""
        if not hasattr(self, '_pending_hover_pos'):
            return
            
        pos = self._pending_hover_pos
        marker = self._get_marker_at_position(pos)

        if marker != self.hover_marker:
            self.hover_marker = marker
            self._schedule_update()  # Throttled update invece di self.update()

            # Mostra tooltip
            if marker:
                # Il tempo è già visualizzato sotto, quindi il tooltip mostra solo descrizione/categoria
                time_str_long = self._format_time_long(marker.timestamp)
                tooltip_text = f"<b>MARKER</b><br><span style='font-size:11pt; color:#C19A6B;'>{time_str_long}</span>"
                if marker.description:
                    tooltip_text += f"<br><i>{marker.description}</i>"
                if marker.category != 'default':
                    tooltip_text += f"<br>📂 {marker.category.title()}"
                
                # Usa QCursor.pos() invece di event.globalPosition() che non abbiamo più
                from PyQt6.QtGui import QCursor
                QToolTip.showText(QCursor.pos(), tooltip_text, self)
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
        """Trova marker (triangolo) sotto il cursore.
        
        OTTIMIZZATO: Usa spatial index per trovare marker vicino in O(log n).
        """
        if not self.marker_manager:
            return None

        width = self.width()
        
        # Le coordinate ora sono relative alla nuova ruler_y_pos
        hit_y_start = self.ruler_y_pos - 5 # 5px buffer sopra la base
        hit_y_end = self.ruler_y_pos + self.marker_height + 5 # 5px buffer sotto la punta

        if not (hit_y_start <= pos.y() <= hit_y_end):
             return None # Cursore non è sulla riga dei marker

        # Converti X in timestamp
        timestamp = self._x_to_timestamp(pos.x(), width)
        
        # Usa spatial index per trovare marker più vicino
        # Distanza massima: metà larghezza marker + buffer
        max_distance_ms = int((self.marker_width / 2 + 3) * self.duration_ms / (width - 20))
        
        nearest_marker = self.marker_index.find_nearest(timestamp, max_distance_ms)
        
        if nearest_marker:
            # Verifica che sia effettivamente in hit range
            marker_x = self._timestamp_to_x(nearest_marker.timestamp, width)
            hit_x_start = marker_x - (self.marker_width // 2) - 3
            hit_x_end = marker_x + (self.marker_width // 2) + 3
            
            if hit_x_start <= pos.x() <= hit_x_end:
                return nearest_marker
        
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
        label.setStyleSheet("color: #0047AB; font-weight: bold;")
        layout.addWidget(label)

        # Pulsante aggiungi marker
        self.btn_add = QPushButton("+ Marker")
        self.btn_add.setToolTip("Aggiungi marker alla posizione corrente (Ctrl+M)")
        self.btn_add.clicked.connect(self.add_marker_requested.emit)
        self.btn_add.setStyleSheet("""
            QPushButton {
                background-color: #5F6F52;
                color: #ffffff;
                border: 1px solid #798969;
                border-radius: 3px;
                padding: 5px 10px;
                font-weight: bold;
            }
            QPushButton:hover {
                background-color: #798969;
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

        # --- NUOVI CONTROLLI EXPORT ---
        # Label "Export:"
        export_label = QLabel("📤 Export:")
        export_label.setStyleSheet("color: #C19A6B; font-weight: bold;")
        layout.addWidget(export_label)

        # Spinbox secondi PRIMA
        label_before = QLabel("Prima:")
        label_before.setStyleSheet("color: #cccccc;")
        layout.addWidget(label_before)

        from PyQt6.QtWidgets import QSpinBox
        self.spin_before = QSpinBox()
        self.spin_before.setRange(0, 300)  # Max 5 minuti
        self.spin_before.setValue(5)       # Default 5 secondi
        self.spin_before.setSuffix("s")
        self.spin_before.setMinimumWidth(65)
        self.spin_before.setMaximumWidth(65)
        self.spin_before.setToolTip("Secondi prima del marker")
        self.spin_before.setStyleSheet("""
            QSpinBox {
                background-color: #808080;
                color: #ffffff;
                border: 1px solid #808080;
                border-radius: 3px;
                padding: 3px;
            }
        """)
        layout.addWidget(self.spin_before)

        # Spinbox secondi DOPO
        label_after = QLabel("Dopo:")
        label_after.setStyleSheet("color: #cccccc;")
        layout.addWidget(label_after)

        self.spin_after = QSpinBox()
        self.spin_after.setRange(0, 300)
        self.spin_after.setValue(5)        # Default 5 secondi
        self.spin_after.setSuffix("s")
        self.spin_after.setMinimumWidth(65)
        self.spin_after.setMaximumWidth(65)
        self.spin_after.setToolTip("Secondi dopo il marker")
        self.spin_after.setStyleSheet("""
            QSpinBox {
                background-color: #808080;
                color: #ffffff;
                border: 1px solid #808080;
                border-radius: 3px;
                padding: 3px;
            }
        """)
        layout.addWidget(self.spin_after)

        # Pulsante esportazione video da markers
        self.btn_export = QPushButton("📤 Esporta")
        self.btn_export.setToolTip("Esporta video da markers (Ctrl+E)")
        self.btn_export.clicked.connect(self.export_markers_requested.emit)
        self.btn_export.setStyleSheet("""
            QPushButton {
                background-color: #0047AB;
                color: #ffffff;
                border: 1px solid #1A5EC4;
                border-radius: 3px;
                padding: 5px 10px;
                font-weight: bold;
            }
            QPushButton:hover {
                background-color: #1A5EC4;
            }
        """)
        layout.addWidget(self.btn_export)

        # Stile generale
        self.setStyleSheet("""
            QPushButton {
                background-color: #252527;
                color: #cccccc;
                border: 1px solid #808080;
                border-radius: 3px;
                padding: 5px 10px;
            }
            QPushButton:hover {
                background-color: #808080;
                color: #ffffff;
            }
        """)

        self.setLayout(layout)

    def get_export_times(self):
        """Ritorna i secondi prima e dopo per l'export."""
        return self.spin_before.value(), self.spin_after.value()

