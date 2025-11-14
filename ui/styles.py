"""
Stili CSS per l'interfaccia tattica/militare di SyncView.
"""

from config.settings import THEME_COLORS

def get_main_stylesheet():
    """Ritorna il foglio di stile principale dell'applicazione."""
    return f"""
    /* ================================================================== */
    /* ==                  SYNCVIEW THEME - NIGHT OPS                  == */
    /* ================================================================== */

    /* --- Base & Finestra Principale --- */
    QMainWindow, QDialog {{ /* Aggiunto gradiente per profondità */
        background: qradialgradient(cx: 0.5, cy: 0.5, radius: 1.2, fx: 0.5, fy: 0.5, stop: 0 #222224, stop: 1 {THEME_COLORS['bg_base']});
        color: {THEME_COLORS['text_primary']};
        font-family: 'Monospace', 'Courier New', 'Lucida Console', monospace;
        font-size: 11px;
    }}
    
    QWidget#centralWidget {{ /* Riferimento: Widget Centrale Principale */
        border: 1px solid {THEME_COLORS['border_primary']};
        border-radius: 5px;
    }}

    /* --- Title Bar --- */
    QWidget#customTitleBar {{ /* Riferimento: Barra del Titolo Personalizzata */
        background-color: {THEME_COLORS['bg_surface']};
        border-bottom: 1px solid {THEME_COLORS['border_primary']};
    }}
    QLabel#titleLabel {{ /* Riferimento: Etichetta Titolo App */
        color: {THEME_COLORS['text_secondary']};
        font-size: 12px;
        padding-left: 10px;
    }}
    QPushButton#minimizeButton, QPushButton#maximizeButton, QPushButton#closeButton, QPushButton#helpButtonTitle {{ /* Riferimento: Pulsante Minimizza, Massimizza, Chiudi, Aiuto (Titolo) */
        background-color: transparent;
        border: none;
        font-size: 16px;
    }}
    QPushButton#closeButton:hover {{ /* Riferimento: Pulsante Chiudi */
        background-color: {THEME_COLORS['accent_negative']};
    }}
    QPushButton#minimizeButton:hover, QPushButton#maximizeButton:hover, QPushButton#helpButtonTitle:hover {{ /* Riferimento: Pulsante Minimizza, Massimizza, Aiuto (Titolo) */
        background-color: {THEME_COLORS['bg_surface_hover']};
    }}

    /* --- Pulsanti Generici --- */
    QPushButton {{ /* Riferimento: Pulsanti generici (es. Vai a Inizio, Vai a Fine, Audio Master, Risincronizza, Fit Video, etc.) */
        background-color: {THEME_COLORS['bg_surface']};
        color: {THEME_COLORS['text_primary']};
        border: 1px solid {THEME_COLORS['border_primary']};
        border-radius: 4px;
        padding: 6px 12px;
        font-weight: 600; /* Semi-bold */
        font-size: 10px;
        text-transform: uppercase;
    }}
    QPushButton:hover {{ /* Riferimento: Pulsanti generici */
        background-color: {THEME_COLORS['bg_surface_hover']};
        border-color: {THEME_COLORS['border_accent']};
    }}
    QPushButton:pressed {{ /* Aggiunto per feedback tattile */
        background-color: {THEME_COLORS['accent_positive']};
        border-color: {THEME_COLORS['accent_positive_hover']};
        color: #ffffff;
    }}
    QPushButton:disabled {{ /* Riferimento: Pulsanti generici */
        background-color: {THEME_COLORS['bg_base']};
        color: {THEME_COLORS['text_disabled']};
        border-color: {THEME_COLORS['border_primary']};
    }}

    /* --- Pulsanti con Accento --- */
    QPushButton#playButton, QPushButton#pauseButton,  /* Riferimento: Pulsante Play/Pausa Globale */
    QPushButton#compactPlayButton, QPushButton#compactPauseButton,
    QPushButton#loadVideoButton, QPushButton#refreshVideoButton, /* Riferimento: Pulsante Carica/Aggiorna Video Feed X */
    QPushButton#okButton {{ /* Riferimento: Pulsante Avvia Export (Dialog) */
        background-color: {THEME_COLORS['accent_positive']};
        border-color: {THEME_COLORS['accent_positive_hover']};
        font-weight: bold;
        color: #ffffff;
    }}
    QPushButton#playButton:hover, QPushButton#pauseButton:hover, /* Riferimento: Pulsante Play/Pausa Globale */
    QPushButton#compactPlayButton:hover, QPushButton#compactPauseButton:hover,
    QPushButton#loadVideoButton:hover, QPushButton#refreshVideoButton:hover, /* Riferimento: Pulsante Carica/Aggiorna Video Feed X */
    QPushButton#okButton:hover {{ /* Riferimento: Pulsante Avvia Export (Dialog) */
        background-color: {THEME_COLORS['accent_positive_hover']};
    }}

    QPushButton#removeVideoButton, /* Riferimento: Pulsante Rimuovi Video Feed X */
    QPushButton#deleteButton,      /* Riferimento: Pulsante Elimina/Cancella Tutti Marker (Dialog) */
    QPushButton#cancelButton {{     /* Riferimento: Pulsante Annulla (Dialog) */
        background-color: {THEME_COLORS['accent_negative']};
        border-color: {THEME_COLORS['accent_negative_hover']};
        font-weight: bold;
        color: #ffffff;
    }}
    QPushButton#removeVideoButton:hover, QPushButton#deleteButton:hover, QPushButton#cancelButton:hover {{ /* Riferimento: Pulsanti negativi */
        background-color: {THEME_COLORS['accent_negative_hover']};
    }}
    
    /* Stile per pulsanti compatti (solo icona) in VideoPlayerWidget */
    QPushButton[compactMode="true"] {{
        font-size: 14px; /* Icona più grande */
        padding: 5px 8px;
        min-width: 35px;
    }}
    QPushButton#loadVideoButton[compactMode="true"] {{
        font-size: 16px;
    }}

    /* --- Input Fields (ComboBox, SpinBox, etc.) --- */
    QComboBox, QSpinBox, QDoubleSpinBox, QLineEdit {{ /* Riferimento: Selettore FPS, Selettore Step Frame, Filtro Categoria, etc. */
        background-color: {THEME_COLORS['bg_surface']};
        color: {THEME_COLORS['text_primary']};
        border: 1px solid {THEME_COLORS['border_primary']};
        border-radius: 4px;
        padding: 5px;
    }}
    QComboBox:hover, QSpinBox:hover, QDoubleSpinBox:hover, QLineEdit:hover {{ /* Riferimento: Input fields */
        border-color: {THEME_COLORS['border_accent']};
    }}
    QComboBox::drop-down {{ /* Riferimento: Dropdown dei ComboBox */
        border: none;
        width: 20px;
    }}
    QComboBox::down-arrow {{ /* Riferimento: Dropdown dei ComboBox */
        border-left: 5px solid transparent;
        border-right: 5px solid transparent;
        border-top: 5px solid {THEME_COLORS['text_secondary']};
        width: 0;
        margin-right: 5px;
    }}
    QComboBox QAbstractItemView {{ /* Riferimento: Dropdown dei ComboBox */
        background-color: {THEME_COLORS['bg_surface']};
        border: 1px solid {THEME_COLORS['border_accent']};
        selection-background-color: {THEME_COLORS['accent_positive']};
        outline: 0px;
        padding: 4px;
    }}
    QComboBox QAbstractItemView::item {{
        min-height: 24px;
        border-radius: 2px;
    }}

    /* --- Etichette (Labels) --- */
    QLabel {{ /* Riferimento: Etichette generiche */
        background-color: transparent;
        color: {THEME_COLORS['text_primary']};
        font-weight: normal;
    }}
    QLabel#headerLabel {{ /* Riferimento: Titolo Feed X */
        color: {THEME_COLORS['accent_primary']};
        font-size: 18px;
        font-weight: bold;
    }}
    QLabel#groupBoxTitle, QLabel#dialogTitle, QLabel#frameModeActiveLabel {{ /* Riferimento: Titoli di sezioni e dialog */
        font-weight: bold;
        color: {THEME_COLORS['accent_primary']};
    }}
    QLabel#frameModeActiveLabel {{
        font-size: 12px;
        padding: 5px;
        border-radius: 3px;
        background-color: {THEME_COLORS['accent_primary_hover']};
        color: #ffffff;
    }}
    QLabel#exportInfoLabel, QLabel#qualityInfoLabel {{ /* Riferimento: Etichetta Info Export (Dialog) */
        background-color: {THEME_COLORS['bg_surface']};
        color: {THEME_COLORS['text_secondary']};
        border-radius: 3px;
        padding: 8px;
    }}
    QLabel#statusIndicator[status="ok"] {{ color: {THEME_COLORS['accent_positive']}; }} /* Riferimento: Indicatore di Stato Sistema */
    QLabel#statusIndicator[status="warning"] {{ color: {THEME_COLORS['accent_primary']}; }} /* Riferimento: Indicatore di Stato Sistema */
    QLabel#statusIndicator[status="error"] {{ color: {THEME_COLORS['accent_negative']}; }} /* Riferimento: Indicatore di Stato Sistema */
    QLabel#statusIndicator[status="loading"], /* Riferimento: Indicatore di Stato Sistema */
    QLabel#statusIndicator[status="exporting"],
    QLabel#statusIndicator[status="ready"] {{ color: {THEME_COLORS['accent_info']}; }} /* Riferimento: Indicatore di Stato Sistema */

    /* --- Slider & Timeline --- */
    QSlider::groove:horizontal {{ /* Riferimento: Slider per Export */
        background: {THEME_COLORS['bg_surface']};
        height: 8px;
        border-radius: 4px;
        border: 1px solid {THEME_COLORS['border_primary']};
    }}
    QSlider::handle:horizontal {{ /* Riferimento: Slider per Export */
        background: {THEME_COLORS['accent_primary']};
        width: 16px;
        height: 16px;
        margin: -5px 0;
        border-radius: 8px;
        border: 1px solid {THEME_COLORS['accent_primary_hover']};
    }}
    QSlider::handle:horizontal:hover {{ /* Riferimento: Slider per Export */
        background: {THEME_COLORS['accent_primary_hover']};
        border: 1px solid {THEME_COLORS['accent_primary']};
    }}
    QSlider::sub-page:horizontal {{ /* Riferimento: Timeline Globale e Individuale */
        background: {THEME_COLORS['accent_positive']};
        border-radius: 4px;
    }}
    
    /* --- Contenitori (GroupBox, Frame) --- */
    QGroupBox {{ /* Stile più pulito per i contenitori */
        color: {THEME_COLORS['accent_primary']};
        border: 1px solid transparent;
        border-top: 2px solid {THEME_COLORS['border_primary']};
        border-radius: 5px;
        margin-top: 10px;
        font-weight: bold;
        padding-top: 10px;
        font-size: 10px;
        text-transform: uppercase;
    }}
    QGroupBox::title {{ /* Riferimento: Titoli dei GroupBox */
        subcontrol-origin: margin;
        subcontrol-position: top left;
        padding: 10px 5px 0 5px; /* Sposta il titolo sopra il bordo */
        color: {THEME_COLORS['accent_primary']};
    }}
    QFrame#videoFrame {{ /* Riferimento: Contenitore Video Feed X */
        background-color: #000000;
        border: 1px solid {THEME_COLORS['border_primary']};
        border-radius: 5px;
        box-shadow: 0 0 10px rgba(0, 0, 0, 0.5);
    }}
    QFrame#videoFrame:hover {{ /* Aggiunto box-shadow per un effetto "glow" */
        border-color: {THEME_COLORS['border_accent']};
        box-shadow: 0 0 15px {THEME_COLORS['border_accent']};
    }}

    /* --- Altri Controlli --- */
    QCheckBox {{ /* Riferimento: Checkbox Sincronizzazione, Checkbox Modalità Frame */
        color: {THEME_COLORS['text_primary']};
        spacing: 8px;
    }}
    QCheckBox::indicator {{ /* Riferimento: Indicatore dei CheckBox */
        width: 18px;
        height: 18px;
        border: 1px solid {THEME_COLORS['border_primary']};
        border-radius: 3px;
        background-color: {THEME_COLORS['bg_surface']};
    }}
    QCheckBox::indicator:checked {{ /* Riferimento: Indicatore dei CheckBox */
        background-color: {THEME_COLORS['accent_positive']};
        border-color: {THEME_COLORS['accent_positive_hover']};
    }}
    QCheckBox::indicator:hover {{ /* Riferimento: Indicatore dei CheckBox */
        border-color: {THEME_COLORS['border_accent']};
    }}

    QScrollBar:vertical {{ /* Riferimento: Barre di scorrimento */
        background: {THEME_COLORS['bg_surface']};
        width: 12px;
        border: 1px solid {THEME_COLORS['border_primary']};
    }}
    QScrollBar::handle:vertical {{ /* Riferimento: Barre di scorrimento */
        background: {THEME_COLORS['accent_primary']};
        min-height: 20px;
        border-radius: 5px;
    }}
    QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {{ /* Riferimento: Barre di scorrimento */
        height: 0px;
    }}

    QToolTip {{ /* Riferimento: Tooltip generici */
        background-color: {THEME_COLORS['bg_surface']};
        color: {THEME_COLORS['text_primary']};
        border: 1px solid {THEME_COLORS['border_accent']};
        padding: 5px;
        border-radius: 3px;
    }}

    QProgressBar {{ /* Riferimento: Barre di progresso */
        background-color: {THEME_COLORS['bg_surface']};
        border: 1px solid {THEME_COLORS['border_primary']};
        border-radius: 4px;
        text-align: center;
        color: {THEME_COLORS['text_primary']};
    }}
    QProgressBar::chunk {{ /* Riferimento: Barre di progresso */
        background-color: {THEME_COLORS['accent_positive']};
        border-radius: 2px;
    }}

    /* --- Stili Specifici Video Player --- */
    QWidget#headerContainer {{ /* Riferimento: Header Feed X */
        min-height: 40px;
        max-height: 40px;
        display: flex;
        align-items: space-between;
    }}

    QLabel#fpsLabel {{ /* Riferimento: Etichetta FPS Feed X */
        color: {THEME_COLORS['accent_primary']};
        font-size: 12px;
        font-weight: normal;
    }}

    QLabel#zoomTitleLabel {{ /* Riferimento: Titolo Etichetta Zoom Feed X */
        color: {THEME_COLORS['text_secondary']};
        font-size: 9px;
        font-weight: normal;
    }}

    QLabel#zoomValueLabel {{ /* Riferimento: Valore Etichetta Zoom Feed X */
        color: {THEME_COLORS['accent_positive']};
        font-size: 12px;
        font-weight: bold;
    }}
    QLabel#zoomValueLabel[zoomed="true"] {{
        color: {THEME_COLORS['accent_primary']};
    }}

    QLabel#statusLabel {{ /* Riferimento: Etichetta Stato Feed X */
        font-size: 18px;      /* Come #headerLabel */
        font-weight: bold;    /* Come #headerLabel */
        color: {THEME_COLORS['accent_primary']}; /* Colore base come #headerLabel */
    }}
    QLabel#statusLabel[status="ready"] {{ color: {THEME_COLORS['accent_positive']}; }}
    QLabel#statusLabel[status="loading"] {{ color: {THEME_COLORS['accent_primary']}; }}
    QLabel#statusLabel[status="error"] {{ color: {THEME_COLORS['accent_negative']}; }}
    QLabel#statusLabel[status="empty"] {{ color: {THEME_COLORS['text_secondary']}; }}

    QLabel#placeholderLabel {{ /* Riferimento: Placeholder Feed X */
        color: {THEME_COLORS['text_disabled']};
        font-size: 14px;
        background-color: transparent;
        border: 2px dashed {THEME_COLORS['border_primary']};
        border-radius: 4px;
    }}
    QLabel#errorLabel {{ /* Riferimento: Placeholder Feed X (stato errore) */
        color: {THEME_COLORS['accent_negative']};
        font-size: 12px;
        background-color: transparent;
        border: 2px dashed {THEME_COLORS['accent_negative']};
        border-radius: 4px;
    }}
    """