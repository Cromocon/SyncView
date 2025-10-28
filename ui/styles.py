"""
Stili CSS per l'interfaccia tattica/militare di SyncView.
"""

from config.settings import THEME_COLORS

def get_main_stylesheet():
    """Ritorna il foglio di stile principale dell'applicazione."""
    return f"""
    /* Stile generale dell'applicazione */
    QMainWindow {{
        background-color: {THEME_COLORS['nero_tattico']};
        color: {THEME_COLORS['text']};
    }}
    
    QWidget {{
        background-color: {THEME_COLORS['nero_tattico']};
        color: {THEME_COLORS['text']};
        font-family: 'Monospace', 'Courier New';
    }}
    
    /* Pulsanti */
    QPushButton {{
        background-color: {THEME_COLORS['bg_input']};
        color: {THEME_COLORS['text']};
        border: 2px solid {THEME_COLORS['grigio_lupo']};
        border-radius: 4px;
        padding: 8px 16px;
        font-weight: bold;
        min-width: 80px;
    }}
    
    QPushButton:hover {{
        background-color: {THEME_COLORS['bg_hover']};
        border-color: {THEME_COLORS['desert_tan']};
    }}
    
    QPushButton:pressed {{
        background-color: {THEME_COLORS['bg_pressed']};
        border-color: {THEME_COLORS['desert_tan_hover']};
    }}
    
    QPushButton:disabled {{
        background-color: {THEME_COLORS['nero_tattico']};
        color: {THEME_COLORS['grigio_lupo']};
        border-color: {THEME_COLORS['grigio_lupo']};
    }}
    
    QPushButton#playButton, QPushButton#pauseButton {{
        background-color: {THEME_COLORS['verde_ranger']};
        border-color: {THEME_COLORS['verde_ranger_hover']};
        min-width: 100px;
    }}
    
    QPushButton#playButton:hover, QPushButton#pauseButton:hover {{
        background-color: {THEME_COLORS['verde_ranger_hover']};
    }}
    
    QPushButton#stopButton {{
        background-color: {THEME_COLORS['rosso_squadra']};
        border-color: {THEME_COLORS['rosso_squadra_hover']};
    }}
    
    QPushButton#stopButton:hover {{
        background-color: {THEME_COLORS['rosso_squadra_hover']};
    }}
    
    /* Bottoni video player */
    QPushButton#loadVideoButton {{
        background-color: {THEME_COLORS['verde_ranger']};
        border-color: {THEME_COLORS['verde_ranger_hover']};
        min-width: 70px;
        padding: 5px 10px;
        font-size: 11px;
    }}
    
    QPushButton#loadVideoButton:hover {{
        background-color: {THEME_COLORS['verde_ranger_hover']};
    }}
    
    QPushButton#removeVideoButton {{
        background-color: {THEME_COLORS['rosso_squadra']};
        border-color: {THEME_COLORS['rosso_squadra_hover']};
        min-width: 70px;
        padding: 5px 10px;
        font-size: 11px;
    }}
    
    QPushButton#removeVideoButton:hover {{
        background-color: {THEME_COLORS['rosso_squadra_hover']};
    }}
    
    /* ComboBox */
    QComboBox {{
        background-color: {THEME_COLORS['bg_input']};
        color: {THEME_COLORS['text']};
        border: 2px solid {THEME_COLORS['grigio_lupo']};
        border-radius: 4px;
        padding: 5px 10px;
        min-width: 100px;
    }}
    
    QComboBox:hover {{
        border-color: {THEME_COLORS['desert_tan']};
    }}
    
    QComboBox::drop-down {{
        border: none;
        padding-right: 10px;
    }}
    
    QComboBox::down-arrow {{
        image: none;
        border-left: 5px solid transparent;
        border-right: 5px solid transparent;
        border-top: 5px solid {THEME_COLORS['desert_tan']};
        margin-right: 5px;
    }}
    
    QComboBox QAbstractItemView {{
        background-color: {THEME_COLORS['bg_input']};
        color: {THEME_COLORS['text']};
        border: 2px solid {THEME_COLORS['desert_tan']};
        selection-background-color: {THEME_COLORS['verde_ranger']};
    }}
    
    /* Labels */
    QLabel {{
        color: {THEME_COLORS['text']};
        font-weight: bold;
    }}
    
    QLabel#headerLabel {{
        color: {THEME_COLORS['desert_tan']};
        font-size: 18px;
        font-weight: bold;
        padding: 10px;
    }}
    
    QLabel#statusLabel {{
        color: {THEME_COLORS['desert_tan']};
        font-size: 12px;
        padding: 5px;
    }}
    
    /* Slider */
    QSlider::groove:horizontal {{
        background: {THEME_COLORS['bg_input']};
        height: 8px;
        border-radius: 4px;
        border: 1px solid {THEME_COLORS['grigio_lupo']};
    }}
    
    QSlider::handle:horizontal {{
        background: {THEME_COLORS['desert_tan']};
        width: 16px;
        height: 16px;
        margin: -5px 0;
        border-radius: 8px;
        border: 2px solid {THEME_COLORS['nero_tattico']};
    }}
    
    QSlider::handle:horizontal:hover {{
        background: {THEME_COLORS['desert_tan_hover']};
    }}
    
    QSlider::sub-page:horizontal {{
        background: {THEME_COLORS['verde_ranger']};
        border-radius: 4px;
    }}
    
    /* GroupBox */
    QGroupBox {{
        color: {THEME_COLORS['desert_tan']};
        border: 2px solid {THEME_COLORS['grigio_lupo']};
        border-radius: 5px;
        margin-top: 10px;
        font-weight: bold;
        padding-top: 10px;
    }}
    
    QGroupBox::title {{
        subcontrol-origin: margin;
        subcontrol-position: top left;
        padding: 0 5px;
        color: {THEME_COLORS['desert_tan']};
    }}
    
    /* CheckBox */
    QCheckBox {{
        color: {THEME_COLORS['text']};
        spacing: 8px;
    }}
    
    QCheckBox::indicator {{
        width: 18px;
        height: 18px;
        border: 2px solid {THEME_COLORS['grigio_lupo']};
        border-radius: 3px;
        background-color: {THEME_COLORS['bg_input']};
    }}
    
    QCheckBox::indicator:checked {{
        background-color: {THEME_COLORS['verde_ranger']};
        border-color: {THEME_COLORS['verde_ranger_hover']};
    }}
    
    QCheckBox::indicator:hover {{
        border-color: {THEME_COLORS['desert_tan']};
    }}
    
    /* SpinBox */
    QSpinBox, QDoubleSpinBox {{
        background-color: {THEME_COLORS['bg_input']};
        color: {THEME_COLORS['text']};
        border: 2px solid {THEME_COLORS['grigio_lupo']};
        border-radius: 4px;
        padding: 5px;
    }}
    
    QSpinBox:hover, QDoubleSpinBox:hover {{
        border-color: {THEME_COLORS['desert_tan']};
    }}
    
    /* ScrollBar */
    QScrollBar:vertical {{
        background: {THEME_COLORS['bg_input']};
        width: 12px;
        border: 1px solid {THEME_COLORS['grigio_lupo']};
    }}
    
    QScrollBar::handle:vertical {{
        background: {THEME_COLORS['desert_tan']};
        min-height: 20px;
        border-radius: 5px;
    }}
    
    QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {{
        height: 0px;
    }}
    
    /* Frame per video */
    QFrame#videoFrame {{
        background-color: #000000;
        border: 2px solid {THEME_COLORS['grigio_lupo']};
        border-radius: 5px;
    }}
    
    QFrame#videoFrame:hover {{
        border-color: {THEME_COLORS['desert_tan']};
        border-width: 3px;
    }}
    
    /* ToolTip */
    QToolTip {{
        background-color: {THEME_COLORS['bg_input']};
        color: {THEME_COLORS['text']};
        border: 2px solid {THEME_COLORS['desert_tan']};
        padding: 5px;
        border-radius: 3px;
    }}
    
    /* Menu */
    QMenuBar {{
        background-color: {THEME_COLORS['bg_input']};
        color: {THEME_COLORS['text']};
        border-bottom: 2px solid {THEME_COLORS['grigio_lupo']};
    }}
    
    QMenuBar::item {{
        padding: 5px 10px;
    }}
    
    QMenuBar::item:selected {{
        background-color: {THEME_COLORS['verde_ranger']};
        color: {THEME_COLORS['text']};
    }}
    
    QMenu {{
        background-color: {THEME_COLORS['bg_input']};
        color: {THEME_COLORS['text']};
        border: 2px solid {THEME_COLORS['grigio_lupo']};
    }}
    
    QMenu::item {{
        padding: 5px 30px 5px 20px;
    }}
    
    QMenu::item:selected {{
        background-color: {THEME_COLORS['verde_ranger']};
    }}
    
    /* Dialog */
    QDialog {{
        background-color: {THEME_COLORS['nero_tattico']};
        color: {THEME_COLORS['text']};
    }}
    
    /* TextEdit */
    QTextEdit, QPlainTextEdit {{
        background-color: {THEME_COLORS['bg_input']};
        color: {THEME_COLORS['text']};
        border: 2px solid {THEME_COLORS['grigio_lupo']};
        border-radius: 4px;
        padding: 5px;
    }}
    
    /* ProgressBar */
    QProgressBar {{
        background-color: {THEME_COLORS['bg_input']};
        border: 2px solid {THEME_COLORS['grigio_lupo']};
        border-radius: 4px;
        text-align: center;
        color: {THEME_COLORS['text']};
    }}
    
    QProgressBar::chunk {{
        background-color: {THEME_COLORS['verde_ranger']};
        border-radius: 2px;
    }}
    """

