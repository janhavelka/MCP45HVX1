@echo off
setlocal

set "PIO_EXE=%USERPROFILE%\.platformio\penv\Scripts\pio.exe"

if not exist "%PIO_EXE%" (
    >&2 echo PlatformIO Core was not found at: "%PIO_EXE%".
    >&2 echo Install the VS Code PlatformIO extension, or run a PlatformIO already on PATH directly.
    exit /b 1
)

"%PIO_EXE%" %*
exit /b %ERRORLEVEL%
