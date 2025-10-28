"""
Dialog per la gestione avanzata dei markers.
"""

from PyQt6.QtWidgets import (QDialog, QVBoxLayout, QHBoxLayout, QTableWidget,
                             QTableWidgetItem, QPushButton, QLabel, QComboBox,
                             QGroupBox, QHeaderView, QMessageBox,
                             QFileDialog, QInputDialog)
from PyQt6.QtCore import Qt
from PyQt6.QtGui import QColor
from pathlib import Path

from core.markers import MarkerManager, Marker


class MarkerManagerDialog(QDialog):
    """Dialog per visualizzare e gestire tutti i markers."""
    
    def __init__(self, marker_manager: MarkerManager, parent=None):
        super().__init__(parent)
        
        self.marker_manager = marker_manager
        self.filtered_markers = list(marker_manager.markers)
        
        self.setWindowTitle("Gestione Markers - SyncView")
        self.setMinimumSize(900, 600)
        
        self.setup_ui()
        self.load_markers()
        
        # Stile
        self.setStyleSheet("""
            QDialog {
                background-color: #1C1C1E;
            }
            QLabel {
                color: #cccccc;
            }
            QTableWidget {
                background-color: #252527;
                color: #ffffff;
                gridline-color: #808080;
                border: 1px solid #808080;
            }
            QTableWidget::item:selected {
                background-color: #0047AB;
            }
            QHeaderView::section {
                background-color: #252527;
                color: #0047AB;
                border: 1px solid #808080;
                padding: 5px;
                font-weight: bold;
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
            QPushButton:pressed {
                background-color: #5F6F52;
            }
            QComboBox, QLineEdit {
                background-color: #252527;
                color: #ffffff;
                border: 1px solid #808080;
                border-radius: 3px;
                padding: 5px;
            }
        """)
    
    def setup_ui(self):
        """Configura l'interfaccia utente."""
        layout = QVBoxLayout()
        
        # Header con statistiche
        stats_group = QGroupBox("📊 Statistiche")
        stats_layout = QHBoxLayout()
        
        self.stats_label = QLabel()
        stats_layout.addWidget(self.stats_label)
        stats_layout.addStretch()
        
        stats_group.setLayout(stats_layout)
        layout.addWidget(stats_group)
        
        # Filtri
        filter_group = QGroupBox("🔍 Filtri")
        filter_layout = QHBoxLayout()
        
        filter_layout.addWidget(QLabel("Categoria:"))
        self.category_filter = QComboBox()
        self.category_filter.addItem("Tutte", None)
        for category in MarkerManager.DEFAULT_CATEGORIES:
            self.category_filter.addItem(category.title(), category)
        self.category_filter.currentIndexChanged.connect(self.apply_filters)
        filter_layout.addWidget(self.category_filter)
        
        filter_layout.addStretch()
        
        filter_group.setLayout(filter_layout)
        layout.addWidget(filter_group)
        
        # Tabella markers
        self.table = QTableWidget()
        self.table.setColumnCount(5)
        self.table.setHorizontalHeaderLabels([
            "Timestamp", "Tempo", "Categoria", "Colore", "Descrizione"
        ])
        self.table.horizontalHeader().setSectionResizeMode(QHeaderView.ResizeMode.Interactive)
        self.table.horizontalHeader().setSectionResizeMode(4, QHeaderView.ResizeMode.Stretch)
        self.table.setSelectionBehavior(QTableWidget.SelectionBehavior.SelectRows)
        self.table.setSelectionMode(QTableWidget.SelectionMode.SingleSelection)
        self.table.itemDoubleClicked.connect(self.edit_marker)
        
        layout.addWidget(self.table, 1)
        
        # Bottoni azioni
        actions_layout = QHBoxLayout()
        
        btn_add = QPushButton("➕ Aggiungi")
        btn_add.clicked.connect(self.add_marker)
        actions_layout.addWidget(btn_add)
        
        btn_edit = QPushButton("✏️ Modifica")
        btn_edit.clicked.connect(self.edit_selected_marker)
        actions_layout.addWidget(btn_edit)
        
        btn_delete = QPushButton("🗑️ Elimina")
        btn_delete.setStyleSheet("""
            QPushButton {
                background-color: #B80F0A;
                border: 1px solid #D41813;
            }
            QPushButton:hover {
                background-color: #D41813;
            }
        """)
        btn_delete.clicked.connect(self.delete_selected_marker)
        actions_layout.addWidget(btn_delete)
        
        actions_layout.addStretch()
        
        btn_export = QPushButton("📤 Esporta CSV")
        btn_export.clicked.connect(self.export_csv)
        actions_layout.addWidget(btn_export)
        
        btn_clear = QPushButton("🗑️ Cancella Tutti")
        btn_clear.setStyleSheet("""
            QPushButton {
                background-color: #B80F0A;
                border: 1px solid #D41813;
            }
            QPushButton:hover {
                background-color: #D41813;
            }
        """)
        btn_clear.clicked.connect(self.clear_all_markers)
        actions_layout.addWidget(btn_clear)
        
        layout.addLayout(actions_layout)
        
        # Bottoni chiusura
        bottom_layout = QHBoxLayout()
        bottom_layout.addStretch()
        
        btn_close = QPushButton("Chiudi")
        btn_close.clicked.connect(self.accept)
        bottom_layout.addWidget(btn_close)
        
        layout.addLayout(bottom_layout)
        
        self.setLayout(layout)
    
    def load_markers(self):
        """Carica i markers nella tabella."""
        self.table.setRowCount(0)
        
        for marker in self.filtered_markers:
            row = self.table.rowCount()
            self.table.insertRow(row)
            
            # Timestamp
            self.table.setItem(row, 0, QTableWidgetItem(str(marker.timestamp)))
            
            # Tempo formattato
            time_str = self._format_time(marker.timestamp)
            self.table.setItem(row, 1, QTableWidgetItem(time_str))
            
            # Categoria
            self.table.setItem(row, 2, QTableWidgetItem(marker.category))
            
            # Colore (con visual)
            color_item = QTableWidgetItem(marker.color)
            color_item.setBackground(QColor(marker.color))
            self.table.setItem(row, 3, color_item)
            
            # Descrizione
            self.table.setItem(row, 4, QTableWidgetItem(marker.description or ""))
            
            # Salva marker nell'item per recupero
            self.table.item(row, 0).setData(Qt.ItemDataRole.UserRole, marker)
        
        # Aggiorna statistiche
        self.update_statistics()
    
    def update_statistics(self):
        """Aggiorna le statistiche visualizzate."""
        stats = self.marker_manager.get_statistics()
        
        text = f"<b>Totale markers:</b> {stats['total']} | "
        
        if stats['by_category']:
            text += "<b>Per categoria:</b> "
            for cat, count in stats['by_category'].items():
                text += f"{cat}: {count}, "
            text = text.rstrip(", ")
        
        self.stats_label.setText(text)
    
    def apply_filters(self):
        """Applica i filtri selezionati."""
        category = self.category_filter.currentData()
        
        self.filtered_markers = []
        
        for marker in self.marker_manager.markers:
            # Filtro categoria
            if category and marker.category != category:
                continue
            
            self.filtered_markers.append(marker)
        
        self.load_markers()
    
    def get_selected_marker(self) -> Marker | None:
        """Ottiene il marker selezionato."""
        selected = self.table.selectedItems()
        if not selected:
            return None
        
        row = selected[0].row()
        return self.table.item(row, 0).data(Qt.ItemDataRole.UserRole)
    
    def add_marker(self):
        """Aggiunge un nuovo marker (placeholder - in pratica si usa Ctrl+M durante playback)."""
        QMessageBox.information(
            self,
            "Aggiungi Marker",
            "Per aggiungere un marker, usa Ctrl+M durante la riproduzione del video.\n\n"
            "Il marker verrà creato alla posizione corrente del video."
        )
    
    def edit_marker(self, item):
        """Modifica un marker tramite doppio click."""
        self.edit_selected_marker()
    
    def edit_selected_marker(self):
        """Modifica il marker selezionato."""
        marker = self.get_selected_marker()
        if not marker:
            QMessageBox.warning(self, "Attenzione", "Nessun marker selezionato.")
            return
        
        # Dialog descrizione
        new_desc, ok = QInputDialog.getText(
            self,
            "Modifica Descrizione",
            "Descrizione:",
            text=marker.description or ""
        )
        
        if ok and marker.id:
            # Aggiorna marker
            self.marker_manager.update_marker(
                marker.id,
                description=new_desc
            )
            self.load_markers()
    
    def delete_selected_marker(self):
        """Elimina il marker selezionato."""
        marker = self.get_selected_marker()
        if not marker:
            QMessageBox.warning(self, "Attenzione", "Nessun marker selezionato.")
            return
        
        time_str = self._format_time(marker.timestamp)
        reply = QMessageBox.question(
            self,
            "Conferma Eliminazione",
            f"Eliminare il marker al timestamp {time_str}?",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
        )
        
        if reply == QMessageBox.StandardButton.Yes and marker.id:
            self.marker_manager.remove_marker(marker.id)
            self.apply_filters()
    
    def clear_all_markers(self):
        """Cancella tutti i markers."""
        if self.marker_manager.count == 0:
            QMessageBox.information(self, "Info", "Nessun marker da eliminare.")
            return
        
        reply = QMessageBox.question(
            self,
            "Conferma Cancellazione",
            f"Eliminare TUTTI i {self.marker_manager.count} markers?\n\nQuesta operazione non può essere annullata!",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
        )
        
        if reply == QMessageBox.StandardButton.Yes:
            self.marker_manager.clear_all()
            self.load_markers()
            QMessageBox.information(self, "Completato", "Tutti i markers sono stati eliminati.")
    
    def export_csv(self):
        """Esporta i markers in CSV."""
        if self.marker_manager.count == 0:
            QMessageBox.warning(self, "Attenzione", "Nessun marker da esportare.")
            return
        
        file_path, _ = QFileDialog.getSaveFileName(
            self,
            "Esporta Markers CSV",
            "syncview_markers.csv",
            "CSV Files (*.csv)"
        )
        
        if file_path:
            success = self.marker_manager.export_csv(Path(file_path))
            if success:
                QMessageBox.information(
                    self,
                    "Esportazione Completata",
                    f"Markers esportati con successo in:\n{file_path}"
                )
            else:
                QMessageBox.critical(
                    self,
                    "Errore",
                    "Errore durante l'esportazione del file CSV."
                )
    
    def _format_time(self, ms: int) -> str:
        """Formatta millisecondi in HH:MM:SS.mmm."""
        total_seconds = ms / 1000
        hours = int(total_seconds // 3600)
        minutes = int((total_seconds % 3600) // 60)
        seconds = int(total_seconds % 60)
        millis = int(ms % 1000)
        return f"{hours:02d}:{minutes:02d}:{seconds:02d}.{millis:03d}"
