@echo off
setlocal
cd /d "%~dp0"

set "VCVARS="
for %%R in ("%ProgramFiles%" "%ProgramFiles(x86)%") do (
    for /d %%V in ("%%~R\Microsoft Visual Studio\*") do (
        for /d %%E in ("%%~V\*") do (
            if exist "%%~E\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%~E\VC\Auxiliary\Build\vcvars64.bat"
        )
    )
)

if not defined VCVARS (
    echo Visual Studio with the Desktop development with C++ workload was not found.
    exit /b 1
)

call "%VCVARS%" >nul 2>nul
if errorlevel 1 exit /b 1

if not exist bin mkdir bin
if not exist obj mkdir obj

rc /nologo /fo obj\app.res src\app.rc
if errorlevel 1 exit /b 1

cl /nologo /W3 /O1 /GS /MT /DUNICODE /D_UNICODE src\main.c obj\app.res ^
    /Foobj\main.obj /Febin\AutoMouseMover.exe ^
    /link /SUBSYSTEM:WINDOWS /MANIFEST:NO /OPT:REF /OPT:ICF kernel32.lib user32.lib advapi32.lib
if errorlevel 1 exit /b 1

del /q obj\*.obj obj\*.res 2>nul
rmdir /q obj 2>nul
for %%F in (bin\AutoMouseMover.exe) do echo Built %%F (%%~zF bytes)
