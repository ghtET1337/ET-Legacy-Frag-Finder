@echo off
setlocal

where cl >nul 2>nul
if errorlevel 1 (
    echo Error: run this file from "Developer Command Prompt for Visual Studio".
    exit /b 1
)

if not exist build\windows mkdir build\windows
if not exist dist mkdir dist

rc /nologo /fo build\windows\app.res src\app.rc
if errorlevel 1 exit /b 1

set COMMON=/nologo /std:c++17 /O2 /EHsc /W4 /permissive- /utf-8 /MT /DNDEBUG /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /Isrc
cl /nologo /O2 /TC /MT /DNDEBUG /DSQLITE_THREADSAFE=1 /DSQLITE_OMIT_LOAD_EXTENSION /c third_party\sqlite\sqlite3.c /Fo:build\windows\sqlite3.obj
if errorlevel 1 exit /b 1
cl %COMMON% /c src\etl_demo_parser.cpp /Fo:build\windows\etl_demo_parser.obj
if errorlevel 1 exit /b 1
cl %COMMON% /c src\idtech3_huffman.cpp /Fo:build\windows\idtech3_huffman.obj
if errorlevel 1 exit /b 1
cl %COMMON% /c src\app_storage.cpp /Fo:build\windows\app_storage.obj
if errorlevel 1 exit /b 1
cl %COMMON% /c src\clip_export.cpp /Fo:build\windows\clip_export.obj
if errorlevel 1 exit /b 1
cl %COMMON% /c src\clip_export_window.cpp /Fo:build\windows\clip_export_window.obj
if errorlevel 1 exit /b 1
cl %COMMON% /c src\win_main.cpp /Fo:build\windows\win_main.obj
if errorlevel 1 exit /b 1
cl %COMMON% /c src\main_cli.cpp /Fo:build\windows\main_cli.obj
if errorlevel 1 exit /b 1

cl /nologo /Fe:dist\ETLFragFinder.exe build\windows\sqlite3.obj build\windows\etl_demo_parser.obj build\windows\idtech3_huffman.obj build\windows\app_storage.obj build\windows\clip_export.obj build\windows\clip_export_window.obj build\windows\win_main.obj build\windows\app.res /link /SUBSYSTEM:WINDOWS comctl32.lib comdlg32.lib shell32.lib ole32.lib uuid.lib gdi32.lib dwmapi.lib uxtheme.lib
if errorlevel 1 exit /b 1
cl /nologo /Fe:dist\etl-frag-cli.exe build\windows\sqlite3.obj build\windows\etl_demo_parser.obj build\windows\idtech3_huffman.obj build\windows\app_storage.obj build\windows\clip_export.obj build\windows\main_cli.obj
if errorlevel 1 exit /b 1

echo.
echo Ready:  dist\ETLFragFinder.exe
echo          dist\etl-frag-cli.exe
