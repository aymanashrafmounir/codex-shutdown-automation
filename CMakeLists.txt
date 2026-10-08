cmake_minimum_required(VERSION 3.21)
project(codex_shutdown_automation VERSION 0.2.1 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Widgets Sql Network)

add_library(csa_core STATIC
    src/native/types.h src/native/policy.h src/native/policy.cpp
    src/native/store.h src/native/store.cpp
    src/native/controller.h src/native/controller.cpp
    src/native/codex_monitor.h src/native/codex_monitor.cpp
    src/native/background_monitor.h src/native/background_monitor.cpp)
target_include_directories(csa_core PUBLIC src/native)
target_link_libraries(csa_core PUBLIC Qt6::Core Qt6::Sql)
add_library(csa_ui STATIC src/native/main_window.h src/native/main_window.cpp
    src/native/window_style.h src/native/window_style.cpp)
target_link_libraries(csa_ui PUBLIC csa_core Qt6::Widgets)
qt_add_executable(CodexShutdownAutomation WIN32 src/native/main.cpp
    src/native/windows_shutdown.h src/native/windows_shutdown.cpp)
target_link_libraries(CodexShutdownAutomation PRIVATE csa_ui Qt6::Network)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    foreach(suite policy controller monitor background ui)
        qt_add_executable(${suite}_test tests/native/${suite}_test.cpp)
        target_link_libraries(${suite}_test PRIVATE csa_core Qt6::Test)
        if(suite STREQUAL "ui")
            target_link_libraries(${suite}_test PRIVATE csa_ui)
        endif()
        add_test(NAME ${suite} COMMAND ${suite}_test)
        set_tests_properties(${suite} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endforeach()
    if(WIN32)
        find_program(POWERSHELL_EXECUTABLE NAMES pwsh.exe powershell.exe REQUIRED)
        add_test(NAME window-launch COMMAND "${POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/native/window_launch_test.ps1"
            -Executable "$<TARGET_FILE:CodexShutdownAutomation>")
        set_tests_properties(window-launch PROPERTIES TIMEOUT 90)
    endif()
endif()
if(MINGW)
    target_compile_options(csa_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(csa_ui PRIVATE -Wall -Wextra -Wpedantic)
endif()
