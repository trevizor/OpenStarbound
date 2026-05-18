@echo off
setlocal

REM Run from this script's directory so preset/build paths resolve consistently.
pushd "%~dp0"

echo [1/3] Incremental build (windows-release-VS2022)...
cmake --build --preset windows-release-VS2022
if errorlevel 1 (
  echo Build failed. Aborting release scripts.
  popd
  exit /b 1
)

REM Release scripts expect to run from repository root.
pushd ..

echo [2/3] Running post_build.bat...
cmd /c scripts\ci\windows\post_build.bat
if errorlevel 1 (
  echo post_build.bat failed.
  popd
  popd
  exit /b 1
)

echo [3/3] Running assemble.bat...
cmd /c scripts\ci\windows\assemble.bat
if errorlevel 1 (
  echo assemble.bat failed.
  popd
  popd
  exit /b 1
)

popd

echo [4/4] Copying assets to Steam Starbound directory...
xcopy /E /Y /I "..\client_distribution\assets\*" "H:\SteamLibrary\steamapps\common\Starbound\assets\"
echo [5/5] Copying win files to Steam Starbound win64 directory...
xcopy /E /Y /I "..\client_distribution\win\*" "H:\SteamLibrary\steamapps\common\Starbound\win64\"

echo Done: incremental build, release, and deploy scripts completed.
popd
exit /b 0
