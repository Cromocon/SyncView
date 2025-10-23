"""
Sistema di logging per SyncView.
Gestisce il logging di runtime e lo sviluppo.
"""

import logging
from datetime import datetime
from config.settings import LOG_FILE, DEVELOPER_LOG

class SyncViewLogger:
    """Gestisce il logging dell'applicazione."""
    
    def __init__(self):
        # Pulisci il file di log all'avvio
        if LOG_FILE.exists():
            LOG_FILE.unlink()
        
        self.logger = logging.getLogger('SyncView')
        self.logger.setLevel(logging.DEBUG)
        
        # Rimuovi handler esistenti
        self.logger.handlers.clear()
        
        # Handler per file
        file_handler = logging.FileHandler(LOG_FILE, encoding='utf-8')
        file_handler.setLevel(logging.DEBUG)
        
        # Handler per console
        console_handler = logging.StreamHandler()
        console_handler.setLevel(logging.INFO)
        
        # Formato
        formatter = logging.Formatter(
            '%(asctime)s - %(name)s - %(levelname)s - %(message)s',
            datefmt='%Y-%m-%d %H:%M:%S'
        )
        file_handler.setFormatter(formatter)
        console_handler.setFormatter(formatter)
        
        self.logger.addHandler(file_handler)
        self.logger.addHandler(console_handler)
        
        # Scrivi intestazione di avvio
        self._write_startup_header()
    
    def _write_startup_header(self):
        """Scrive l'intestazione di avvio nel log."""
        header = f"\n{'=' * 70}\n"
        header += f"  SYNCVIEW - AVVIO APPLICAZIONE\n"
        header += f"  Timestamp: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n"
        header += f"{'=' * 70}\n"
        
        with open(LOG_FILE, 'a', encoding='utf-8') as f:
            f.write(header)
        
        self.logger.info("Applicazione SyncView avviata")
    
    def log_dependency_check(self, missing_packages):
        """Registra il risultato del controllo dipendenze."""
        if not missing_packages:
            self.logger.info("✓ Tutte le dipendenze sono installate")
        else:
            self.logger.warning(f"✗ Dipendenze mancanti: {', '.join(missing_packages)}")
    
    def log_user_action(self, action, details=""):
        """Registra un'azione dell'utente."""
        msg = f"[AZIONE UTENTE] {action}"
        if details:
            msg += f" - {details}"
        self.logger.info(msg)
    
    def log_video_action(self, video_index, action, details=""):
        """Registra un'azione su un video specifico."""
        msg = f"[VIDEO {video_index + 1}] {action}"
        if details:
            msg += f" - {details}"
        self.logger.info(msg)
    
    def log_playback(self, video_index, state):
        """Registra cambio stato riproduzione di un video."""
        self.logger.info(f"[VIDEO {video_index + 1}] Stato riproduzione: {state}")
    
    def log_timeline_seek(self, video_index, position_ms):
        """Registra uno spostamento sulla timeline."""
        seconds = position_ms / 1000
        minutes = int(seconds // 60)
        secs = int(seconds % 60)
        self.logger.info(f"[VIDEO {video_index + 1}] Timeline seek: {minutes:02d}:{secs:02d} ({position_ms}ms)")
    
    def log_error(self, error_msg, exception=None):
        """Registra un errore."""
        self.logger.error(error_msg)
        if exception:
            self.logger.exception(exception)
    
    def log_export(self, video_name, success=True, error_msg=""):
        """Registra un'operazione di esportazione."""
        if success:
            self.logger.info(f"✓ Esportazione completata: {video_name}")
        else:
            self.logger.error(f"✗ Esportazione fallita: {video_name} - {error_msg}")
    
    @staticmethod
    def add_developer_log(entry):
        """Aggiunge un'entrata al log di sviluppo."""
        timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        with open(DEVELOPER_LOG, 'a', encoding='utf-8') as f:
            f.write(f"\n### {timestamp}\n{entry}\n\n---\n")

# Istanza globale
logger = SyncViewLogger()
