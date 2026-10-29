@echo off
setlocal
cd /d "%~dp0"

rem Use UTF-8 for the Web server console and pass optional arguments through.
chcp 65001 >nul
set "PYTHONUTF8=1"
set "PYTHONIOENCODING=utf-8"

if exist "%~dp0.venv\Scripts\python.exe" (
    "%~dp0.venv\Scripts\python.exe" "%~dp0server.py" %*
) else (
    where py >nul 2>&1
    if not errorlevel 1 (
        py -3 "%~dp0server.py" %*
    ) else (
        where python >nul 2>&1
        if errorlevel 1 (
            echo [ERROR] Python 3 was not found. Install Python 3 and retry.
            pause
            exit /b 9009
        )
        python "%~dp0server.py" %*
    )
)

set "EXIT_CODE=%ERRORLEVEL%"
if not "%EXIT_CODE%"=="0" (
    echo [ERROR] Server stopped with code %EXIT_CODE%.
    pause
)
exit /b %EXIT_CODE%
