"""
Widget video con supporto zoom e pan per SyncView.
Permette zoom con Ctrl+Scroll e pan con drag&drop quando zoomato.
"""

from PyQt6.QtWidgets import QGraphicsView, QGraphicsScene
from PyQt6.QtMultimediaWidgets import QGraphicsVideoItem
from PyQt6.QtCore import Qt, QPointF, QSizeF, QRectF, pyqtSignal
from PyQt6.QtGui import QTransform
from core.logger import logger


class ZoomableVideoWidget(QGraphicsView):
    """QGraphicsView con QGraphicsVideoItem per zoom e pan."""
    
    zoom_changed = pyqtSignal(float)
    pan_changed = pyqtSignal(float, float)
    
    def __init__(self, parent=None):
        super().__init__(parent)
        
        self._scene = QGraphicsScene(self)
        self.setScene(self._scene)
        
        self.video_item = QGraphicsVideoItem()
        self._scene.addItem(self.video_item)
        
        # Imposta aspect ratio mode per mantenere le proporzioni
        self.video_item.setAspectRatioMode(Qt.AspectRatioMode.KeepAspectRatio)
        
        self.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self.setFrameShape(QGraphicsView.Shape.NoFrame)
        self.setStyleSheet("background-color: black;")
        
        self.zoom_level = 1.0
        self.min_zoom = 1.0
        self.max_zoom = 5.0
        self.zoom_step = 0.1
        
        self.is_panning = False
        self.last_mouse_pos = QPointF()
        
        self.setMouseTracking(True)
        self._update_cursor()
        
        # Imposta dimensione iniziale del video item per riempire la vista
        self._fit_video_to_view()
        
    def wheelEvent(self, event):
        if not event:
            return
        
        if event.modifiers() & Qt.KeyboardModifier.ControlModifier:
            delta = event.angleDelta().y()
            
            if delta > 0:
                new_zoom = min(self.max_zoom, self.zoom_level + self.zoom_step)
            else:
                new_zoom = max(self.min_zoom, self.zoom_level - self.zoom_step)
            
            if new_zoom != self.zoom_level:
                old_zoom = self.zoom_level
                
                # Salva il punto della scena sotto il mouse prima dello zoom
                mouse_pos_scene_raw = self.mapToScene(event.position().toPoint())
                
                # --- FIX: Limita il punto di zoom ai bordi del video ---
                # Questo evita che lo zoom "scappi" se il mouse è fuori dal video.
                video_rect_scene = self.video_item.sceneBoundingRect()
                
                # Limita le coordinate del mouse ai bordi del video
                clamped_x = max(video_rect_scene.left(), min(mouse_pos_scene_raw.x(), video_rect_scene.right()))
                clamped_y = max(video_rect_scene.top(), min(mouse_pos_scene_raw.y(), video_rect_scene.bottom()))
                
                # Usa il punto "clamped" (limitato) come target per lo zoom
                zoom_target_scene = QPointF(clamped_x, clamped_y)
                # ---------------------------------------------------------
                
                self.zoom_level = new_zoom
                self._apply_transform()
                
                if self.zoom_level == self.min_zoom:
                    # Se torniamo allo zoom minimo, centra sempre sul video
                    self.centerOn(self.video_item)
                elif old_zoom >= self.min_zoom:
                    # Altrimenti, centra sul punto target calcolato (che sia dentro o sul bordo del video)
                    self.centerOn(zoom_target_scene)
                else:
                    # Altrimenti zoom al centro del video
                    self.centerOn(self.video_item)
                
                self.zoom_changed.emit(self.zoom_level)
                self._update_cursor()
                
                logger.log_user_action(
                    f"Zoom {'in' if delta > 0 else 'out'}",
                    f"Livello: {self.zoom_level:.1f}x"
                )
            
            event.accept()
        else:
            super().wheelEvent(event)
    
    def mousePressEvent(self, event):
        if not event:
            return
        
        if event.button() == Qt.MouseButton.LeftButton and self.zoom_level > self.min_zoom:
            self.is_panning = True
            self.last_mouse_pos = event.position()
            self.setCursor(Qt.CursorShape.ClosedHandCursor)
            event.accept()
        else:
            super().mousePressEvent(event)
    
    def mouseMoveEvent(self, event):
        if not event:
            return
        
        if self.is_panning:
            current_pos = event.position()
            delta_x = current_pos.x() - self.last_mouse_pos.x()
            delta_y = current_pos.y() - self.last_mouse_pos.y()
            
            self.horizontalScrollBar().setValue(  # type: ignore
                self.horizontalScrollBar().value() - int(delta_x)  # type: ignore
            )
            self.verticalScrollBar().setValue(  # type: ignore
                self.verticalScrollBar().value() - int(delta_y)  # type: ignore
            )
            
            self.last_mouse_pos = current_pos
            
            center = self.mapToScene(self.viewport().rect().center())  # type: ignore
            self.pan_changed.emit(center.x(), center.y())
            
            event.accept()
        else:
            super().mouseMoveEvent(event)
    
    def mouseReleaseEvent(self, event):
        if not event:
            return
        
        if event.button() == Qt.MouseButton.LeftButton and self.is_panning:
            self.is_panning = False
            self._update_cursor()
            event.accept()
        else:
            super().mouseReleaseEvent(event)
    
    def _apply_transform(self):
        transform = QTransform()
        transform.scale(self.zoom_level, self.zoom_level)
        
        self.video_item.setTransform(transform)
        
        item_rect = self.video_item.boundingRect()
        scaled_rect = transform.mapRect(item_rect)
        self.setSceneRect(scaled_rect)
        
        if self.zoom_level > self.min_zoom:
            self.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
            self.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
        else:
            self.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
            self.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        
        if self.zoom_level == self.min_zoom:
            self.centerOn(self.video_item)
    
    def _update_cursor(self):
        if self.zoom_level > self.min_zoom and not self.is_panning:
            self.setCursor(Qt.CursorShape.OpenHandCursor)
        elif not self.is_panning:
            self.setCursor(Qt.CursorShape.ArrowCursor)
    
    def reset_zoom_pan(self):
        if self.zoom_level != self.min_zoom:
            self.zoom_level = self.min_zoom
            
            # Prima fa il fit del video alla vista
            self._fit_video_to_view()
            
            self._apply_transform()
            self.centerOn(self.video_item)
            
            self.zoom_changed.emit(self.zoom_level)
            center = self.mapToScene(self.viewport().rect().center())  # type: ignore
            self.pan_changed.emit(center.x(), center.y())
            self._update_cursor()
            
            logger.log_user_action("Zoom/Pan reset", "Ritorno a 100%")
        else:
            # Se già a zoom minimo, forza comunque il fit
            self._fit_video_to_view()
            self.centerOn(self.video_item)
    
    def get_zoom_info(self):
        if self.zoom_level <= self.min_zoom:
            return "100%"
        return f"{int(self.zoom_level * 100)}%"
    
    def _fit_video_to_view(self):
        """Ridimensiona il video item per riempire la vista mantenendo l'aspect ratio."""
        view_rect = self.viewport().rect()  # type: ignore
        
        if view_rect.width() <= 0 or view_rect.height() <= 0:
            return
        
        # Imposta la dimensione del video item uguale alla vista
        self.video_item.setSize(QSizeF(view_rect.width(), view_rect.height()))
        
        # Aggiorna la scena per contenere il video
        self.setSceneRect(QRectF(0, 0, view_rect.width(), view_rect.height()))
        
        # Centra il video nella vista
        self.centerOn(self.video_item)
    
    def resizeEvent(self, event):  # type: ignore[override]
        super().resizeEvent(event)
        
        # Quando la vista viene ridimensionata, adatta il video solo se non zoomato
        if self.zoom_level == self.min_zoom:
            self._fit_video_to_view()
