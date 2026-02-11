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

# Determine OFX architecture directory name
if(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        set(OFX_ARCH "MacOS-ARM-64")
    else()
        set(OFX_ARCH "MacOS-x86-64")
    endif()
elseif(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(OFX_ARCH "Win64")
    else()
        set(OFX_ARCH "Win32")
    endif()
elseif(UNIX)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(OFX_ARCH "Linux-ARM-64")
    else()
        set(OFX_ARCH "Linux-x86-64")
    endif()
endif()

# Set bundle directory structure
set(OFX_BUNDLE_DIR "${CMAKE_BINARY_DIR}/PoissonBlend.ofx.bundle")
set(OFX_CONTENTS_DIR "${OFX_BUNDLE_DIR}/Contents")
set(OFX_ARCH_DIR "${OFX_CONTENTS_DIR}/${OFX_ARCH}")

# Set library output directory to build into bundle structure
set_target_properties(PoissonBlend PROPERTIES
    PREFIX ""
    SUFFIX ".ofx"
    LIBRARY_OUTPUT_DIRECTORY "${OFX_ARCH_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${OFX_ARCH_DIR}"
)

# Create Info.plist content
set(INFO_PLIST_PATH "${CMAKE_CURRENT_BINARY_DIR}/Info.plist.in")
if(APPLE)
    file(WRITE "${INFO_PLIST_PATH}" "<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">
<plist version=\"1.0\">
<dict>
    <key>CFBundleIdentifier</key>
    <string>com.example.PoissonBlend</string>
    <key>CFBundlePackageType</key>
    <string>BNDL</string>
    <key>CFBundleVersion</key>
    <string>${PROJECT_VERSION}</string>
    <key>CFBundleShortVersionString</key>
    <string>${PROJECT_VERSION}</string>
</dict>
</plist>
")
else()
    file(WRITE "${INFO_PLIST_PATH}" "<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">
<plist version=\"1.0\">
<dict>
    <key>CFBundleIdentifier</key>
    <string>com.example.PoissonBlend</string>
    <key>CFBundlePackageType</key>
    <string>BNDL</string>
</dict>
</plist>
")
endif()

# Copy Info.plist after build
add_custom_command(TARGET PoissonBlend POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${OFX_CONTENTS_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy "${INFO_PLIST_PATH}" "${OFX_CONTENTS_DIR}/Info.plist"
    COMMENT "Creating Info.plist in bundle"
)

# Installation rules
include(GNUInstallDirs)

# Set default OFX plugin installation path based on platform
# Users can override by setting -DOFX_PLUGIN_PATH=...
if(NOT DEFINED OFX_PLUGIN_PATH)
    if(APPLE)
        # macOS: Install to /Library/OFX/Plugins (system-wide)
        set(OFX_PLUGIN_PATH "/Library/OFX/Plugins" CACHE PATH "OFX plugin installation path")
    elseif(WIN32)
        # Windows: Install to C:/Program Files/Common Files/OFX/Plugins
        set(OFX_PLUGIN_PATH "C:/Program Files/Common Files/OFX/Plugins" CACHE PATH "OFX plugin installation path")
    else()
        # Linux: Install to ${CMAKE_INSTALL_PREFIX}/OFX/Plugins (defaults to /usr/local/OFX/Plugins)
        set(OFX_PLUGIN_PATH "${CMAKE_INSTALL_PREFIX}/OFX/Plugins" CACHE PATH "OFX plugin installation path")
    endif()
endif()

# Install the entire bundle directory
install(DIRECTORY "${OFX_BUNDLE_DIR}/"
    DESTINATION "${OFX_PLUGIN_PATH}/PoissonBlend.ofx.bundle"
    USE_SOURCE_PERMISSIONS
)
