"""
Configurazione globale dell'applicazione SyncView.
"""
import logging
from pathlib import Path

# Percorsi del progetto
PROJECT_ROOT = Path(__file__).parent.parent
# --- MODIFICA: Aggiunto percorso a requirements.txt ---
REQUIREMENTS_FILE = PROJECT_ROOT / "requirements.txt"
# ----------------------------------------------------

# Configurazione video
MAX_VIDEOS = 4
SUPPORTED_VIDEO_FORMATS = ['.mp4', '.avi', '.mov', '.mkv', '.wmv', '.flv']

# Configurazione playback
# --- MODIFICA: Ripristinate le voci FPS originali ---
DEFAULT_FPS_OPTIONS = ["Auto", "24 fps", "25 fps", "29.97 fps", "30 fps", "50 fps", "59.94 fps", "60 fps", "Personalizzato"]
FRAME_STEP_OPTIONS = [40, 100, 200] # Mantenuto per frame stepping

# Configurazione esportazione
DEFAULT_EXPORT_WINDOW = 5
EXPORT_SETTINGS_FILE = PROJECT_ROOT / "export_settings.json"  # File per salvare preferenze export
LOG_FILE = PROJECT_ROOT / "syncview_log.txt"
DEVELOPER_LOG = PROJECT_ROOT / "DEVELOPER_LOG.md"

# Impostazioni di Logging
LOG_LEVEL_FILE = logging.DEBUG
LOG_LEVEL_CONSOLE = logging.INFO
LOG_MAX_BYTES = 5 * 1024 * 1024  # 5 MB
LOG_BACKUP_COUNT = 3  # Mantiene syncview.log e 3 backup (es. syncview.log.1)

# Dipendenze richieste
REQUIRED_PACKAGES = [
    "PyQt6",
    "moviepy",
    "numpy",
    "PIL"
]

# Tema UI - Palette Tattica "Night Ops"
THEME_COLORS = {
    # --- Sfondi ---
    "bg_base": "#1a1a1a",           # Sfondo principale, quasi nero
    "bg_surface": "#2c2c2c",        # Sfondo per elementi "in superficie" (es. input, groupbox)
    "bg_surface_hover": "#3a3a3a",  # Hover per superfici
    
    # --- Testo ---
    "text_primary": "#e0e0e0",      # Testo principale, bianco sporco
    "text_secondary": "#a0a0a0",    # Testo secondario, grigio chiaro
    "text_disabled": "#6e6e6e",     # Testo per elementi disabilitati
    
    # --- Bordi ---
    "border_primary": "#444444",    # Bordo standard per elementi
    "border_accent": "#d4a373",     # Bordo per focus o hover (sabbia)
    
    # --- Accenti Funzionali ---
    "accent_primary": "#d4a373",    # Colore principale per accenti (sabbia/oro)
    "accent_positive": "#6a994e",   # Verde oliva per azioni positive (play, ok)
    "accent_negative": "#bc4749",   # Rosso mattone per azioni negative (delete, error)
    "accent_info": "#6c757d",       # Grigio-blu per informazioni
    
    # --- Varianti Hover ---
    "accent_primary_hover": "#e6b88a",
    "accent_positive_hover": "#8aae6f",
    "accent_negative_hover": "#d46a6c",
    
    # --- Alias per retrocompatibilità (se necessario) ---
    "text": "#e0e0e0",
    "error": "#bc4749",
    "success": "#6a994e",
}
