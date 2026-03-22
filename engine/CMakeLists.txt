find_package(ONNXRuntime REQUIRED)

add_library(noiseengine STATIC
    src/engine.cpp
    src/deepfilter.cpp
    src/ring_buffer.cpp
    src/shared_ring_buffer.cpp
    src/inference.cpp
    src/resampler.cpp
    src/stft.cpp
    src/post_process.cpp
)

target_include_directories(noiseengine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(noiseengine
    PUBLIC rnnoise onnxruntime::onnxruntime
)

noise_set_compile_options(noiseengine)

# Tests
add_subdirectory(tests)
