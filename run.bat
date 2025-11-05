@echo off
REM Script di avvio per SyncView (Windows)
REM Setup automatico al primo avvio

cd /d "%~dp0"

echo.
echo ========================================
echo       SyncView - T-MVA System
echo ========================================
echo.
echo Sistema operativo: Windows
echo.

REM Verifica Python
where python >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    set PYTHON_CMD=python
    goto :python_found
)

where py >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    set PYTHON_CMD=py
    goto :python_found
)

where python3 >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    set PYTHON_CMD=python3
    goto :python_found
)

echo [X] Python non trovato!
echo.
echo   Installa Python da: https://www.python.org/downloads/
echo   IMPORTANTE: Durante installazione seleziona 'Add Python to PATH'
echo.
pause
exit /b 1

:python_found
for /f "tokens=*" %%i in ('%PYTHON_CMD% --version 2^>^&1') do set PYTHON_VERSION=%%i
echo [*] Python trovato: %PYTHON_VERSION%

REM Verifica/crea ambiente virtuale
if not exist ".venv" (
    echo [*] Primo avvio rilevato - creazione ambiente virtuale...
    %PYTHON_CMD% -m venv .venv
    
    if %ERRORLEVEL% NEQ 0 (
        echo [X] Errore nella creazione dell'ambiente virtuale!
        echo   Assicurati di aver installato Python con tutte le opzioni
        echo   Riavvia il terminale dopo l'installazione
        pause
        exit /b 1
    )
    
    echo [*] Ambiente virtuale creato
    set FIRST_RUN=true
) else (
    echo [*] Ambiente virtuale trovato
    set FIRST_RUN=false
)

REM Attiva ambiente virtuale
call .venv\Scripts\activate.bat

REM Installazione dipendenze al primo avvio
if "%FIRST_RUN%"=="true" (
    echo [*] Installazione dipendenze ^(puo richiedere qualche minuto^)...
    echo    - Aggiornamento pip...
    python -m pip install --upgrade pip setuptools wheel -q
    
    echo    - Installazione pacchetti da requirements.txt...
    pip install -r requirements.txt
    
    if %ERRORLEVEL% NEQ 0 (
        echo [X] Errore nell'installazione delle dipendenze!
        echo   Verifica requirements.txt e la connessione internet
        pause
        exit /b 1
    )
    
    echo [*] Dipendenze installate con successo
)

REM Verifica FFmpeg
where ffmpeg >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo [!] FFmpeg non trovato ^(raccomandato per export video^)
    echo    Scarica da: https://ffmpeg.org/download.html
    echo    oppure usa: winget install FFmpeg
    echo    oppure usa: choco install ffmpeg
) else (
    echo [*] FFmpeg trovato
)

echo.
echo ========================================
echo     Avvio SyncView in corso...
echo ========================================
echo.

REM Avvia l'applicazione
python main.py

REM Disattiva ambiente virtuale
call .venv\Scripts\deactivate.bat

pause
