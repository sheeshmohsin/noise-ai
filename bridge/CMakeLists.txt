add_library(noisebridge STATIC
    src/noise_bridge.cpp
)

target_include_directories(noisebridge
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_SOURCE_DIR}/engine/include
)

target_link_libraries(noisebridge
    PRIVATE noiseengine
)

noise_set_compile_options(noisebridge)
