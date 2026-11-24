@echo off
setlocal

:: Rebuilds the app (see build.bat) and packages it into a Windows Installer
:: (MSI) that other people can run to install the simulator on their own PC.
:: Requires the WiX Toolset v5 CLI: dotnet tool install --global wix --version 5.0.2
:: then: wix extension add -g WixToolset.UI.wixext/5.0.2

call "%~dp0build.bat"
if errorlevel 1 exit /b 1

set PATH=%PATH%;%USERPROFILE%\.dotnet\tools

:: cd into installer\ first -- Product.wxs's File sources (..\build\...,
:: ..\resources\..., License.rtf) are relative to wix's own working
:: directory, not to the .wxs file's location, so running this from the
:: project root without cd'ing in first fails to find every one of them.
pushd "%~dp0installer"
wix build "Product.wxs" -ext WixToolset.UI.wixext -arch x64 -o "BlackHoleSimulatorSetup.msi"
set BUILD_RESULT=%ERRORLEVEL%
popd
if %BUILD_RESULT% neq 0 (
    echo.
    echo *** MSI packaging failed -- see errors above. ***
    pause
    exit /b 1
)

echo.
echo Installer ready: installer\BlackHoleSimulatorSetup.msi
