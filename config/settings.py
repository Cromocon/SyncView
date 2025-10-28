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

# Tema UI - Palette Tattica SyncView
THEME_COLORS = {
    # Colori Base
    "nero_tattico": "#1C1C1E",      # Sfondo principale opaco
    "desert_tan": "#C19A6B",        # Sostituzione giallo/warning/accent
    "verde_ranger": "#5F6F52",      # Sostituzione verde/success
    "grigio_lupo": "#808080",       # Elementi secondari/disabilitati
    "rosso_squadra": "#B80F0A",     # Errori/azioni distruttive
    "blu_squadra": "#0047AB",       # Info/link/elementi interattivi
    
    # Alias per compatibilità
    "primary": "#1C1C1E",           # nero_tattico
    "secondary": "#808080",         # grigio_lupo
    "accent": "#C19A6B",            # desert_tan
    "text": "#E0E0E0",              # Testo principale (bianco sporco)
    "text_secondary": "#808080",    # Testo secondario (grigio_lupo)
    "border": "#808080",            # Bordi (grigio_lupo)
    "error": "#B80F0A",             # rosso_squadra
    "warning": "#C19A6B",           # desert_tan
    "success": "#5F6F52",           # verde_ranger
    "info": "#0047AB",              # blu_squadra
    
    # Varianti Hover/Pressed (30% più chiare/scure)
    "desert_tan_hover": "#D4B088",
    "verde_ranger_hover": "#798969",
    "grigio_lupo_hover": "#999999",
    "rosso_squadra_hover": "#D41813",
    "blu_squadra_hover": "#1A5EC4",
    
    # Background varianti
    "bg_hover": "#2A2A2C",
    "bg_pressed": "#353537",
    "bg_input": "#252527",
}

