@echo off
cd /d "c:\Users\29120\Documents\GitHub\coding-practice\Server"
set PATH=C:\Program Files\Git\mingw64\bin;%PATH%
g++ -std=c++17 -I../DataBase -I../DataBase/ThirdParty/SQLite -o main.exe main.cpp ../DataBase/database.cpp ../DataBase/ThirdParty/SQLite/sqlite3.c
if %errorlevel% equ 0 (
    echo Build succeeded!
    main.exe
) else (
    echo Build failed!
    pause
)