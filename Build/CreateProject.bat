@echo off
setlocal enabledelayedexpansion

:: Save current directory
set "ORIG_DIR=%CD%"

:: Check for admin
>nul 2>&1 fsutil dirty query %systemdrive% && goto :gotAdmin

:: Relaunch elevated, passing original dir as arg
powershell -Command "Start-Process '%~f0' -ArgumentList '%ORIG_DIR%' -Verb RunAs"
exit /b

:gotAdmin
:: If relaunched, restore original dir (from arg) if provided
if "%~1" neq "" cd /d "%~1"

:: READY TO WORK!!!!

rmdir /s /q xGPUExamples.vs2022
rem rmdir /s /q ..\dependencies
cmake ../ -G "Visual Studio 17 2022" -A x64 -B xGPUExamples.vs2022
if errorlevel 1 (
    echo Error: Cmake failed
    goto :ERROR
)

rem -- HACK -- We must force the node library to support old ImGui Keys...

:: Define input file
set "inputFile=..\dependencies\imgui-node-editor\imgui_node_editor_internal.h"

:: Check if file exists
if not exist "%inputFile%" (
    echo Error: imgui_node_editor_internal.h not found
    goto :ERROR
)

:: Define search string
set "search=# include \"imgui_node_editor.h\""

:: Create temporary file
set "tempFile=%temp%\tempfile.txt"

:: Use PowerShell for multiline search and replace with explicit newline
powershell -command "$search = [regex]::Escape('%search%'); $replace = '#include \"imgui_node_editor.h\"' + \"`r`n\" + 'namespace ImGui{ template< typename T> T GetKeyIndex(T a) {return a;} }'; (Get-Content '%inputFile%' -Raw) -replace $search, $replace | Set-Content '%tempFile%'"

:: Replace original file with modified content
move /y "%tempFile%" "%inputFile%" >nul
if errorlevel 1 (
    echo Error: Failed to replace the original file
    goto :ERROR
)

echo String replacement complete
endlocal
pause
exit /b 0

:ERROR
endlocal
pause
exit /b 1