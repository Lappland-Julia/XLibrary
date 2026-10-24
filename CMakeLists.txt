cmake_minimum_required(VERSION 3.21)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" XLIBRARY_RELEASE_VERSION)
string(STRIP "${XLIBRARY_RELEASE_VERSION}" XLIBRARY_RELEASE_VERSION)
if(NOT XLIBRARY_RELEASE_VERSION MATCHES "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$")
    message(FATAL_ERROR "VERSION must contain a version in major.minor.patch format.")
endif()
project(XLibrary VERSION "${XLIBRARY_RELEASE_VERSION}" LANGUAGES C CXX)

# Использовать время распаковки файлов загружаемых архивов.
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# Учитывать общие флаги проекта при проверке поддержки IPO/LTO.
if(POLICY CMP0138)
    cmake_policy(SET CMP0138 NEW)
endif()

if(WIN32)
    enable_language(RC)
elseif(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "XLibrary supports Windows and Linux.")
endif()

# ------------------------------------------------------------
# Стандарт языка
# ------------------------------------------------------------

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Используется clang-tidy и другими инструментами.
# Поддерживается генераторами Ninja и Makefiles.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ------------------------------------------------------------
# Параметры сборки
# ------------------------------------------------------------

option(XLIBRARY_ENABLE_IPO
        "Enable IPO/LTO for Release builds when supported"
        ON
)

option(XLIBRARY_NATIVE_CPU
        "Optimize Release builds for the build computer CPU (not portable)"
        OFF
)

# ------------------------------------------------------------
# Зависимости
# ------------------------------------------------------------

include(FetchContent)
find_package(Threads REQUIRED)
if(WIN32)
    set(CMAKE_INSTALL_BINDIR ".")
endif()
include(GNUInstallDirs)
find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Concurrent Network)
find_package(Qt6 6.5 REQUIRED COMPONENTS LinguistTools)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(Qt6 6.5 REQUIRED COMPONENTS DBus)
    find_package(X11)
endif()

FetchContent_Declare(sqlite3
        URL
        https://sqlite.org/2026/sqlite-amalgamation-3540000.zip
        URL_HASH
        SHA3_256=7b670a62fdfbd672b75fef004cb703c8a3e87d3a5cc7d675b4a08337004a2d93
)

FetchContent_MakeAvailable(sqlite3)

# Архив amalgamation содержит исходники без CMake-проекта.
add_library(sqlite3 STATIC
        "${sqlite3_SOURCE_DIR}/sqlite3.c"
)

target_include_directories(sqlite3 SYSTEM PUBLIC
        "${sqlite3_SOURCE_DIR}"
)

target_link_libraries(sqlite3 PUBLIC
        Threads::Threads
        ${CMAKE_DL_LIBS}
)

# ------------------------------------------------------------
# Приложение
# ------------------------------------------------------------

add_library(xlibrary_core STATIC

        src/Application.cpp
        src/Application.h

        src/Manager.cpp
        src/Manager.h

        src/Heartbeat.cpp
        src/Heartbeat.h

        src/Database.cpp
        src/Database.h

        src/Backend.h

        src/WindowManager.cpp
        src/WindowManager.h

)

