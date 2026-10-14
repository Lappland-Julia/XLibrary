cmake_minimum_required(VERSION 4.2)
project(XLibrary)

set(CMAKE_CXX_STANDARD 23)

find_package(Threads REQUIRED)

add_executable(XLibrary
        main.cpp
        src/Application.cpp
        src/Application.h
        src/Manager.cpp
        src/Manager.h
        src/Heartbeat.cpp
        src/Heartbeat.h
        src/WindowManager.cpp
        src/WindowManager.h)

target_link_libraries(XLibrary PRIVATE ole32 version shell32 Threads::Threads)
