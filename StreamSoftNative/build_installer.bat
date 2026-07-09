@echo off
setlocal

set VCVARS="C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
set ROOT=%~dp0

echo Setting up MSVC environment...
call %VCVARS% || goto :error

cd /d "%ROOT%"

echo Configuring...
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 || goto :error

echo Building streamsoft_gui (Release)...
cmake --build build --config Release --target streamsoft_gui || goto :error

echo Building installer (Inno Setup)...
cmake --build build --config Release --target installer || goto :error

echo.
echo Done: installer\Output\StreamSoftSetup.exe
exit /b 0

:error
echo.
echo Build failed.
pause
exit /b 1
