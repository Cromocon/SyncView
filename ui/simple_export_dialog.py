"""
Simplified Export Dialog - Solo destinazione e qualità.
"""

from PyQt6.QtWidgets import (QDialog, QVBoxLayout, QHBoxLayout, QLabel, 
                             QPushButton, QDialogButtonBox, QWidget, 
                             QLineEdit, QFileDialog, QComboBox, QGroupBox)
from PyQt6.QtCore import Qt
from pathlib import Path
from typing import Optional
import json

from core.advanced_exporter import ExportQuality
from config.settings import EXPORT_SETTINGS_FILE
from config.user_paths import user_path_manager


class ExportSettings:
    """Gestisce il caricamento e salvataggio delle impostazioni di export."""
    
    @staticmethod
    def load() -> dict:
        """Carica le impostazioni salvate."""
        # Usa user_path_manager per la directory
        last_dir = user_path_manager.get_export_dir()
        if not last_dir:
            last_dir = Path.home()
        
        default_settings = {
            'last_directory': str(last_dir),
            'last_quality': ExportQuality.MEDIUM.name
        }
        
        try:
            if EXPORT_SETTINGS_FILE.exists():
                with open(EXPORT_SETTINGS_FILE, 'r') as f:
                    saved = json.load(f)
                    # Merge solo per quality (directory viene da user_path_manager)
                    if 'last_quality' in saved:
                        default_settings['last_quality'] = saved['last_quality']
        except Exception:
            pass  # Usa default se caricamento fallisce
        
        return default_settings
    
    @staticmethod
    def save(directory: Path, quality: ExportQuality) -> None:
        """Salva le impostazioni."""
        try:
            # Salva directory in user_path_manager
            user_path_manager.set_export_dir(directory)
            
            # Salva solo quality in EXPORT_SETTINGS_FILE
            EXPORT_SETTINGS_FILE.parent.mkdir(parents=True, exist_ok=True)
            settings = {
                'last_quality': quality.name
            }
            with open(EXPORT_SETTINGS_FILE, 'w') as f:
                json.dump(settings, f, indent=2)
        except Exception:
            pass  # Ignora errori di salvataggio


