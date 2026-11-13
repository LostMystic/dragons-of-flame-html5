@echo off
python "%~dp0install.py" %*
if errorlevel 1 python3 "%~dp0install.py" %*
pause
