@echo off
echo Building, uploading, and monitoring ESP32-C3 Mochi...
C:\Users\aragasa\.platformio\penv\Scripts\pio.exe run -t upload -t monitor
pause
