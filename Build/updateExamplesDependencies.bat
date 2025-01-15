echo OFF
setlocal enabledelayedexpansion
set XGPU_PATH="%cd%"

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
rem XTEXTURE PLUGIN
rem ------------------------------------------------------------
:XGEOM_COMPILER
rmdir "../dependencies/xtexture.plugin" /S /Q
git clone https://github.com/LIONant-depot/xtexture.plugin "../dependencies/xtexture.plugin"
if %ERRORLEVEL% GEQ 1 goto :ERROR
cd ../dependencies/xtexture.plugin/build
if %ERRORLEVEL% GEQ 1 goto :ERROR
call updateDependencies.bat "return"
if %ERRORLEVEL% GEQ 1 goto :ERROR

rem now let us build
call build_versions.bat "return"
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
rem Download and install ShaderC
rem ------------------------------------------------------------
:SHADERC
echo.
rmdir "../dependencies/Shaderc" /S /Q
cd "../dependencies"
if %ERRORLEVEL% GEQ 1 goto :ERROR

rem Download the shaderc binaries if we don't have it downloaded already
if NOT exist "./Shaderc.zip" (
   rem bitsadmin /transfer DownloadShaderc /download /priority high https://storage.googleapis.com/shaderc/artifacts/prod/graphics_shader_compiler/shaderc/windows/continuous_release_2017/371/20210722-133829/install.zip "%cd%\Shaderc.zip"

   powershell -Command "Invoke-WebRequest" -Uri "https://storage.googleapis.com/shaderc/artifacts/prod/graphics_shader_compiler/shaderc/windows/continuous_release_2017/371/20210722-133829/install.zip" -OutFile "%cd%\Shaderc.zip"

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
copy "..\dependencies\xgeom_compiler\dependencies\xraw3D\dependencies\assimp\BINARIES\Win32\bin\Release\assimp-vc143-mt.dll" "xGPUExamples.vs2022\assimp-vc143-mt.dll" /Y
if %ERRORLEVEL% GEQ 1 goto :ERROR

rem ------------------------------------------------------------
rem Decompress the asset folder
rem ------------------------------------------------------------
:UNPACKING
cd /d %XGPU_PATH%\..\Assets\Animated
rmdir "../../dependencies/Assets" /S /Q
powershell -nologo -noprofile -command "& { Add-Type -A 'System.IO.Compression.FileSystem'; [IO.Compression.ZipFile]::ExtractToDirectory('catwalk.zip', '../../dependencies/Assets/Animated'); }"
if %ERRORLEVEL% GEQ 1 goto :ERROR
powershell -nologo -noprofile -command "& { Add-Type -A 'System.IO.Compression.FileSystem'; [IO.Compression.ZipFile]::ExtractToDirectory('ImperialWalker.zip', '../../dependencies/Assets/Animated'); }"
if %ERRORLEVEL% GEQ 1 goto :ERROR
powershell -nologo -noprofile -command "& { Add-Type -A 'System.IO.Compression.FileSystem'; [IO.Compression.ZipFile]::ExtractToDirectory('Sonic.zip', '../../dependencies/Assets/Animated'); }"
if %ERRORLEVEL% GEQ 1 goto :ERROR
powershell -nologo -noprofile -command "& { Add-Type -A 'System.IO.Compression.FileSystem'; [IO.Compression.ZipFile]::ExtractToDirectory('Starwars.zip', '../../dependencies/Assets/Animated'); }"
if %ERRORLEVEL% GEQ 1 goto :ERROR
powershell -nologo -noprofile -command "& { Add-Type -A 'System.IO.Compression.FileSystem'; [IO.Compression.ZipFile]::ExtractToDirectory('supersoldier.zip', '../../dependencies/Assets/Animated'); }"
if %ERRORLEVEL% GEQ 1 goto :ERROR
powershell -nologo -noprofile -command "& { Add-Type -A 'System.IO.Compression.FileSystem'; [IO.Compression.ZipFile]::ExtractToDirectory('walking-while-listening.zip', '../../dependencies/Assets/Animated'); }"
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

