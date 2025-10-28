"""
Dialog per la configurazione dell'esportazione clip.
"""

from PyQt6.QtWidgets import (QDialog, QVBoxLayout, QHBoxLayout, QLabel, 
                             QPushButton, QDialogButtonBox, QWidget, 
                             QLineEdit, QFileDialog)
from PyQt6.QtCore import Qt
from pathlib import Path
from typing import Optional

class ExportDialog(QDialog):
    """Dialog per selezionare la directory di destinazione delle clip."""
    
    def __init__(self, default_dir: Path, parent: Optional[QWidget] = None):
        super().__init__(parent)
        self.setWindowTitle("Scegli Directory di Esportazione")
        self.setModal(True)
        self.selected_dir = default_dir
        self.setup_ui()
        
    def setup_ui(self):
        """Configura l'interfaccia del dialog."""
        layout = QVBoxLayout(self)
        
        # Stile
        self.setStyleSheet("""
            QDialog {
                background-color: #1C1C1E;
                color: #e0e0e0;
            }
            QLabel {
                color: #e0e0e0;
                font-size: 13px;
            }
            QLineEdit {
                background-color: #808080;
                color: #ffffff;
                border: 1px solid #808080;
                border-radius: 3px;
                padding: 5px;
                font-size: 14px;
            }
            QPushButton {
                background-color: #5F6F52;
                color: #ffffff;
                border: 1px solid #798969;
                border-radius: 3px;
                padding: 8px 15px;
                font-weight: bold;
            }
            QPushButton:hover {
                background-color: #798969;
            }
        """)
        
        # Titolo
        title = QLabel("Scegli Destinazione Esportazione")
        title.setStyleSheet("font-size: 16px; font-weight: bold; color: #C19A6B; margin-bottom: 10px;")
        layout.addWidget(title)
        
        # Info
        info = QLabel(
            "Seleziona la directory dove salvare le clip esportate dai markers."
        )
        info.setWordWrap(True)
        info.setStyleSheet("color: #c0c0c0; margin: 10px 0;")
        layout.addWidget(info)
        
        # Directory selector
        dir_layout = QHBoxLayout()
        dir_layout.setSpacing(10)
        
        dir_label = QLabel("Directory:")
        dir_layout.addWidget(dir_label)
        
        self.dir_input = QLineEdit()
        self.dir_input.setText(str(self.selected_dir))
        self.dir_input.setReadOnly(True)
        dir_layout.addWidget(self.dir_input, 1)
        
        self.btn_browse = QPushButton("📁 Sfoglia")
        self.btn_browse.clicked.connect(self.browse_directory)
        self.btn_browse.setStyleSheet("""
            QPushButton {
                background-color: #0047AB;
                color: #ffffff;
                border: 1px solid #1A5EC4;
                border-radius: 3px;
                padding: 5px 15px;
                font-weight: bold;
            }
            QPushButton:hover {
                background-color: #1A5EC4;
            }
        """)
        dir_layout.addWidget(self.btn_browse)
        
        layout.addLayout(dir_layout)
        
        # Separator
        layout.addSpacing(20)
        
        # Bottoni OK/Cancel
        button_box = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok | 
            QDialogButtonBox.StandardButton.Cancel
        )
        
        ok_button = button_box.button(QDialogButtonBox.StandardButton.Ok)
        if ok_button:
            ok_button.setText("✓")
            ok_button.setStyleSheet(
                "background-color: #5F6F52; border: 1px solid #798969; font-size: 18px; padding: 8px 20px;"
            )
        
        cancel_button = button_box.button(QDialogButtonBox.StandardButton.Cancel)
        if cancel_button:
            cancel_button.setText("✗")
            cancel_button.setStyleSheet(
                "background-color: #B80F0A; border: 1px solid #D41813; font-size: 18px; padding: 8px 20px;"
            )
        
        button_box.accepted.connect(self.accept)
        button_box.rejected.connect(self.reject)
        layout.addWidget(button_box)
        
        # Dimensioni
        self.setMinimumWidth(550)
    
    def browse_directory(self):
        """Apre il dialog per selezionare la directory."""
        directory = QFileDialog.getExistingDirectory(
            self,
            "Seleziona Directory di Esportazione",
            str(self.selected_dir),
            QFileDialog.Option.ShowDirsOnly
        )
        
        if directory:
            self.selected_dir = Path(directory)
            self.dir_input.setText(str(self.selected_dir))
    
    def get_directory(self) -> Path:
        """Ritorna la directory selezionata."""
        return self.selected_dir
    
    @staticmethod
    def get_export_directory(default_dir: Path, parent: Optional[QWidget] = None) -> Optional[Path]:
        """Metodo statico per aprire il dialog e ottenere la directory."""
        dialog = ExportDialog(default_dir, parent)
        if dialog.exec() == QDialog.DialogCode.Accepted:
            return dialog.get_directory()
        return None

