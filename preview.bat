@echo off
cd /d "%~dp0"
python preview.py || py preview.py
pause
