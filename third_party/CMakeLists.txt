include(FetchContent)

FetchContent_Declare(
    rnnoise
    GIT_REPOSITORY https://github.com/xiph/rnnoise.git
    GIT_TAG v0.1.1
)
FetchContent_MakeAvailable(rnnoise)

# Build RNNoise as a static library from specific source files.
# RNNoise uses autotools natively, so we manually list the C sources.
# Using v0.1.1 which bundles the model weights in rnn_data.c.
add_library(rnnoise STATIC
    ${rnnoise_SOURCE_DIR}/src/denoise.c
    ${rnnoise_SOURCE_DIR}/src/rnn.c
    ${rnnoise_SOURCE_DIR}/src/rnn_data.c
    ${rnnoise_SOURCE_DIR}/src/pitch.c
    ${rnnoise_SOURCE_DIR}/src/kiss_fft.c
    ${rnnoise_SOURCE_DIR}/src/celt_lpc.c
)

target_include_directories(rnnoise
    PUBLIC
        ${rnnoise_SOURCE_DIR}/include
    PRIVATE
        ${rnnoise_SOURCE_DIR}/src
)

# RNNoise needs these defines to compile correctly outside autotools
target_compile_definitions(rnnoise PRIVATE
    COMPILE_OPUS=1
    HAVE_STDLIB_H=1
    HAVE_STRING_H=1
)

# Suppress warnings in third-party code
target_compile_options(rnnoise PRIVATE -w)
