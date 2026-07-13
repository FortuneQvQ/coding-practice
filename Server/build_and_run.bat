@echo off
set PATH=C:\Program Files\RedPanda-Cpp\mingw64\bin;%PATH%
cd /d "%~dp0"
echo 正在编译...
g++ -std=c++17 -static -I../DataBase -I../DataBase/ThirdParty/SQLite -o main.exe main.cpp ../DataBase/database.cpp -x c ../DataBase/ThirdParty/SQLite/sqlite3.c
if %errorlevel% equ 0 (
    echo 编译成功!
    echo 正在运行...
    chcp 65001
    main.exe
) else (
    echo 编译失败!
)
pause