"""
Stili CSS per l'interfaccia tattica/militare di SyncView.
"""

from config.settings import THEME_COLORS

def get_main_stylesheet():
    """Ritorna il foglio di stile principale dell'applicazione."""
    return f"""
    /* Stile generale dell'applicazione */
    QMainWindow {{
        background-color: {THEME_COLORS['primary']};
        color: {THEME_COLORS['text']};
    }}
    
    QWidget {{
        background-color: {THEME_COLORS['primary']};
        color: {THEME_COLORS['text']};
        font-family: 'Monospace', 'Courier New';
    }}
    
    /* Pulsanti */
    QPushButton {{
        background-color: {THEME_COLORS['secondary']};
        color: {THEME_COLORS['text']};
        border: 2px solid {THEME_COLORS['border']};
        border-radius: 4px;
        padding: 8px 16px;
        font-weight: bold;
        min-width: 80px;
    }}
    
    QPushButton:hover {{
        background-color: #3d3d3d;
        border-color: {THEME_COLORS['accent']};
    }}
    
    QPushButton:pressed {{
        background-color: #1a1a1a;
        border-color: {THEME_COLORS['accent']};
    }}
    
    QPushButton:disabled {{
        background-color: #1a1a1a;
        color: #666666;
        border-color: #2d2d2d;
    }}
    
    QPushButton#playButton, QPushButton#pauseButton {{
        background-color: #2a4a2a;
        border-color: {THEME_COLORS['accent']};
        min-width: 100px;
    }}
    
    QPushButton#playButton:hover, QPushButton#pauseButton:hover {{
        background-color: #355f35;
    }}
    
    QPushButton#stopButton {{
        background-color: #4a2a2a;
        border-color: {THEME_COLORS['error']};
    }}
    
    QPushButton#stopButton:hover {{
        background-color: #5f3535;
    }}
    
    /* Bottoni video player */
    QPushButton#loadVideoButton {{
        background-color: #2a4a2a;
        border-color: {THEME_COLORS['accent']};
        min-width: 70px;
        padding: 5px 10px;
        font-size: 11px;
    }}
    
    QPushButton#loadVideoButton:hover {{
        background-color: #355f35;
        border-color: #5fb573;
    }}
    
    QPushButton#removeVideoButton {{
        background-color: #4a2a2a;
        border-color: {THEME_COLORS['error']};
        min-width: 70px;
        padding: 5px 10px;
        font-size: 11px;
    }}
    
    QPushButton#removeVideoButton:hover {{
        background-color: #5f3535;
        border-color: #e87070;
    }}
    
    /* ComboBox */
    QComboBox {{
        background-color: {THEME_COLORS['secondary']};
        color: {THEME_COLORS['text']};
        border: 2px solid {THEME_COLORS['border']};
        border-radius: 4px;
        padding: 5px 10px;
        min-width: 100px;
    }}
    
    QComboBox:hover {{
        border-color: {THEME_COLORS['accent']};
    }}
    
    QComboBox::drop-down {{
        border: none;
        padding-right: 10px;
    }}
    
    QComboBox::down-arrow {{
        image: none;
        border-left: 5px solid transparent;
        border-right: 5px solid transparent;
        border-top: 5px solid {THEME_COLORS['accent']};
        margin-right: 5px;
    }}
    
    QComboBox QAbstractItemView {{
        background-color: {THEME_COLORS['secondary']};
        color: {THEME_COLORS['text']};
        border: 2px solid {THEME_COLORS['accent']};
        selection-background-color: #355f35;
    }}
    
    /* Labels */
    QLabel {{
        color: {THEME_COLORS['text']};
        font-weight: bold;
    }}
    
    QLabel#headerLabel {{
        color: {THEME_COLORS['accent']};
        font-size: 18px;
        font-weight: bold;
        padding: 10px;
    }}
    
    QLabel#statusLabel {{
        color: {THEME_COLORS['warning']};
        font-size: 12px;
        padding: 5px;
    }}
    
    /* Slider */
    QSlider::groove:horizontal {{
        background: {THEME_COLORS['secondary']};
        height: 8px;
        border-radius: 4px;
        border: 1px solid {THEME_COLORS['border']};
    }}
    
    QSlider::handle:horizontal {{
        background: {THEME_COLORS['accent']};
        width: 16px;
        height: 16px;
        margin: -5px 0;
        border-radius: 8px;
        border: 2px solid {THEME_COLORS['primary']};
    }}
    
    QSlider::handle:horizontal:hover {{
        background: #5fb573;
    }}
    
    QSlider::sub-page:horizontal {{
        background: {THEME_COLORS['accent']};
        border-radius: 4px;
    }}
    
    /* GroupBox */
    QGroupBox {{
        color: {THEME_COLORS['accent']};
        border: 2px solid {THEME_COLORS['border']};
        border-radius: 5px;
        margin-top: 10px;
        font-weight: bold;
        padding-top: 10px;
    }}
    
    QGroupBox::title {{
        subcontrol-origin: margin;
        subcontrol-position: top left;
        padding: 0 5px;
        color: {THEME_COLORS['accent']};
    }}
    
    /* CheckBox */
    QCheckBox {{
        color: {THEME_COLORS['text']};
        spacing: 8px;
    }}
    
    QCheckBox::indicator {{
        width: 18px;
        height: 18px;
        border: 2px solid {THEME_COLORS['border']};
        border-radius: 3px;
        background-color: {THEME_COLORS['secondary']};
    }}
    
    QCheckBox::indicator:checked {{
        background-color: {THEME_COLORS['accent']};
        border-color: {THEME_COLORS['accent']};
    }}
    
    QCheckBox::indicator:hover {{
        border-color: {THEME_COLORS['accent']};
    }}
    
    /* SpinBox */
    QSpinBox, QDoubleSpinBox {{
        background-color: {THEME_COLORS['secondary']};
        color: {THEME_COLORS['text']};
        border: 2px solid {THEME_COLORS['border']};
        border-radius: 4px;
        padding: 5px;
    }}
    
    QSpinBox:hover, QDoubleSpinBox:hover {{
        border-color: {THEME_COLORS['accent']};
    }}
    
    /* ScrollBar */
    QScrollBar:vertical {{
        background: {THEME_COLORS['secondary']};
        width: 12px;
        border: 1px solid {THEME_COLORS['border']};
    }}
    
    QScrollBar::handle:vertical {{
        background: {THEME_COLORS['accent']};
        min-height: 20px;
        border-radius: 5px;
    }}
    
    QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {{
        height: 0px;
    }}
    
    /* Frame per video */
    QFrame#videoFrame {{
        background-color: #000000;
        border: 2px solid {THEME_COLORS['accent']};
        border-radius: 5px;
    }}
    
    QFrame#videoFrame:hover {{
        border-color: #5fb573;
        border-width: 3px;
    }}
    
    /* ToolTip */
    QToolTip {{
        background-color: {THEME_COLORS['secondary']};
        color: {THEME_COLORS['text']};
        border: 2px solid {THEME_COLORS['accent']};
        padding: 5px;
        border-radius: 3px;
    }}
    
    /* Menu */
    QMenuBar {{
        background-color: {THEME_COLORS['secondary']};
        color: {THEME_COLORS['text']};
        border-bottom: 2px solid {THEME_COLORS['border']};
    }}
    
    QMenuBar::item {{
        padding: 5px 10px;
    }}
    
    QMenuBar::item:selected {{
        background-color: #355f35;
        color: {THEME_COLORS['accent']};
    }}
    
    QMenu {{
        background-color: {THEME_COLORS['secondary']};
        color: {THEME_COLORS['text']};
        border: 2px solid {THEME_COLORS['border']};
    }}
    
    QMenu::item {{
        padding: 5px 30px 5px 20px;
    }}
    
    QMenu::item:selected {{
        background-color: #355f35;
    }}
    
    /* Dialog */
    QDialog {{
        background-color: {THEME_COLORS['primary']};
        color: {THEME_COLORS['text']};
    }}
    
    /* TextEdit */
    QTextEdit, QPlainTextEdit {{
        background-color: {THEME_COLORS['secondary']};
        color: {THEME_COLORS['text']};
        border: 2px solid {THEME_COLORS['border']};
        border-radius: 4px;
        padding: 5px;
    }}
    
    /* ProgressBar */
    QProgressBar {{
        background-color: {THEME_COLORS['secondary']};
        border: 2px solid {THEME_COLORS['border']};
        border-radius: 4px;
        text-align: center;
        color: {THEME_COLORS['text']};
    }}
    
    QProgressBar::chunk {{
        background-color: {THEME_COLORS['accent']};
        border-radius: 2px;
    }}
    """

def get_video_player_stylesheet():
    """Ritorna lo stile per i player video."""
    return f"""
    QFrame {{
        background-color: #000000;
        border: 2px solid {THEME_COLORS['accent']};
        border-radius: 5px;
    }}
    
    QLabel#placeholderLabel {{
        color: {THEME_COLORS['text']};
        font-size: 14px;
        background-color: #0a0a0a;
    }}
    
    QLabel#errorLabel {{
        color: {THEME_COLORS['error']};
        font-size: 12px;
        background-color: #1a0a0a;
    }}
    """
