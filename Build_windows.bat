@echo off
setlocal
cd /d "%~dp0Source"
windres app.rc -O coff -o app.res.o
if errorlevel 1 exit /b 1
gcc -std=c11 -Wall -Wextra -Werror -O2 -municode -mwindows -static -static-libgcc -finput-charset=UTF-8 main.c typing.c app.res.o -o ..\AutoTyper.exe -lcomctl32 -lshell32 -lgdi32 -luser32
if errorlevel 1 exit /b 1
echo Built AutoTyper.exe
pause
