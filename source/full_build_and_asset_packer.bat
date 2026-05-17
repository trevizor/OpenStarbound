@echo off
setlocal

REM Run from this script's directory so preset/build paths resolve consistently.
pushd "%~dp0"

echo [1/5] Configuring (windows-release-VS2022)...
cmake --preset windows-release-VS2022
if errorlevel 1 (
  echo Configure failed. Aborting.
  popd
  exit /b 1
)

echo [2/5] Full rebuild (clean first)...
cmake --build --preset windows-release-VS2022 --clean-first
if errorlevel 1 (
  echo Build failed. Aborting.
  popd
  exit /b 1
)

REM Asset packing scripts expect to run from repository root.
pushd ..

echo [3/5] Running post_build.bat...
cmd /c scripts\ci\windows\post_build.bat
if errorlevel 1 (
  echo post_build.bat failed.
  popd
  popd
  exit /b 1
)

echo [4/5] Packing client assets...
if not exist dist\assets mkdir dist\assets
.\dist\asset_packer.exe -c scripts\packing.config assets\opensb dist\assets\opensb_client.pak
if errorlevel 1 (
  echo Client asset packing failed.
  popd
  popd
  exit /b 1
)

echo [5/5] Packing server assets...
.\dist\asset_packer.exe -c scripts\packing.config -s assets\opensb dist\assets\opensb_server.pak
if errorlevel 1 (
  echo Server asset packing failed.
  popd
  popd
  exit /b 1
)

popd
echo Done: full build and asset packing completed.
popd
exit /b 0
