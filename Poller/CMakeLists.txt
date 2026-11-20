cmake_minimum_required(VERSION 3.21)

project(ModbusRtuPoller LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

set(_emulator_root "${CMAKE_CURRENT_LIST_DIR}/../Emulator")

add_executable(modbus_rtu_poller WIN32
    src/main.cpp
    src/main_frame.cpp
    src/modbus_master.cpp
    src/poll_engine.cpp
    src/serial_rtu_master.cpp
    "${_emulator_root}/src/app_art.cpp"
    "${_emulator_root}/src/config_store.cpp"
    "${_emulator_root}/src/modbus_crc.cpp"
    "${_emulator_root}/src/register_panel.cpp"
    "${_emulator_root}/src/register_store.cpp"
    "${_emulator_root}/src/serial_ports.cpp"
    "${_emulator_root}/src/serial_rtu.cpp"
)

target_include_directories(modbus_rtu_poller
    PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/include"
        "${_emulator_root}/include"
)

set(_boost_lib)
if(TARGET Boost::headers)
    set(_boost_lib Boost::headers)
elseif(TARGET Boost::boost)
    set(_boost_lib Boost::boost)
endif()

target_link_libraries(modbus_rtu_poller
    PRIVATE
        nlohmann_json::nlohmann_json
        ${_wx_libs}
        ${_boost_lib}
)

if(NOT _boost_lib)
    target_include_directories(modbus_rtu_poller PRIVATE ${Boost_INCLUDE_DIRS})
endif()

target_compile_definitions(modbus_rtu_poller
    PRIVATE
        UNICODE
        _UNICODE
        BOOST_ASIO_NO_DEPRECATED
        BOOST_ASIO_NO_WIN32_LEAN_AND_MEAN
)

if(WIN32)
    target_compile_definitions(modbus_rtu_poller PRIVATE _WIN32_WINNT=0x0A00)
    target_link_libraries(modbus_rtu_poller PRIVATE ws2_32 mswsock advapi32)
endif()

if(MINGW)
    target_compile_options(modbus_rtu_poller PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/modbus_rtu_poller")
set_target_properties(modbus_rtu_poller PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/modbus_rtu_poller"
    WIN32_EXECUTABLE TRUE
)

add_custom_command(TARGET modbus_rtu_poller POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_LIST_DIR}/../assets/modbus_icon.png"
        "$<TARGET_FILE_DIR:modbus_rtu_poller>/modbus_icon.png"
    COMMENT "Copying Modbus icon next to the poller"
)
