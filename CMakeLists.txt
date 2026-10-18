cmake_minimum_required(VERSION 3.20)
project(EDPC VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)
find_package(Vulkan)

set(EDPC_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/edpc_core")
set(EDPC_CORE_LIB "${EDPC_CORE_DIR}/target/release/libedpc_core.a")

# Custom target to ensure Rust core is always built/updated
add_custom_target(edpc_core_target
    COMMAND cargo build --release
    WORKING_DIRECTORY "${EDPC_CORE_DIR}"
    COMMENT "Building Rust edpc_core static library..."
    BYPRODUCTS "${EDPC_CORE_LIB}"
)

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

if (Vulkan_INCLUDE_DIRS)
    target_include_directories(edpc PRIVATE ${Vulkan_INCLUDE_DIRS})
endif()

target_link_libraries(edpc PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    "${EDPC_CORE_LIB}"
    pthread
    dl
    m
)

if (Vulkan_LIBRARIES)
    target_link_libraries(edpc PRIVATE ${Vulkan_LIBRARIES})
else()
    target_link_libraries(edpc PRIVATE vulkan)
endif()
