@echo off
setlocal
chcp 65001 >nul
title 校园资讯聚合系统 - 网站服务

set "ROOT=%~dp0"
set "SITE=%ROOT%output"
set "URL=http://127.0.0.1:8765/index.html"
cd /d "%ROOT%"

if not exist "%SITE%\index.html" (
    echo [错误] 未找到网站首页：
    echo %SITE%\index.html
    echo 请先运行“运行更新.bat”生成网站文件。
    echo.
    pause
    exit /b 1
)

rem 如果端口上已有服务，直接打开网站，避免第二次启动因端口冲突而闪退。
netstat -ano | findstr /r /c:":8765 .*LISTENING" >nul 2>nul
if not errorlevel 1 (
    echo [提示] 8765 端口上的网站服务已经运行，正在打开浏览器。
    if /i not "%~1"=="--no-browser" start "" "%URL%"
    echo.
    pause
    exit /b 0
)

python.exe -c "import sys" >nul 2>nul
if not errorlevel 1 goto use_python

py.exe -3 -c "import sys" >nul 2>nul
if not errorlevel 1 goto use_py

echo [错误] 未找到可用的 Python 3。
echo 请安装 Python 3，并确保 python.exe 或 py.exe 可以在命令行运行。
echo.
pause
exit /b 1

:use_python
echo 网站服务正在启动：%URL%
echo 请保持此窗口开启；关闭窗口即可停止网站。
if /i not "%~1"=="--no-browser" start "" powershell.exe -NoProfile -WindowStyle Hidden -Command "Start-Sleep -Milliseconds 800; Start-Process '%URL%'"
python.exe -m http.server 8765 --bind 127.0.0.1 --directory "%SITE%"
set "EXIT_CODE=%ERRORLEVEL%"
goto server_ended

:use_py
echo 网站服务正在启动：%URL%
echo 请保持此窗口开启；关闭窗口即可停止网站。
if /i not "%~1"=="--no-browser" start "" powershell.exe -NoProfile -WindowStyle Hidden -Command "Start-Sleep -Milliseconds 800; Start-Process '%URL%'"
py.exe -3 -m http.server 8765 --bind 127.0.0.1 --directory "%SITE%"
set "EXIT_CODE=%ERRORLEVEL%"

:server_ended
echo.
if "%EXIT_CODE%"=="0" (
    echo 网站服务已正常停止。
) else (
    echo [错误] 网站服务启动或运行失败，退出码为 %EXIT_CODE%。
)
echo.
pause
endlocal & exit /b %EXIT_CODE%
