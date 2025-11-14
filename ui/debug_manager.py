"""
Modulo per la gestione della modalità debug dell'interfaccia utente.
"""

from PyQt6.QtWidgets import QWidget, QLayout, QPushButton
from PyQt6.QtCore import QEvent, QObject, Qt, QRectF
from PyQt6.QtGui import QPainter, QPen, QColor, QBrush, QPainterPath
from typing import Set, Dict

from core.logger import logger

class DebugLayoutManager:
    """
    Helper per tracciare e gestire i layout per la modalità debug.
    """
    _instance = None
    layouts: Set[QLayout]

    def __new__(cls):
        if cls._instance is None:
            cls._instance = super().__new__(cls)
            cls._instance.layouts = set()
        return cls._instance

    def add_layout(self, layout: QLayout):
        self.layouts.add(layout)

    def get_all_layouts(self) -> Set[QLayout]:
        return self.layouts

    def clear(self):
        self.layouts.clear()

    @staticmethod
    def find_and_register_layouts(widget: QWidget):
        manager = DebugLayoutManager()
        widgets_to_check = [widget] + widget.findChildren(QWidget)
        for w in widgets_to_check:
            layout = w.layout()
            if layout is not None:
                manager.add_layout(layout)


class DebugManager(QObject):
    """
    Gestisce l'attivazione, la disattivazione e la logica della modalità debug.
    """
    def __init__(self, main_window: QWidget):
        super().__init__(main_window)
        self.main_window = main_window
        self.is_enabled = False
        self.original_tooltips: Dict[QWidget, str] = {}

    def toggle(self):
        """Attiva o disattiva la modalità debug."""
        self.is_enabled = not self.is_enabled
        if self.is_enabled:
            self.enable()
        else:
            self.disable()

    def enable(self):
        """Attiva la modalità debug."""
        logger.log_user_action("Debug Mode", "ATTIVATO")
        self.original_tooltips.clear()
        DebugLayoutManager().clear()
        DebugLayoutManager.find_and_register_layouts(self.main_window)
        self._save_and_clear_tooltips(self.main_window)
        self._install_event_filter_recursive(self.main_window)

    def disable(self):
        """Disattiva la modalità debug."""
        logger.log_user_action("Debug Mode", "DISATTIVATO")
        self._remove_event_filter_recursive(self.main_window)
        self._restore_tooltips()
        self.original_tooltips.clear()
        for layout in DebugLayoutManager().get_all_layouts():
            parent_widget = layout.parentWidget()
            if parent_widget:
                parent_widget.update()
        DebugLayoutManager().clear()

    def _save_and_clear_tooltips(self, widget: QWidget):
        widgets = [widget] + widget.findChildren(QWidget)
        for w in widgets:
            tooltip = w.toolTip()
            if tooltip:
                self.original_tooltips[w] = tooltip
                w.setToolTip("")

    def _restore_tooltips(self):
        for widget, tooltip in self.original_tooltips.items():
            try:
                widget.setToolTip(tooltip)
            except RuntimeError:
                pass

    def _install_event_filter_recursive(self, widget: QWidget):
        widgets = [widget] + widget.findChildren(QWidget)
        for w in widgets:
            w.installEventFilter(self)

    def _remove_event_filter_recursive(self, widget: QWidget):
        widgets = [widget] + widget.findChildren(QWidget)
        for w in widgets:
            w.removeEventFilter(self)

    def eventFilter(self, watched: QObject, event: QEvent) -> bool: # type: ignore[override]
        if not self.is_enabled or not isinstance(watched, QWidget):
            return super().eventFilter(watched, event)

        obj: QWidget = watched # Now we can safely use 'obj' as a QWidget
        if event.type() == QEvent.Type.Enter:
            size = obj.size()
            obj_name = obj.objectName() or obj.__class__.__name__
            nickname = obj.property("nickname")
            tooltip = f"DEBUG: {obj_name} ({obj.__class__.__name__})"
            if nickname:
                tooltip += f"\nNickname: {nickname}"
            tooltip += f"\nSize: {size.width()}x{size.height()}"
            geom = obj.geometry()
            tooltip += f" | Geo: {geom.x()},{geom.y()} {geom.width()}x{geom.height()}"
            parent = obj.parent()
            if parent:
                tooltip += f" | Parent: {parent.objectName() or parent.__class__.__name__}"
            obj.setToolTip(tooltip)

        elif event.type() == QEvent.Type.Leave:
            obj.setToolTip("")

        elif event.type() == QEvent.Type.Paint:
            result = super().eventFilter(watched, event)
            painter = QPainter(obj)

            if obj.parentWidget():
                painter.setRenderHint(QPainter.RenderHint.Antialiasing)

                path = QPainterPath()
                path.addRect(QRectF(obj.rect()))
                path.addRect(QRectF(obj.contentsRect()))
                path.setFillRule(Qt.FillRule.OddEvenFill)
                painter.fillPath(path, QBrush(QColor(0, 100, 255, 70)))

                border_pen = QPen(QColor(255, 0, 0, 200))
                border_pen.setWidth(1)
                painter.setPen(border_pen)
                painter.setBrush(Qt.BrushStyle.NoBrush)
                painter.drawRect(obj.rect().adjusted(0, 0, -1, -1))

            painter.end()
            return result

        return super().eventFilter(watched, event)