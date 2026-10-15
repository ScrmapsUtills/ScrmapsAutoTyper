@echo off
setlocal
cd /d "%~dp0"
gcc -std=c11 -Wall -Wextra -Werror -O2 -ISource Source\typing.c Tests\test_typing.c -o Tests\test_typing.exe
if errorlevel 1 exit /b 1
Tests\test_typing.exe
pause
