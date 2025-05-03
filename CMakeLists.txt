cmake_minimum_required(VERSION 3.10)
project(PolyphonicPitchShifter)

# Set the C++ standard to 17 to support modern features used in AudioUnitSDK
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON) # Ensure the specified standard is used

# Check for macOS
if(APPLE)
    # Set architectures for macOS (x86_64 for Intel, arm64 for Apple Silicon)
    set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64")
endif()

# Find RubberBand library
# This assumes RubberBand is installed in a standard location or findable via CMake
find_library(RUBBERBAND_LIB rubberband REQUIRED)
# Include RubberBand headers
# CMake should find include directories associated with the found library
# find_package(RubberBand CONFIG REQUIRED) # Alternative if RubberBand provides a CMake config file
# include_directories(${RUBBERBAND_INCLUDE_DIRS}) # Use this if using find_package

# Set the include directory for the AudioUnitSDK
# This path is relative to the project's source directory
set(AUDIOUNIT_SDK_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/AudioUnitSDK/include")
# Add the AudioUnitSDK include directory to the project's include paths
include_directories(${AUDIOUNIT_SDK_INCLUDE_DIR})

# macOS frameworks required for AudioUnits
if(APPLE)
    find_framework(CoreAudio REQUIRED)
    find_framework(AudioUnit REQUIRED)
    find_framework(AudioToolbox REQUIRED REQUIRED)
    find_framework(CoreFoundation REQUIRED)
endif()

# Define the source files for the plugin
set(SOURCE_FILES
    PolyphonicPitchShifter.cpp
    PolyphonicPitchShifterVersion.h
)

# Build configuration specifically for macOS
if(APPLE)
    # Add the plugin as a MODULE library (for AudioUnit components)
    add_library(PolyphonicPitchShifter MODULE ${SOURCE_FILES})

    # Set properties for the macOS bundle
    set_target_properties(PolyphonicPitchShifter PROPERTIES
        BUNDLE TRUE # Treat as a macOS bundle
        BUNDLE_EXTENSION "component" # Set the bundle extension to .component
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/Info.plist" # Specify the Info.plist file
    )

    # Link against required frameworks and the RubberBand library
    target_link_libraries(PolyphonicPitchShifter
        CoreAudio
        AudioUnit
        AudioToolbox
        CoreFoundation
        ${RUBBERBAND_LIB} # Link the found RubberBand library
    )
else()
    # Generic shared library build for non-macOS (if applicable)
    add_library(PolyphonicPitchShifter SHARED ${SOURCE_FILES})
    target_link_libraries(PolyphonicPitchShifter ${RUBBERBAND_LIB})
endif()

# Set release build flags (optimize for size/speed)
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

# Example of how to add an executable for testing (currently commented out)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    # add_executable(TestPitchShifter tests/TestPitchShifter.cpp)
    # target_link_libraries(TestPitchShifter PolyphonicPitchShifter)
endif()

# Installation rules for macOS
if(APPLE)
    install(TARGETS PolyphonicPitchShifter
        DESTINATION "Library/Audio/Plug-Ins/Components" # Standard location for AU components
        BUNDLE DESTINATION "${CMAKE_INSTALL_PREFIX}/Library/Audio/Plug-Ins/Components" # Ensure bundle structure is preserved
    )
endif()

# Add resource file if needed (often handled by the bundle structure on macOS)
# configure_file(${CMAKE_SOURCE_DIR}/PolyphonicPitchShifter.r ${CMAKE_BINARY_DIR}/PolyphonicPitchShifter.r COPYONLY)
