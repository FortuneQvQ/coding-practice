@echo off
setlocal
chcp 65001 >nul
title 校园资讯聚合系统 - 更新程序

set "ROOT=%~dp0"
set "APP=%ROOT%bin\x64\Release\CampusNewsSystem.exe"
cd /d "%ROOT%"

if not exist "%APP%" (
    echo [错误] 未找到 Release 可执行程序：
    echo %APP%
    echo 请先在 Visual Studio 中编译 Release x64。
    echo.
    pause
    exit /b 1
)

echo 校园资讯聚合系统正在更新，请保持此窗口开启。
echo.
"%APP%" %*
set "EXIT_CODE=%ERRORLEVEL%"
echo.
if "%EXIT_CODE%"=="0" (
    echo 程序已正常运行结束。
) else (
    echo [错误] 程序运行失败，退出码为 %EXIT_CODE%。
)
echo.
pause
endlocal & exit /b %EXIT_CODE%
