echo OFF
setlocal enabledelayedexpansion
set XGPU_PATH=%cd%

rem --------------------------------------------------------------------------------------------------------
rem Set the color of the terminal to blue with yellow text
rem --------------------------------------------------------------------------------------------------------
COLOR 8E
powershell write-host -fore White ------------------------------------------------------------------------------------------------------
powershell write-host -fore Cyan Welcome I am your XGPU EXAMPLES dependency updater bot, let me get to work...
powershell write-host -fore White ------------------------------------------------------------------------------------------------------
echo.

:DOWNLOAD_DEPENDENCIES
powershell write-host -fore White ------------------------------------------------------------------------------------------------------
powershell write-host -fore White XGPU EXAMPLES - DOWNLOADING DEPENDENCIES
powershell write-host -fore White ------------------------------------------------------------------------------------------------------
echo.

rem ------------------------------------------------------------
rem XGEOM_COMPILER
rem ------------------------------------------------------------
:XGEOM_COMPILER
rmdir "../dependencies/xgeom_compiler" /S /Q
git clone https://github.com/LIONant-depot/xgeom_compiler "../dependencies/xgeom_compiler"
if %ERRORLEVEL% GEQ 1 goto :ERROR
cd ../dependencies/xgeom_compiler/build
if %ERRORLEVEL% GEQ 1 goto :ERROR
call updateDependencies.bat "return"
if %ERRORLEVEL% GEQ 1 goto :ERROR
cd /d %XGPU_PATH%
if %ERRORLEVEL% GEQ 1 goto :ERROR

rem ------------------------------------------------------------
rem XPRIM_GEOM
rem ------------------------------------------------------------
:XPRIM_GEOM
rmdir "../dependencies/xprim_geom" /S /Q
git clone https://github.com/LIONant-depot/xprim_geom.git "../dependencies/xprim_geom"
if %ERRORLEVEL% GEQ 1 goto :ERROR

rem ------------------------------------------------------------
rem XBMP_TOOLS
rem ------------------------------------------------------------
:XBMP_TOOLS
rmdir "../dependencies/xbmp_tools" /S /Q
git clone https://github.com/LIONant-depot/xbmp_tools.git "../dependencies/xbmp_tools"
if %ERRORLEVEL% GEQ 1 goto :ERROR
cd ../dependencies/xbmp_tools/build
if %ERRORLEVEL% GEQ 1 goto :ERROR
call UpdateDependencies.bat "return"
if %ERRORLEVEL% GEQ 1 goto :ERROR
cd /d %XGPU_PATH%
if %ERRORLEVEL% GEQ 1 goto :ERROR

rem ------------------------------------------------------------
rem Basis Universal
rem ------------------------------------------------------------
:BASIS_UNIVERSAL
rmdir "../dependencies/basis_universal" /S /Q
git clone --recurse-submodules -j8 https://github.com/BinomialLLC/basis_universal.git "../dependencies/basis_universal"

rem ------------------------------------------------------------
rem Download and install ShaderC
rem ------------------------------------------------------------
:SHADERC
echo.
rmdir "../dependencies/Shaderc" /S /Q
cd "../dependencies"
if %ERRORLEVEL% GEQ 1 goto :ERROR

rem Download the shaderc binaries if we don't have it downloaded already
if NOT exist "./Shaderc.zip" (
   bitsadmin /transfer DownloadShaderc /download /priority high https://storage.googleapis.com/shaderc/artifacts/prod/graphics_shader_compiler/shaderc/windows/continuous_release_2017/371/20210722-133829/install.zip "%cd%\Shaderc.zip"
   if %ERRORLEVEL% GEQ 1 goto :ERROR
)

rem  unzip the installation file
powershell write-host -fore Cyan ShaderC: Unzipping...
powershell -nologo -noprofile -command "& { Add-Type -A 'System.IO.Compression.FileSystem'; [IO.Compression.ZipFile]::ExtractToDirectory('Shaderc.zip', '../dependencies'); }"
if %ERRORLEVEL% GEQ 1 goto :ERROR

rem Rename the "INSTALL" which is the default name of ShaderC unziped folder to the proper one
rename install shaderc
if %ERRORLEVEL% GEQ 1 goto :ERROR

cd /d %XGPU_PATH%
if %ERRORLEVEL% GEQ 1 goto :ERROR

rem ------------------------------------------------------------
rem Copy ASSIMP DLL
rem ------------------------------------------------------------
:ASSIMP
rem copy the dlls just in case the user runs the examples 
copy "..\dependencies\xgeom_compiler\dependencies\xraw3D\dependencies\assimp\BINARIES\Win32\bin\Release\assimp-vc142-mt.dll" "xGPUExamples.vs2019\assimp-vc142-mt.dll" /Y
if %ERRORLEVEL% GEQ 1 goto :ERROR


:DONE
powershell write-host -fore White ------------------------------------------------------------------------------------------------------
powershell write-host -fore White XGPU EXAMPLES - DONE!!
powershell write-host -fore White ------------------------------------------------------------------------------------------------------
goto :PAUSE

:ERROR
powershell write-host -fore Red ------------------------------------------------------------------------------------------------------
powershell write-host -fore Red XGPU EXAMPLES - ERROR!!
powershell write-host -fore Red ------------------------------------------------------------------------------------------------------

:PAUSE
rem if no one give us any parameters then we will pause it at the end, else we are assuming that another batch file called us
if %1.==. pause

