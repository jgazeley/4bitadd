@echo off
REM Check if the "build" folder exists; if not, create it.
if not exist "build" (
    mkdir build
)

echo Running nmake...
nmake

pause