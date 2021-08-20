@echo OFF
setlocal enabledelayedexpansion
set XGPU_PATH=%cd%

rem --------------------------------------------------------------------------------------------------------
rem Set the color of the terminal to blue with yellow text
rem --------------------------------------------------------------------------------------------------------
COLOR 8E
powershell write-host -fore White ------------------------------------------------------------------------------------------------------
powershell write-host -fore Cyan Welcome I am your XGPU dependency updater bot, let me get to work...
powershell write-host -fore White ------------------------------------------------------------------------------------------------------
echo.

:DOWNLOAD_DEPENDENCIES
powershell write-host -fore White ------------------------------------------------------------------------------------------------------
powershell write-host -fore White XGPU - DOWNLOADING DEPENDENCIES
powershell write-host -fore White ------------------------------------------------------------------------------------------------------

echo.

rem rmdir "../dependencies/xcore" /S /Q
rem git clone https://gitlab.com/LIONant/xcore.git "../dependencies/xcore"
rem if %ERRORLEVEL% GEQ 1 goto :PAUSE
rem cd ../dependencies/xcore/builds
rem call UpdateDependencies.bat "return"
rem cd /d %MECS_PATH%

rem ------------------------------------------------------------
rem Download and install ShaderC
rem ------------------------------------------------------------

echo.
rmdir "../dependencies/Shaderc" /S /Q
cd "../dependencies"

rem Download the shaderc binaries if we don't have it downloaded already
if NOT exist "./Shaderc.zip" (
   bitsadmin /transfer DownloadShaderc /download /priority high https://storage.googleapis.com/shaderc/artifacts/prod/graphics_shader_compiler/shaderc/windows/continuous_release_2017/371/20210722-133829/install.zip "%cd%\Shaderc.zip"
   if %ERRORLEVEL% GEQ 1 goto :PAUSE
)

rem  unzip the installation file
powershell write-host -fore Cyan ShaderC: Unzipping...
powershell -nologo -noprofile -command "& { Add-Type -A 'System.IO.Compression.FileSystem'; [IO.Compression.ZipFile]::ExtractToDirectory('Shaderc.zip', '../dependencies'); }"
if %ERRORLEVEL% GEQ 1 goto :PAUSE

rem Rename the "INSTALL" which is the default name of ShaderC unziped folder to the proper one
rename install shaderc
if %ERRORLEVEL% GEQ 1 goto :PAUSE


:DONE
powershell write-host -fore White ------------------------------------------------------------------------------------------------------
powershell write-host -fore White XGPU - DONE!!
powershell write-host -fore White ------------------------------------------------------------------------------------------------------

:PAUSE
rem if no one give us any parameters then we will pause it at the end, else we are assuming that another batch file called us
if %1.==. pause

