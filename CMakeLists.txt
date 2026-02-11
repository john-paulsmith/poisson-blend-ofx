cmake_minimum_required(VERSION 3.15)
project(PoissonBlendOFX VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find OpenCV
find_package(OpenCV REQUIRED)

# Option to use user-provided OpenFX headers
set(OpenFX_ROOT "" CACHE PATH "Path to OpenFX headers (optional, will fetch if not provided)")

# Fetch OpenFX headers if not provided
if(NOT OpenFX_ROOT)
    include(FetchContent)
    FetchContent_Declare(
        openfx
        GIT_REPOSITORY https://github.com/AcademySoftwareFoundation/openfx.git
        GIT_TAG        OFX_Release_1_4_TAG
    )
    FetchContent_MakeAvailable(openfx)
    set(OpenFX_INCLUDE_DIR ${openfx_SOURCE_DIR}/include)
else()
    set(OpenFX_INCLUDE_DIR ${OpenFX_ROOT}/include)
endif()

# Create interface library for OpenFX headers
add_library(openfx_headers INTERFACE)
target_include_directories(openfx_headers INTERFACE ${OpenFX_INCLUDE_DIR})

# Build the plugin
add_library(PoissonBlend SHARED
    src/PoissonBlend.cpp
)

target_link_libraries(PoissonBlend
    PRIVATE
        openfx_headers
        ${OpenCV_LIBS}
)

target_include_directories(PoissonBlend
    PRIVATE
        ${OpenCV_INCLUDE_DIRS}
)

# Set proper extension for OFX plugin
set_target_properties(PoissonBlend PROPERTIES
    PREFIX ""
    SUFFIX ".ofx"
)

# Platform-specific settings
if(APPLE)
    set_target_properties(PoissonBlend PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION ofx
    )
elseif(WIN32)
    # Windows specific settings if needed
elseif(UNIX)
    # Linux specific settings if needed
endif()
