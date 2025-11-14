"""
Funzioni di utilità e helper per SyncView.
"""

import sys
import shutil


def check_dependencies():
    """
    Controlla lo stato delle dipendenze del sistema.
    Ritorna un dizionario con lo stato di ogni dipendenza.
    """
    status = {
        'all_ok': True,
        'python_version': '',
        'pyqt6': False,
        'moviepy': False,
        'numpy': False,
        'pillow': False,
        'ffmpeg': False,
        'errors': []
    }
    
    # Controlla versione Python
    python_version = f"{sys.version_info.major}.{sys.version_info.minor}.{sys.version_info.micro}"
    status['python_version'] = python_version
    
    # Controlla librerie
    libs = {'pyqt6': 'PyQt6', 'moviepy': 'moviepy', 'numpy': 'numpy', 'pillow': 'PIL'}
    for key, lib_name in libs.items():
        try:
            __import__(lib_name)
            status[key] = True
        except ImportError:
            status['all_ok'] = False
            status['errors'].append(f'{lib_name} non trovato')
    
    # Controlla FFmpeg
    if shutil.which('ffmpeg') is not None:
        status['ffmpeg'] = True
    else:
        status['all_ok'] = False
        status['errors'].append('FFmpeg non trovato (raccomandato)')
    
    return status


def generate_dependency_tooltip():
    """Genera il tooltip dinamico per l'indicatore di sistema."""
    deps = check_dependencies()
    
    if deps['all_ok']:
        tooltip = "✅ TUTTE LE DIPENDENZE INSTALLATE CORRETTAMENTE\n\n"
        tooltip += f"🐍 Python: {deps['python_version']}\n"
        tooltip += "✓ PyQt6: Installato\n"
        tooltip += "✓ moviepy: Installato\n"
        tooltip += "✓ numpy: Installato\n"
        tooltip += "✓ Pillow: Installato\n"
        tooltip += "✓ FFmpeg: Disponibile\n"
        tooltip += "\nSistema pronto per l'uso completo!"
    else:
        tooltip = "⚠️ ATTENZIONE: ALCUNE DIPENDENZE MANCANTI\n\n"
        tooltip += f"🐍 Python: {deps['python_version']}\n"
        
        # Mappa per generare le righe
        lib_map = {
            'pyqt6': ('PyQt6', ''),
            'moviepy': ('moviepy', ' (export video non disponibile)'),
            'numpy': ('numpy', ''),
            'pillow': ('Pillow', ''),
            'ffmpeg': ('FFmpeg', ' (raccomandato per FPS detection)')
        }
        
        for key, (name, note) in lib_map.items():
            if deps[key]:
                tooltip += f"✓ {name}: Installato\n"
            else:
                icon = '✗' if key != 'ffmpeg' else '⚠'
                tooltip += f"{icon} {name}: NON TROVATO{note}\n"
        
        tooltip += "\n❌ PROBLEMI RILEVATI:\n"
        for error in deps['errors']:
            tooltip += f"  • {error}\n"
        
        tooltip += "\n💡 SOLUZIONE:\n"
        tooltip += "Esegui: pip install -r requirements.txt\n"
        if not deps['ffmpeg']:
            tooltip += "Per FFmpeg: sudo apt install ffmpeg (Linux)\n"
            tooltip += "           brew install ffmpeg (macOS)\n"
            tooltip += "           winget install FFmpeg (Windows)"
    
    return tooltip


def format_time(ms: int) -> str:
    """Formatta millisecondi in HH:MM:SS.mmm."""
    if not isinstance(ms, (int, float)) or ms < 0:
        return "00:00:00.000"
        
    total_seconds = ms / 1000
    hours = int(total_seconds // 3600)
    minutes = int((total_seconds % 3600) // 60)
    seconds = int(total_seconds % 60)
    millis = int(ms % 1000)
    return f"{hours:02d}:{minutes:02d}:{seconds:02d}.{millis:03d}"