@echo off
cd /d "%~dp0"

where python.exe >nul 2>nul
if not errorlevel 1 goto use_python

where py.exe >nul 2>nul
if not errorlevel 1 goto use_py

echo Python was not found. Please install Python 3 first.
pause
exit /b 1

:use_python
start "" "http://127.0.0.1:8765/index.html"
python.exe -m http.server 8765 --bind 127.0.0.1 --directory "%~dp0output"
goto server_ended

:use_py
start "" "http://127.0.0.1:8765/index.html"
py.exe -3 -m http.server 8765 --bind 127.0.0.1 --directory "%~dp0output"

:server_ended
echo.
echo Website server stopped or failed to start.
pause
