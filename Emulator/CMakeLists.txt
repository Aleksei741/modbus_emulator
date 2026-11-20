cmake_minimum_required(VERSION 3.21)

project(ModbusRtuEmulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Boost CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

set(_wx_libs)
find_package(wxWidgets 3.2 CONFIG QUIET COMPONENTS core base)
if(NOT wxWidgets_FOUND)
    find_package(wxWidgets CONFIG QUIET COMPONENTS core base)
endif()
if(wxWidgets_FOUND)
    set(_wx_libs wx::core wx::base)
    if(TARGET wx::adv)
        list(APPEND _wx_libs wx::adv)
    endif()
else()
    find_package(wxWidgets REQUIRED COMPONENTS core base)
    include(${wxWidgets_USE_FILE})
    set(_wx_libs ${wxWidgets_LIBRARIES})
endif()

add_executable(modbus_rtu_emulator WIN32
    src/main.cpp
    src/app_art.cpp
    src/config_store.cpp
    src/main_frame.cpp
    src/modbus_crc.cpp
    src/modbus_slave.cpp
    src/register_panel.cpp
    src/register_store.cpp
    src/serial_ports.cpp
    src/serial_rtu.cpp
)

target_include_directories(modbus_rtu_emulator
    PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/include"
)

set(_boost_lib)
if(TARGET Boost::headers)
    set(_boost_lib Boost::headers)
elseif(TARGET Boost::boost)
    set(_boost_lib Boost::boost)
endif()

target_link_libraries(modbus_rtu_emulator
    PRIVATE
        nlohmann_json::nlohmann_json
        ${_wx_libs}
        ${_boost_lib}
)

if(NOT _boost_lib)
    target_include_directories(modbus_rtu_emulator PRIVATE ${Boost_INCLUDE_DIRS})
endif()

target_compile_definitions(modbus_rtu_emulator
    PRIVATE
        UNICODE
        _UNICODE
        BOOST_ASIO_NO_DEPRECATED
        BOOST_ASIO_NO_WIN32_LEAN_AND_MEAN
)

if(WIN32)
    target_compile_definitions(modbus_rtu_emulator PRIVATE _WIN32_WINNT=0x0A00)
    target_link_libraries(modbus_rtu_emulator PRIVATE ws2_32 mswsock advapi32)
endif()

if(MINGW)
    target_compile_options(modbus_rtu_emulator PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/modbus_rtu_emulator")
set_target_properties(modbus_rtu_emulator PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/modbus_rtu_emulator"
    WIN32_EXECUTABLE TRUE
)

add_custom_command(TARGET modbus_rtu_emulator POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_LIST_DIR}/../assets/modbus_icon.png"
        "$<TARGET_FILE_DIR:modbus_rtu_emulator>/modbus_icon.png"
    COMMENT "Copying Modbus icon next to the emulator"
)
