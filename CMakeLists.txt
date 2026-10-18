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

if (WIN32)
    if (MSVC)
        set(EDPC_CORE_LIB "${EDPC_CORE_DIR}/target/release/edpc_core.lib")
    else()
        set(EDPC_CORE_LIB "${EDPC_CORE_DIR}/target/release/libedpc_core.a")
    endif()
    set(PLATFORM_LIBS ws2_32 userenv bcrypt ntdll)
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

if (TARGET Vulkan::Vulkan)
    # Modern CMake imported target handles include dirs and libs
elseif (Vulkan_INCLUDE_DIRS)
    target_include_directories(edpc PRIVATE ${Vulkan_INCLUDE_DIRS})
elseif (DEFINED ENV{VULKAN_SDK})
    target_include_directories(edpc PRIVATE "$ENV{VULKAN_SDK}/Include")
endif()

target_link_libraries(edpc PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    "${EDPC_CORE_LIB}"
    ${PLATFORM_LIBS}
)

if (TARGET Vulkan::Vulkan)
    target_link_libraries(edpc PRIVATE Vulkan::Vulkan)
elseif (Vulkan_LIBRARIES)
    target_link_libraries(edpc PRIVATE ${Vulkan_LIBRARIES})
else()
    if (WIN32)
        if (DEFINED ENV{VULKAN_SDK})
            target_link_directories(edpc PRIVATE "$ENV{VULKAN_SDK}/Lib")
        endif()
        target_link_libraries(edpc PRIVATE vulkan-1)
    else()
        target_link_libraries(edpc PRIVATE vulkan)
    endif()
endif()

install(TARGETS edpc RUNTIME DESTINATION bin)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/edpc.desktop" DESTINATION share/applications)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/edpc.png" DESTINATION share/icons/hicolor/256x256/apps)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/edpc.png" DESTINATION share/pixmaps)
