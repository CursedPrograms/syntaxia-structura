@echo off
setlocal

set FLAGS=-O2 -static-libgcc -static-libstdc++ -mwindows
set LIBS=-lcomctl32 -lcomdlg32 -lgdi32 -luser32 -luuid -lole32 -lshell32

echo ========================================
echo  Structura Build
echo ========================================

echo [1/2] StructArch.exe  (page layout editor)
g++ -std=c++14 %FLAGS% -o StructArch.exe maker\structarch.cpp %LIBS%
if %errorlevel%==0 (echo       OK) else (echo       FAILED & goto :end)

echo [2/2] SynReader.exe   (visual document reader)
g++ -std=c++14 %FLAGS% -o SynReader.exe renderer\synreader.cpp %LIBS%
if %errorlevel%==0 (echo       OK) else (echo       FAILED & goto :end)

echo.
echo Both EXEs in project root.
echo   StructArch.exe — layout editor, saves .syn
echo   SynReader.exe  — page viewer, drag a .syn onto it

:end
pause
