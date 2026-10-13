cmake_minimum_required(VERSION 4.2)
project(XLibrary)

set(CMAKE_CXX_STANDARD 23)

add_executable(XLibrary
        main.cpp
        src/Application.cpp
        src/Application.h
        src/Manager.cpp
        src/Manager.h)

target_link_libraries(XLibrary PRIVATE ole32 version)
