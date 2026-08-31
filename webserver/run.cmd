@echo off
REM Launch the Kokoro TTS local web app.
REM Activates the local venv (created during setup) and starts the server
REM bound to 0.0.0.0:8000 so the Chikadora ESP32-C3 can reach it over the LAN.
REM Open http://127.0.0.1:8000 in your browser.

setlocal
cd /d "%~dp0"

if not exist "venv\Scripts\python.exe" (
  echo [!] venv not found. Run the setup steps in README.md first.
  exit /b 1
)

set TTS_HOST=0.0.0.0
set TTS_PORT=8000

REM Speech-to-text engine: "moonshine" (fast, short English) or "faster-whisper".
set STT_ENGINE=moonshine

"venv\Scripts\python.exe" backend\server.py
endlocal
