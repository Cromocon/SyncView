"""
Dialog per la selezione di FPS personalizzato.
"""

from PyQt6.QtWidgets import (QDialog, QVBoxLayout, QHBoxLayout, QLabel, 
                             QPushButton, QDoubleSpinBox, QDialogButtonBox)
from PyQt6.QtCore import Qt

from ui.styles import get_main_stylesheet


class FPSDialog(QDialog):
    """Dialog per inserire un FPS personalizzato."""
    
    def __init__(self, current_fps=25.0, parent=None):
        super().__init__(parent)
        self.setWindowTitle("FPS Personalizzato")
        self.setModal(True)
        self.custom_fps = current_fps
        self.setup_ui()
        
    def setup_ui(self):
        """Configura l'interfaccia del dialog."""
        self.setStyleSheet(get_main_stylesheet())
        layout = QVBoxLayout(self)
        
        # Titolo
        title = QLabel("Imposta FPS Personalizzato")
        title.setStyleSheet("font-size: 14px; font-weight: bold; color: #C19A6B;")
        layout.addWidget(title)
        
        # Info
        info = QLabel("Inserisci il valore FPS desiderato per la riproduzione video.")
        info.setWordWrap(True)
        info.setStyleSheet("color: #E0E0E0; margin: 10px 0;")
        layout.addWidget(info)
        
        # Input FPS
        input_layout = QHBoxLayout()
        input_label = QLabel("FPS:")
        input_layout.addWidget(input_label)
        
        self.fps_spinbox = QDoubleSpinBox()
        self.fps_spinbox.setRange(1.0, 240.0)
        self.fps_spinbox.setValue(self.custom_fps)
        self.fps_spinbox.setDecimals(3)
        self.fps_spinbox.setSingleStep(0.001)
        self.fps_spinbox.setSuffix(" fps")
        self.fps_spinbox.setMinimumWidth(150)
        input_layout.addWidget(self.fps_spinbox)
        
        layout.addLayout(input_layout)
        
        # Preset comuni
        preset_label = QLabel("Preset Comuni:")
        preset_label.setStyleSheet("font-weight: bold; color: #C19A6B; margin-top: 15px;")
        layout.addWidget(preset_label)
        
        presets_layout = QHBoxLayout()
        presets = [
            ("23.976", 23.976),
            ("24", 24.0),
            ("25", 25.0),
            ("29.97", 29.970),
            ("30", 30.0),
            ("50", 50.0),
            ("60", 60.0)
        ]
        
        for name, value in presets:
            btn = QPushButton(name)
            btn.setMinimumWidth(60)
            btn.clicked.connect(lambda checked, v=value: self.fps_spinbox.setValue(v))
            presets_layout.addWidget(btn)
        
        layout.addLayout(presets_layout)
        
        # Separator
        layout.addSpacing(20)
        
        # Bottoni OK/Cancel
        button_box = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok | 
            QDialogButtonBox.StandardButton.Cancel
        )
        button_box.accepted.connect(self.accept)
        button_box.rejected.connect(self.reject)
        layout.addWidget(button_box)
        
        # Dimensioni
        self.setMinimumWidth(500)
        self.setMaximumWidth(600)
    
    def get_fps(self):
        """Ritorna l'FPS selezionato."""
        return self.fps_spinbox.value()
    
    @staticmethod
    def get_custom_fps(current_fps=25.0, parent=None):
        """Metodo statico per aprire il dialog e ottenere l'FPS."""
        dialog = FPSDialog(current_fps, parent)
        if dialog.exec() == QDialog.DialogCode.Accepted:
            return dialog.get_fps()
        return None
