#!/bin/bash
# Script di avvio per SyncView
# Setup automatico multi-piattaforma (Linux/Windows/macOS)

cd "$(dirname "$0")"

# Rileva sistema operativo
OS_TYPE="unknown"
case "$(uname -s)" in
    Linux*)     OS_TYPE="linux";;
    Darwin*)    OS_TYPE="macos";;
    CYGWIN*|MINGW*|MSYS*|MINGW32*|MINGW64*)  OS_TYPE="windows";;
    *)          OS_TYPE="unknown";;
esac

# Colori per output (disabilitati su Windows per compatibilità)
if [ "$OS_TYPE" = "windows" ]; then
    RED=''; GREEN=''; YELLOW=''; BLUE=''; NC=''
else
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[1;33m'
    BLUE='\033[0;34m'
    NC='\033[0m'
fi

echo -e "${BLUE}╔═══════════════════════════════════════╗${NC}"
echo -e "${BLUE}║       SyncView - T-MVA System        ║${NC}"
echo -e "${BLUE}╚═══════════════════════════════════════╝${NC}"
echo ""
echo -e "${BLUE}Sistema operativo:${NC} $OS_TYPE"
echo ""

# Determina comando Python in base al sistema
PYTHON_CMD=""
VENV_ACTIVATE=""

if [ "$OS_TYPE" = "windows" ]; then
    # Windows: cerca python o python3 o py
    if command -v python &> /dev/null; then
        PYTHON_CMD="python"
    elif command -v py &> /dev/null; then
        PYTHON_CMD="py"
    elif command -v python3 &> /dev/null; then
        PYTHON_CMD="python3"
    fi
    VENV_ACTIVATE=".venv/Scripts/activate"
else
    # Linux/macOS: usa python3
    if command -v python3 &> /dev/null; then
        PYTHON_CMD="python3"
    fi
    VENV_ACTIVATE=".venv/bin/activate"
fi

# Verifica Python
if [ -z "$PYTHON_CMD" ]; then
    echo -e "${RED}✗ Python non trovato!${NC}"
    echo ""
    case "$OS_TYPE" in
        linux)
            echo "  Installa Python3: sudo apt install python3 python3-venv python3-pip"
            echo "  oppure: sudo dnf install python3 python3-pip (Fedora/RHEL)"
            ;;
        macos)
            echo "  Installa Python3: brew install python3"
            echo "  oppure scarica da: https://www.python.org/downloads/"
            ;;
        windows)
            echo "  Installa Python da: https://www.python.org/downloads/"
            echo "  IMPORTANTE: Durante installazione seleziona 'Add Python to PATH'"
            ;;
        *)
            echo "  Installa Python3 dal sito ufficiale: https://www.python.org/"
            ;;
    esac
    exit 1
fi

# Ottieni versione Python
PYTHON_VERSION=$($PYTHON_CMD --version 2>&1)
echo -e "${GREEN}✓${NC} Python trovato: $PYTHON_VERSION"

# Verifica/crea ambiente virtuale
if [ ! -d ".venv" ]; then
    echo -e "${YELLOW}⚙${NC}  Primo avvio rilevato - creazione ambiente virtuale..."
    $PYTHON_CMD -m venv .venv
    
    if [ $? -ne 0 ]; then
        echo -e "${RED}✗ Errore nella creazione dell'ambiente virtuale!${NC}"
        case "$OS_TYPE" in
            linux)
                echo "  Prova: sudo apt install python3-venv"
                echo "  oppure: sudo dnf install python3-devel (Fedora/RHEL)"
                ;;
            macos)
                echo "  Python dovrebbe includere venv di default"
                echo "  Se il problema persiste, reinstalla Python"
                ;;
            windows)
                echo "  Assicurati di aver installato Python con tutte le opzioni"
                echo "  Riavvia il terminale dopo l'installazione"
                ;;
        esac
        exit 1
    fi
    
    echo -e "${GREEN}✓${NC} Ambiente virtuale creato"
    FIRST_RUN=true
else
    echo -e "${GREEN}✓${NC} Ambiente virtuale trovato"
    FIRST_RUN=false
fi

# Attiva ambiente virtuale
if [ "$OS_TYPE" = "windows" ]; then
    # Windows usa Scripts invece di bin
    source .venv/Scripts/activate 2>/dev/null || . .venv/Scripts/activate
else
    source .venv/bin/activate
fi

# Installazione dipendenze al primo avvio o se requirements.txt è più recente
INSTALL_DEPS=false
if [ "$FIRST_RUN" = true ]; then
    INSTALL_DEPS=true
    echo -e "${YELLOW}⚙${NC}  Installazione dipendenze (può richiedere qualche minuto)..."
elif [ "requirements.txt" -nt ".venv/lib/python3.*/site-packages" ] 2>/dev/null; then
    INSTALL_DEPS=true
    echo -e "${YELLOW}⚙${NC}  Aggiornamento dipendenze rilevato..."
fi

if [ "$INSTALL_DEPS" = true ]; then
    # Aggiorna pip
    echo -e "   ${BLUE}→${NC} Aggiornamento pip..."
    python -m pip install --upgrade pip setuptools wheel -q
    
    # Installa requirements
    echo -e "   ${BLUE}→${NC} Installazione pacchetti da requirements.txt..."
    pip install -r requirements.txt
    
    if [ $? -ne 0 ]; then
        echo -e "${RED}✗ Errore nell'installazione delle dipendenze!${NC}"
        echo "  Verifica requirements.txt e la connessione internet"
        deactivate
        exit 1
    fi
    
    echo -e "${GREEN}✓${NC} Dipendenze installate con successo"
fi

# Verifica FFmpeg (opzionale ma raccomandato)
if ! command -v ffmpeg &> /dev/null; then
    echo -e "${YELLOW}⚠${NC}  FFmpeg non trovato (raccomandato per export video)"
    case "$OS_TYPE" in
        linux)
            echo "   Installa con: sudo apt install ffmpeg"
            echo "   oppure: sudo dnf install ffmpeg (Fedora/RHEL)"
            ;;
        macos)
            echo "   Installa con: brew install ffmpeg"
            ;;
        windows)
            echo "   Scarica da: https://ffmpeg.org/download.html"
            echo "   oppure usa: winget install FFmpeg"
            echo "   oppure usa: choco install ffmpeg"
            ;;
    esac
else
    echo -e "${GREEN}✓${NC} FFmpeg trovato"
fi

# Crea directory necessarie se non esistono
for dir in Feed-1 Feed-2 Feed-3 Feed-4 Salvataggi; do
    if [ ! -d "$dir" ]; then
        mkdir -p "$dir"
        echo -e "${GREEN}✓${NC} Creata directory: $dir"
    fi
done

echo ""
echo -e "${GREEN}═══════════════════════════════════════${NC}"
echo -e "${GREEN}    Avvio SyncView in corso...${NC}"
echo -e "${GREEN}═══════════════════════════════════════${NC}"
echo ""

# Avvia l'applicazione
python main.py

# Disattiva ambiente virtuale
deactivate