if(WIN32)
    target_sources(xlibrary_core PRIVATE src/RegistryScan.cpp src/RegistryScan.h)
    target_compile_definitions(xlibrary_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(xlibrary_core PUBLIC version shell32 gdi32 advapi32)
else()
    target_sources(xlibrary_core PRIVATE src/linux_process.cpp src/linux_process.h)
endif()
target_include_directories(xlibrary_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(xlibrary_core PUBLIC
        sqlite3
        Threads::Threads

)

qt_add_executable(XLibrary WIN32
        main.cpp
        desktop/catalog_types.h
        desktop/catalog_store.h desktop/catalog_store.cpp
        desktop/platform_integration.h
        desktop/path_helpers.h
        desktop/application_paths.h
        desktop/localization.h desktop/localization.cpp
        desktop/library_model.h desktop/library_model.cpp
        desktop/desktop_service.h desktop/desktop_service.cpp
        desktop/appearance.h desktop/appearance.cpp
        desktop/selection_button.h desktop/selection_button.cpp
        desktop/library_splitter.h desktop/library_splitter.cpp
        desktop/ui_registry.h desktop/ui_helpers.h
        desktop/editors.h desktop/editors.cpp
        desktop/library_views.h desktop/library_views.cpp
        desktop/settings_view.h desktop/settings_view.cpp
        desktop/main_window.h desktop/main_window.cpp
)
set_target_properties(XLibrary PROPERTIES AUTOMOC ON)
qt_add_lrelease(XLibrary TS_FILES translations/xlibrary_ru.ts translations/qt_widgets_ru.ts
        QM_FILES_OUTPUT_VARIABLE xlibrary_qm_files)
qt_add_resources(XLibrary application_translations
        PREFIX "/i18n"
        BASE "${CMAKE_CURRENT_BINARY_DIR}"
        FILES ${xlibrary_qm_files}
)
if(WIN32)
    target_sources(XLibrary PRIVATE desktop/windows_integration.cpp assets/application_icon.rc)
    target_include_directories(XLibrary PRIVATE "$<$<COMPILE_LANGUAGE:RC>:${CMAKE_CURRENT_SOURCE_DIR}>")
    set_source_files_properties(assets/application_icon.rc PROPERTIES
            OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/xlibrary.ico"
    )
    if(MINGW)
        # windres через popen некорректно обрабатывает пробелы в пути к препроцессору.
        set_property(SOURCE assets/application_icon.rc APPEND PROPERTY COMPILE_OPTIONS --use-temp-file)
    endif()
else()
    target_sources(XLibrary PRIVATE desktop/linux_integration.cpp)
    target_link_libraries(XLibrary PRIVATE Qt6::DBus)
    if(X11_FOUND)
        target_link_libraries(XLibrary PRIVATE X11::X11)
        target_compile_definitions(XLibrary PRIVATE XLIBRARY_HAVE_X11=1)
    endif()
endif()
qt_add_resources(XLibrary application_images
        PREFIX "/"
        FILES assets/xlibrary.ico
)
target_compile_definitions(XLibrary PRIVATE XLIBRARY_VERSION="${PROJECT_VERSION}")

target_link_libraries(XLibrary PRIVATE xlibrary_core Qt6::Widgets Qt6::Concurrent Qt6::Network)

if(WIN32)
    target_link_libraries(XLibrary PRIVATE ole32 uuid dwmapi)
    configure_file(packaging/windows_version.rc.in windows_version.rc @ONLY)
    target_sources(XLibrary PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/windows_version.rc")
    if(MINGW)
        set_property(SOURCE "${CMAKE_CURRENT_BINARY_DIR}/windows_version.rc" APPEND PROPERTY
                COMPILE_OPTIONS --use-temp-file)
    endif()

    # Метаданные выбранного toolchain нужны только явной упаковке.
    configure_file(packaging/build_config.json.in packaging-config.json @ONLY)
endif()

if(MSVC)
    target_compile_options(XLibrary PRIVATE /utf-8 /W4)
else()
    target_compile_options(XLibrary PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS XLibrary RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(WIN32)
    install(IMPORTED_RUNTIME_ARTIFACTS Qt6::Concurrent RUNTIME DESTINATION .)
    # Qt развёртывается только при install; CRT добавляет упаковщик из выбранного toolchain.
    qt_generate_deploy_app_script(TARGET XLibrary OUTPUT_SCRIPT xlibrary_deploy_script
            NO_TRANSLATIONS NO_COMPILER_RUNTIME)
    install(SCRIPT "${xlibrary_deploy_script}")
    install(FILES LICENSE DESTINATION .)
else()
    install(FILES packaging/io.github.LapplandJulia.XLibrary.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES assets/github_avatar_256.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
            RENAME io.github.LapplandJulia.XLibrary.png)
    install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/XLibrary)
    include(packaging/linux_package.cmake)
endif()

# ------------------------------------------------------------
# Release: O2
# ------------------------------------------------------------

# MSVC также охватывает совместимые с cl компиляторы, например
# clang-cl. Для них используется /O2.
#
# Для GCC и Clang с GNU-интерфейсом используется -O2.
# Опции цели добавляются после стандартных флагов конфигурации.

foreach(xlibrary_target IN ITEMS XLibrary xlibrary_core sqlite3)
    if(MSVC)
        target_compile_options(${xlibrary_target} PRIVATE
                "$<$<CONFIG:Release>:/O2>"
        )
    else()
        target_compile_options(${xlibrary_target} PRIVATE
                "$<$<AND:$<CONFIG:Release>,$<COMPILE_LANG_AND_ID:C,GNU,Clang,AppleClang>>:-O2>"
                "$<$<AND:$<CONFIG:Release>,$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>>:-O2>"
        )
    endif()
endforeach()

# ------------------------------------------------------------
# IPO / LTO для Release
# ------------------------------------------------------------

if(XLIBRARY_ENABLE_IPO)
    include(CheckIPOSupported)

    check_ipo_supported(
            RESULT XLIBRARY_IPO_SUPPORTED
            OUTPUT XLIBRARY_IPO_ERROR
            LANGUAGES C CXX
    )

    if(XLIBRARY_IPO_SUPPORTED)
        set_property(TARGET XLibrary xlibrary_core sqlite3 PROPERTY
                INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
        )
    else()
        message(STATUS
                "IPO/LTO is unavailable and will be disabled: "
                "${XLIBRARY_IPO_ERROR}"
        )
    endif()
endif()

# ------------------------------------------------------------
# Оптимизация под CPU компьютера сборки
# ------------------------------------------------------------

# Для публично распространяемых бинарников оставлять OFF.
# Полученный код может не работать на другом процессоре.

if(XLIBRARY_NATIVE_CPU)
    if(CMAKE_CROSSCOMPILING)
        message(FATAL_ERROR
                "XLIBRARY_NATIVE_CPU cannot be used when cross-compiling."
        )
    endif()

    if(MSVC)
        message(FATAL_ERROR
                "XLIBRARY_NATIVE_CPU is not implemented for MSVC or clang-cl. "
                "Configure with -DXLIBRARY_NATIVE_CPU=OFF."
        )
    endif()

    if(NOT CMAKE_C_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$"
            OR NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
        message(FATAL_ERROR
                "XLIBRARY_NATIVE_CPU requires GCC or Clang "
                "with a GNU-compatible command-line interface."
        )
    endif()

    include(CheckCCompilerFlag)
    include(CheckCXXCompilerFlag)

    check_c_compiler_flag(
            "-march=native"
            XLIBRARY_C_NATIVE_SUPPORTED
    )

    check_cxx_compiler_flag(
            "-march=native"
            XLIBRARY_CXX_NATIVE_SUPPORTED
    )

    if(NOT XLIBRARY_C_NATIVE_SUPPORTED
            OR NOT XLIBRARY_CXX_NATIVE_SUPPORTED)
        message(FATAL_ERROR
                "XLIBRARY_NATIVE_CPU was requested, but -march=native "
                "is not supported by both configured compilers."
        )
    endif()

    target_compile_options(sqlite3 PRIVATE
            "$<$<CONFIG:Release>:-march=native>"
    )

    target_compile_options(XLibrary PRIVATE
            "$<$<CONFIG:Release>:-march=native>"
    )
    target_compile_options(xlibrary_core PRIVATE "$<$<CONFIG:Release>:-march=native>")
endif()
