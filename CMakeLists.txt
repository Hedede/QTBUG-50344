cmake_minimum_required(VERSION 3.16)
project(qtbug50344 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Test)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Test)

add_executable(repro main.cpp)
target_compile_definitions(repro PRIVATE REPRO_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(repro PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Test)
