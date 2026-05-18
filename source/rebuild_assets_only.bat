@echo off
setlocal

REM Asset packing scripts expect to run from repository root.
pushd ..

echo Packing client assets...
if not exist dist\assets mkdir dist\assets
.\dist\asset_packer.exe -c scripts\packing.config assets\opensb dist\assets\opensb_client.pak
if errorlevel 1 (
  echo Client asset packing failed.
  popd
  popd
  exit /b 1
)

echo Packing server assets...
.\dist\asset_packer.exe -c scripts\packing.config -s assets\opensb dist\assets\opensb_server.pak
if errorlevel 1 (
  echo Server asset packing failed.
  popd
  popd
  exit /b 1
)

echo Running post_build.bat...
cmd /c scripts\ci\windows\post_build.bat
if errorlevel 1 (
  echo post_build.bat failed.
  popd
  popd
  exit /b 1
)

echo Running assemble.bat...
cmd /c scripts\ci\windows\assemble.bat
if errorlevel 1 (
  echo assemble.bat failed.
  popd
  popd
  exit /b 1
)

echo Copying win files to Steam Starbound win64 directory...
xcopy /E /Y /I "client_distribution\assets\*" "H:\SteamLibrary\steamapps\common\Starbound\assets\"
xcopy /E /Y /I "client_distribution\win\*" "H:\SteamLibrary\steamapps\common\Starbound\win64\"

popd
echo Done: full build and asset packing completed.
popd
exit /b 0
