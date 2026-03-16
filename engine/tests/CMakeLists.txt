enable_testing()

add_executable(test_ring_buffer test_ring_buffer.cpp)

target_link_libraries(test_ring_buffer
    PRIVATE noiseengine
)

noise_set_compile_options(test_ring_buffer)

add_test(NAME ring_buffer_test COMMAND test_ring_buffer)
