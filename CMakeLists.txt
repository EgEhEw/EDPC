cmake_minimum_required(VERSION 3.20)
project(EDPC VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if (MSVC)
    add_compile_options(/utf-8 /EHsc)
    add_definitions(-DNOMINMAX)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

set(EDPC_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/edpc_core")

if (WIN32)
    if (MSVC)
        set(EDPC_CORE_LIB "${EDPC_CORE_DIR}/target/release/edpc_core.lib")
    else()
        set(EDPC_CORE_LIB "${EDPC_CORE_DIR}/target/release/libedpc_core.a")
    endif()
    set(PLATFORM_LIBS ws2_32 userenv bcrypt ntdll advapi32 user32)
else()
    set(EDPC_CORE_LIB "${EDPC_CORE_DIR}/target/release/libedpc_core.a")
    set(PLATFORM_LIBS pthread dl m)
endif()

# Custom command and target to ensure Rust core is built/updated
add_custom_command(
    OUTPUT "${EDPC_CORE_LIB}"
    COMMAND cargo build --release
    WORKING_DIRECTORY "${EDPC_CORE_DIR}"
    COMMENT "Building Rust edpc_core static library..."
)
add_custom_target(edpc_core_target DEPENDS "${EDPC_CORE_LIB}")

set(SOURCES
    src/main.cpp
    src/mainwindow.h
    src/mainwindow.cpp
    src/vulkan_viewport.h
    src/vulkan_viewport.cpp
    src/vulkan_renderer.h
    src/vulkan_renderer.cpp
)

add_executable(edpc ${SOURCES})
add_dependencies(edpc edpc_core_target)

target_include_directories(edpc PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/edpc_core/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/include"
)

target_link_libraries(edpc PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    "${EDPC_CORE_LIB}"
    ${PLATFORM_LIBS}
)

install(TARGETS edpc RUNTIME DESTINATION bin)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/edpc.desktop" DESTINATION share/applications)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/edpc.png" DESTINATION share/icons/hicolor/256x256/apps)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/edpc.png" DESTINATION share/pixmaps)
