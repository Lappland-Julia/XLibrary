cmake_minimum_required(VERSION 4.2)
project(XLibrary)

set(CMAKE_CXX_STANDARD 23)

include(FetchContent)

FetchContent_Declare(sqlite3
        URL https://sqlite.org/2026/sqlite-amalgamation-3540000.zip
        URL_HASH SHA3_256=7b670a62fdfbd672b75fef004cb703c8a3e87d3a5cc7d675b4a08337004a2d93)
FetchContent_MakeAvailable(sqlite3)

add_library(sqlite3 STATIC "${sqlite3_SOURCE_DIR}/sqlite3.c")
target_include_directories(sqlite3 SYSTEM PUBLIC "${sqlite3_SOURCE_DIR}")

find_package(Threads REQUIRED)

add_executable(XLibrary
        main.cpp
        src/Application.cpp
        src/Application.h
        src/Manager.cpp
        src/Manager.h
        src/Heartbeat.cpp
        src/Heartbeat.h
        src/Database.cpp
        src/Database.h
        src/Backend.h)
        src/WindowManager.cpp
        src/WindowManager.h)

target_link_libraries(XLibrary PRIVATE ole32 version shell32 Threads::Threads sqlite3)
