cmake_minimum_required(VERSION 3.26)

include(FetchContent)

FetchContent_Declare(
    libASPL
    GIT_REPOSITORY https://github.com/gavv/libASPL.git
    GIT_TAG v3.1.1
)
FetchContent_MakeAvailable(libASPL)

add_library(NoiseAIDriver MODULE
    src/plugin.cpp
    src/device.cpp
    src/io_handler.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/shared_ring_buffer.cpp
)

target_link_libraries(NoiseAIDriver PRIVATE
    libASPL
    "-framework CoreFoundation"
    "-framework CoreAudio"
)

set_target_properties(NoiseAIDriver PROPERTIES
    OUTPUT_NAME "NoiseAI"
    BUNDLE TRUE
    BUNDLE_EXTENSION "driver"
    PREFIX ""
    SUFFIX ""
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

target_include_directories(NoiseAIDriver PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/engine/include
)
