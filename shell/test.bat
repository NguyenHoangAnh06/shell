@echo off
REM ================================================================
REM  test.bat  --  myShell regression / demonstration test suite
REM
REM  How to run:
REM    1. Build myShell:  make  (or compile manually)
REM    2. From PowerShell / cmd:
REM         .\myShell.exe < test.bat
REM       OR just run myShell and type:  test.bat
REM
REM  Each test prints a header, runs the command, and moves on.
REM  Inspect the output to verify results.
REM ================================================================

echo.
echo ================================================================
echo  myShell Test Suite
echo ================================================================

echo.
echo [TEST 1] date -- expect YYYY-MM-DD
date

echo.
echo [TEST 2] time -- expect HH:MM:SS
time

echo.
echo [TEST 3] dir -- expect directory listing of current folder
dir

echo.
echo [TEST 4] cd without args -- expect current path printed
cd

echo.
echo [TEST 5] cd to a valid path then back
cd C:\Windows
cd
cd C:\Users\ADMIN\Documents\shell
cd

echo.
echo [TEST 6] cd to an invalid path -- expect error message
cd C:\this_path_does_not_exist_xyz

echo.
echo [TEST 7] path -- expect PATH displayed with session note
path

echo.
echo [TEST 8] addpath -- add a dummy dir, verify it appears
addpath C:\mytest_temp_dir
path

echo.
echo [TEST 9] list -- no background processes yet, expect empty list
list

echo.
echo [TEST 10] Foreground process -- ping (waits for completion)
ping -n 2 127.0.0.1

echo.
echo [TEST 11] help -- expect built-in command table
help

echo.
echo ================================================================
echo  All scripted tests done.
echo  Manual tests to run separately:
echo    notepad ^&         -- background process
echo    list               -- should show notepad entry
echo    stop ^<pid^>        -- suspend notepad
echo    resume ^<pid^>      -- resume notepad
echo    kill ^<pid^>        -- terminate notepad
echo    CTRL+C during ping -t 127.0.0.1  -- kills ping, shell lives
echo ================================================================

exit