class SimpleExportDialog(QDialog):
    """Dialog semplificato per export - Solo destinazione e qualità."""
    
    def __init__(self, parent: Optional[QWidget] = None):
        super().__init__(parent)
        self.setWindowTitle("Configurazione Export")
        self.setModal(True)
        
        # Carica impostazioni salvate
        self.settings = ExportSettings.load()
        self.selected_dir = Path(self.settings['last_directory'])
        
        self.setup_ui()
        
    def setup_ui(self):
        """Configura l'interfaccia del dialog."""
        layout = QVBoxLayout(self)
        layout.setSpacing(15)
        
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
                font-size: 12px;
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
            QComboBox {
                background-color: #808080;
                color: #ffffff;
                border: 1px solid #808080;
                border-radius: 3px;
                padding: 5px;
            }
            QComboBox::drop-down {
                border: none;
            }
            QComboBox::down-arrow {
                image: url(none);
                border-left: 5px solid transparent;
                border-right: 5px solid transparent;
                border-top: 5px solid #ffffff;
                margin-right: 5px;
            }
            QGroupBox {
                border: 2px solid #808080;
                border-radius: 5px;
                margin-top: 10px;
                padding-top: 10px;
                font-weight: bold;
                color: #C19A6B;
            }
            QGroupBox::title {
                subcontrol-origin: margin;
                left: 10px;
                padding: 0 5px;
            }
        """)
        
        # Titolo
        title = QLabel("⚙ Configurazione Export")
        title.setStyleSheet("font-size: 18px; font-weight: bold; color: #C19A6B; margin-bottom: 10px;")
        layout.addWidget(title)
        
        # Info sistema
        info_label = QLabel(
            "🚀 Export automatizzato con:\n"
            "• Hardware acceleration (se disponibile)\n"
            "• Export parallelo multi-core\n"
            "• Retry automatico (3 tentativi)"
        )
        info_label.setStyleSheet("color: #5F6F52; font-size: 11px; margin-bottom: 10px; padding: 10px; background-color: #252527; border-radius: 3px;")
        layout.addWidget(info_label)
        
        # === SEZIONE DESTINAZIONE ===
        dir_group = QGroupBox("📁 Destinazione")
        dir_layout = QVBoxLayout(dir_group)
        
        dir_row = QHBoxLayout()
        dir_row.setSpacing(10)
        
        self.dir_input = QLineEdit()
        self.dir_input.setText(str(self.selected_dir))
        self.dir_input.setReadOnly(True)
        dir_row.addWidget(self.dir_input, 1)
        
        btn_browse = QPushButton("📂 Sfoglia")
        btn_browse.clicked.connect(self.browse_directory)
        dir_row.addWidget(btn_browse)
        
        dir_layout.addLayout(dir_row)
        layout.addWidget(dir_group)
        
        # === SEZIONE QUALITÀ ===
        quality_group = QGroupBox("🎨 Qualità Video")
        quality_layout = QVBoxLayout(quality_group)
        
        self.quality_combo = QComboBox()
        self.quality_combo.addItem("⚡ Fast Preview - Anteprime veloci", 
                                  ExportQuality.FAST_PREVIEW.name)
        self.quality_combo.addItem("⚖ Medium - Bilanciato [Consigliato]", 
                                  ExportQuality.MEDIUM.name)
        self.quality_combo.addItem("⭐ High - Alta qualità", 
                                  ExportQuality.HIGH.name)
        self.quality_combo.addItem("💎 Best - Massima qualità (lento)", 
                                  ExportQuality.BEST.name)
        
        # Imposta qualità salvata
        saved_quality = self.settings['last_quality']
        for i in range(self.quality_combo.count()):
            if self.quality_combo.itemData(i) == saved_quality:
                self.quality_combo.setCurrentIndex(i)
                break
        
        quality_layout.addWidget(self.quality_combo)
        
        quality_info = QLabel(
            "• Fast: Ideale per anteprime rapide\n"
            "• Medium: Ottimo bilanciamento qualità/velocità\n"
            "• High/Best: Per rendering finali di alta qualità"
        )
        quality_info.setStyleSheet("color: #b0b0b0; font-size: 11px; margin-top: 5px;")
        quality_layout.addWidget(quality_info)
        
        layout.addWidget(quality_group)
        
        # Separator
        layout.addSpacing(10)
        
        # === BOTTONI OK/CANCEL ===
        button_box = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok | 
            QDialogButtonBox.StandardButton.Cancel
        )
        
        ok_button = button_box.button(QDialogButtonBox.StandardButton.Ok)
        if ok_button:
            ok_button.setText("🚀 Avvia Export")
            ok_button.setStyleSheet(
                "background-color: #5F6F52; border: 1px solid #798969; "
                "padding: 10px 25px; font-size: 14px;"
            )
        
        cancel_button = button_box.button(QDialogButtonBox.StandardButton.Cancel)
        if cancel_button:
            cancel_button.setText("✖ Annulla")
            cancel_button.setStyleSheet(
                "background-color: #B80F0A; border: 1px solid #D41813; "
                "padding: 10px 25px; font-size: 14px;"
            )
        
        button_box.accepted.connect(self.accept)
        button_box.rejected.connect(self.reject)
        layout.addWidget(button_box)
        
        # Dimensioni
        self.setMinimumWidth(550)
        self.setMinimumHeight(450)
    
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
    
    def get_export_config(self) -> dict:
        """Ritorna la configurazione selezionata e salva le preferenze."""
        quality_name = self.quality_combo.currentData()
        quality = ExportQuality[quality_name]
        
        # Salva le preferenze per la prossima volta
        ExportSettings.save(self.selected_dir, quality)
        
        return {
            'directory': self.selected_dir,
            'quality': quality
        }
    
    @staticmethod
    def get_export_config_simple(parent: Optional[QWidget] = None) -> Optional[dict]:
        """Metodo statico per aprire il dialog e ottenere la configurazione."""
        dialog = SimpleExportDialog(parent)
        if dialog.exec() == QDialog.DialogCode.Accepted:
            return dialog.get_export_config()
        return None
