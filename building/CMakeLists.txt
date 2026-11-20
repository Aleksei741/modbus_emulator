cmake_minimum_required(VERSION 3.21)

project(ModbusRtuEmulatorBuild LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../Emulator" emulator)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../Poller" poller)
