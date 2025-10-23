"""
Configurazione globale dell'applicazione SyncView.
"""

from pathlib import Path

# Percorsi del progetto
PROJECT_ROOT = Path(__file__).parent.parent
# --- MODIFICA: Aggiunto percorso a requirements.txt ---
REQUIREMENTS_FILE = PROJECT_ROOT / "requirements.txt"
# ----------------------------------------------------
SOURCE_DIRS = [
    PROJECT_ROOT / "Feed-1",
    PROJECT_ROOT / "Feed-2",
    PROJECT_ROOT / "Feed-3",
    PROJECT_ROOT / "Feed-4"
]
EXPORT_DIR = PROJECT_ROOT / "Salvataggi"
LOG_FILE = PROJECT_ROOT / "syncview_log.txt"
DEVELOPER_LOG = PROJECT_ROOT / "DEVELOPER_LOG.md"

# --- MODIFICA: Assicura che la directory di esportazione esista ---
EXPORT_DIR.mkdir(parents=True, exist_ok=True)
# -----------------------------------------------------------------

# Configurazione video
MAX_VIDEOS = 4
SUPPORTED_VIDEO_FORMATS = ['.mp4', '.avi', '.mov', '.mkv', '.wmv', '.flv']

# Configurazione playback
# --- MODIFICA: Ripristinate le voci FPS originali ---
DEFAULT_FPS_OPTIONS = ["Auto", "24 fps", "25 fps", "29.97 fps", "30 fps", "50 fps", "59.94 fps", "60 fps", "Personalizzato"]
FRAME_STEP_OPTIONS = [40, 100, 200] # Mantenuto per frame stepping

# Configurazione esportazione
DEFAULT_EXPORT_WINDOW = 5

# Dipendenze richieste
REQUIRED_PACKAGES = [
    "PyQt6",
    "moviepy",
    "numpy",
    "PIL"
]

# Tema UI - Stile militare/tattico
THEME_COLORS = {
    "primary": "#1a1a1a",
    "secondary": "#2d2d2d",
    "accent": "#3d7a4a",
    "text": "#b0b0b0",
    "border": "#3a3a3a",
    "error": "#a84444",
    "warning": "#b88c44",
    "success": "#4d8559"
}

