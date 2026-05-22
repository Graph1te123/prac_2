cmake_minimum_required(VERSION 3.20)
project(TrayApp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(TrayApp WIN32
    src/main.cpp
    src/app.rc
)

target_include_directories(TrayApp PRIVATE src)

target_link_libraries(TrayApp PRIVATE
    shell32
    user32
    gdi32
    comctl32
    advapi32
)

# UTF-8 source encoding
target_compile_options(TrayApp PRIVATE /utf-8)

# Enable manifest for visual styles
target_link_options(TrayApp PRIVATE
    /MANIFEST:EMBED
    /MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/src/app.manifest
)

# ---- Windows Service --------------------------------------------------------
add_executable(TrayAppService
    src/service.cpp
)

target_compile_options(TrayAppService PRIVATE /utf-8)

target_link_libraries(TrayAppService PRIVATE
    advapi32
    wtsapi32
    userenv
)