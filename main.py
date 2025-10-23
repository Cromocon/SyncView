#!/usr/bin/env python3
"""
SyncView - Tactical Multi-Video Analysis (T-MVA)
Main Application Entry Point
"""

import sys
from pathlib import Path

# Aggiungi il percorso del progetto al PYTHONPATH
project_root = Path(__file__).parent
sys.path.insert(0, str(project_root))

from PyQt6.QtWidgets import QApplication
from core.logger import logger

def main():
    """Funzione principale dell'applicazione."""
    app = QApplication(sys.argv)
    app.setApplicationName("SyncView")
    app.setOrganizationName("T-MVA")
    
    logger.log_user_action("Sistema avviato", "SyncView v2.5")
    
    # Carica la finestra principale
    from ui.main_window import MainWindow
    window = MainWindow()
    window.show()
    
    return app.exec()

if __name__ == "__main__":
    sys.exit(main())
