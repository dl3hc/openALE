@echo off
setlocal

echo.
echo ========================================
echo  CMake Build Script
echo ========================================
echo.

echo [1/4] Erstelle Build-Ordner...
if not exist build (
    mkdir build
    echo     Build-Ordner erstellt.
) else (
    echo     Build-Ordner existiert bereits.
)

echo.
echo [2/4] Wechsle in Build-Ordner...
cd build || (
    echo FEHLER: Konnte nicht in den Build-Ordner wechseln.
    exit /b 1
)

echo.
echo [3/4] Fuehre CMake-Konfiguration aus...
cmake ..
if errorlevel 1 (
    echo.
    echo FEHLER: CMake-Konfiguration fehlgeschlagen.
    exit /b %errorlevel%
)

echo.
echo [4/4] Starte Build...
cmake --build .
if errorlevel 1 (
    echo.
    echo FEHLER: Build fehlgeschlagen.
    exit /b %errorlevel%
)

echo.
echo ========================================
echo  Build erfolgreich abgeschlossen.
echo ========================================
echo.

endlocal