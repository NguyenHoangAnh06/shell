@echo off
setlocal
cd /d "%~dp0"

REM ================================================================
REM  myShell demonstration test suite
REM
REM  Run from PowerShell/CMD:
REM      .\test.bat
REM
REM  It can also be entered inside myShell:
REM      test.bat
REM
REM  tests\demo_input.txt is redirected into myShell.exe so date,
REM  time, cd, dir, path, addpath, list and help are tested as
REM  myShell built-ins rather than CMD built-ins.
REM ================================================================

if not exist myShell.exe (
    echo [ERROR] myShell.exe not found.
    echo Build the project before running this test.
    exit /b 1
)

if not exist tests\demo_input.txt (
    echo [ERROR] tests\demo_input.txt not found.
    exit /b 1
)

echo ================================================================
echo  myShell Test Suite
echo ================================================================
echo.

myShell.exe < tests\demo_input.txt 2>&1
set "TEST_RESULT=%ERRORLEVEL%"

echo.
if not "%TEST_RESULT%"=="0" (
    echo ================================================================
    echo  TEST SUITE FAILED - exit code %TEST_RESULT%
    echo ================================================================
    exit /b %TEST_RESULT%
)

echo ================================================================
echo  Scripted demonstration completed.
echo.
echo  Manual tests:
echo    notepad ^&          -- start a background process
echo    list                -- copy the displayed PID
echo    stop ^<pid^>         -- mark process Stopped
echo    resume ^<pid^>       -- mark process Running
echo    kill ^<pid^>         -- terminate process
echo    ping -t 127.0.0.1   -- press CTRL+C; ping must stop
echo ================================================================

exit /b 0
