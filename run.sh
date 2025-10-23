#!/bin/bash
# Script di avvio per SyncView
# Attiva automaticamente l'ambiente virtuale

cd "$(dirname "$0")"

# Verifica esistenza ambiente virtuale
if [ ! -d ".venv" ]; then
    echo "⚠ Ambiente virtuale non trovato!"
    echo "Esegui: python3 -m venv .venv"
    exit 1
fi

# Attiva ambiente virtuale
source .venv/bin/activate

# Avvia l'applicazione
python main.py

# Disattiva ambiente virtuale
deactivate
