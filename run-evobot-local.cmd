@echo off
setlocal
set "EZQUAKE_EXE=%~dp0build-msbuild-x64\Debug\ezquake.exe"
set "QUAKE_BASEDIR=K:\development\EvoBot\server"

if not exist "%EZQUAKE_EXE%" set "EZQUAKE_EXE=%~dp0build-msbuild-x64-vs2022\Debug\ezquake.exe"

if not "%~1"=="" set "QUAKE_BASEDIR=%~1"

if not exist "%EZQUAKE_EXE%" (
	echo ezQuake Debug executable not found: "%EZQUAKE_EXE%"
	echo Build it with the configured Windows x64 Debug build directory.
	exit /b 1
)

"%EZQUAKE_EXE%" -basedir "%QUAKE_BASEDIR%" -game evosp -progtype 0 +set sv_progsname spprogs +set deathmatch 0 +set coop 1 +set skill 1 +map e1m1
