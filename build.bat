@echo off
REM ============================================================
REM  GamePreloader V2.0 build script
REM  1) 编译 UE5 解析库为独立 DLL -> park\UE5PakParser.dll
REM  2) 编译主程序（不含 PakParser.cpp），delay-load 链接解析库
REM  3) V2.0 引擎无关的通用大文件预读（Unity *_Data / ForzaTech media）内置在主程序，
REM     无需额外 DLL，自动兼容《午夜轮班》《地平线6》等非 UE 游戏。
REM ============================================================
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
cd /d "%~dp0"

if not exist bin mkdir bin
if not exist bin\dll mkdir bin\dll
if not exist park mkdir park

set COMMON_FLAGS=/EHsc /std:c++17 /utf-8 /W3 /nologo /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX

echo === [1/4] Build UE5 parser DLL -> park\UE5PakParser.dll ===
cl.exe /LD %COMMON_FLAGS% /DGAMEPAK_EXPORTS ^
    /Fo"bin\dll\\" ^
    src\PakParser.cpp src\Common.cpp ^
    /Fe"park\UE5PakParser.dll" ^
    /link /DLL /OUT:"park\UE5PakParser.dll" /IMPLIB:"bin\UE5PakParser.lib" ^
    shell32.lib dxgi.lib
if errorlevel 1 ( echo DLL COMPILE FAILED & exit /b 1 )

echo === [2/4] Compile main program objects (no PakParser.cpp) ===
cl.exe /c %COMMON_FLAGS% ^
    /Fo"bin\\" ^
    src\main.cpp src\Common.cpp src\Config.cpp src\ProcessMonitor.cpp ^
    src\GameDetector.cpp src\Preprocessor.cpp src\FloatingWindow.cpp
if errorlevel 1 ( echo COMPILE FAILED & exit /b 1 )

echo === [3/4] Compile resources app.rc -> app.res ===
rc.exe /nologo /fo"bin\app.res" app.rc
if errorlevel 1 ( echo RC FAILED & exit /b 1 )

echo === [4/4] Link GamePreloader.exe (delay-load parser DLL) ===
link.exe /nologo /OUT:"GamePreloader.exe" /SUBSYSTEM:WINDOWS ^
    bin\main.obj bin\Common.obj bin\Config.obj bin\ProcessMonitor.obj ^
    bin\GameDetector.obj bin\Preprocessor.obj bin\FloatingWindow.obj ^
    bin\app.res ^
    bin\UE5PakParser.lib ^
    /DELAYLOAD:UE5PakParser.dll delayimp.lib ^
    psapi.lib shell32.lib user32.lib gdi32.lib advapi32.lib ole32.lib comdlg32.lib powrprof.lib dxgi.lib
if errorlevel 1 ( echo LINK FAILED & exit /b 1 )

del /q bin\*.obj bin\*.res 2>nul
del /q bin\dll\*.obj 2>nul
echo === SUCCESS ===
dir GamePreloader.exe
dir park\UE5PakParser.dll
exit /b 0