def get_video_player_stylesheet():
    """Ritorna lo stile per i player video."""
    return f"""
    QFrame {{
        background-color: #000000;
        border: 2px solid {THEME_COLORS['grigio_lupo']};
        border-radius: 5px;
    }}
    
    QLabel#placeholderLabel {{
        color: {THEME_COLORS['text']};
        font-size: 14px;
        background-color: #0a0a0a;
    }}
    
    QLabel#errorLabel {{
        color: {THEME_COLORS['rosso_squadra']};
        font-size: 12px;
        background-color: #1a0a0a;
    }}
    """

def get_dialog_stylesheet():
    """Ritorna il foglio di stile per i dialog."""
    return f"""
    QDialog {{
        background-color: {THEME_COLORS['nero_tattico']};
        color: {THEME_COLORS['text']};
        font-family: 'Monospace', 'Courier New';
    }}

    QLabel {{
        color: {THEME_COLORS['text']};
        font-weight: bold;
    }}

    QPushButton {{
        background-color: {THEME_COLORS['bg_input']};
        color: {THEME_COLORS['text']};
        border: 2px solid {THEME_COLORS['grigio_lupo']};
        border-radius: 4px;
        padding: 8px 16px;
        font-weight: bold;
        min-width: 80px;
    }}

    QPushButton:hover {{
        background-color: {THEME_COLORS['bg_hover']};
        border-color: {THEME_COLORS['desert_tan']};
    }}

    QPushButton:pressed {{
        background-color: {THEME_COLORS['bg_pressed']};
        border-color: {THEME_COLORS['desert_tan_hover']};
    }}

    QComboBox, QLineEdit, QSpinBox, QDoubleSpinBox {{
        background-color: {THEME_COLORS['bg_input']};
        color: {THEME_COLORS['text']};
        border: 2px solid {THEME_COLORS['grigio_lupo']};
        border-radius: 4px;
        padding: 5px;
    }}

    QComboBox:hover, QLineEdit:hover, QSpinBox:hover, QDoubleSpinBox:hover {{
        border-color: {THEME_COLORS['desert_tan']};
    }}

    QGroupBox {{
.
        color: {THEME_COLORS['desert_tan']};
        border: 2px solid {THEME_COLORS['grigio_lupo']};
        border-radius: 5px;
        margin-top: 10px;
        font-weight: bold;
        padding-top: 10px;
    }}

    QGroupBox::title {{
        subcontrol-origin: margin;
        subcontrol-position: top left;
        padding: 0 5px;
        color: {THEME_COLORS['desert_tan']};
    }}
    """