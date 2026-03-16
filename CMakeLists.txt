cmake_minimum_required(VERSION 3.26)

# Allow older CMake version requirements in fetched dependencies (e.g., libASPL)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# Read version from VERSION file
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" PROJECT_VERSION_STRING)
string(STRIP "${PROJECT_VERSION_STRING}" PROJECT_VERSION_STRING)

project(NoiseAI
    VERSION ${PROJECT_VERSION_STRING}
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Module path for custom cmake helpers
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

include(utils)

enable_testing()

add_subdirectory(engine)
add_subdirectory(bridge)
add_subdirectory(driver)
