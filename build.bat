@echo off
setlocal enabledelayedexpansion

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "!VSWHERE!" (
    echo [ERROR] vswhere.exe not found. Is Visual Studio installed?
    exit /b 1
)

set "VSPATH="
"!VSWHERE!" -latest -property installationPath > "%TEMP%\tg2c_vspath.txt" 2>nul
set /p VSPATH=<"%TEMP%\tg2c_vspath.txt"
del "%TEMP%\tg2c_vspath.txt" >nul 2>&1
if not defined VSPATH (
    echo [ERROR] Could not locate a Visual Studio installation.
    exit /b 1
)

call "!VSPATH!\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul
if errorlevel 1 (
    echo [ERROR] vcvarsall.bat x86 failed.
    exit /b 1
)

if not exist "%~dp0build" mkdir "%~dp0build"

cl /nologo /LD /O2 /MT /EHsc /W3 /DWIN32 /D_WINDOWS /DNDEBUG ^
   "%~dp0src\dllmain.cpp" ^
   /Fe:"%~dp0build\dinput8.dll" /Fo:"%~dp0build\\" ^
   /link /DEF:"%~dp0src\dinput8.def" user32.lib kernel32.lib
if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)

echo.
echo [OK] Built build\dinput8.dll
exit /b 0
