cmake_minimum_required(VERSION 3.10)
project(PolyphonicPitchShifter)

set(CMAKE_CXX_STANDARD 14)

# Check if we're on macOS
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64")
endif()

# Find RubberBand library
find_library(RUBBERBAND_LIB rubberband REQUIRED)
include_directories(${RUBBERBAND_INCLUDE_DIR})

# Include system headers
include_directories(/usr/local/include)

# macOS specific frameworks
if(APPLE)
    find_library(COREAUDIO_FRAMEWORK CoreAudio REQUIRED)
    find_library(AUDIOUNIT_FRAMEWORK AudioUnit REQUIRED)
    find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox REQUIRED)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
endif()

# Source files
set(SOURCE_FILES
    PolyphonicPitchShifter.cpp
    PolyphonicPitchShifterVersion.h
)

# For macOS, build as AU component
if(APPLE)
    add_library(PolyphonicPitchShifter MODULE ${SOURCE_FILES})
    set_target_properties(PolyphonicPitchShifter PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION "component"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/Info.plist"
    )

    # Link macOS frameworks
    target_link_libraries(PolyphonicPitchShifter
        ${COREAUDIO_FRAMEWORK}
        ${AUDIOUNIT_FRAMEWORK}
        ${AUDIOTOOLBOX_FRAMEWORK}
        ${COREFOUNDATION_FRAMEWORK}
        ${RUBBERBAND_LIB}
    )
else()
    # For non-macOS, build a shared library (mostly for testing/development)
    add_library(PolyphonicPitchShifter SHARED ${SOURCE_FILES})
    target_link_libraries(PolyphonicPitchShifter ${RUBBERBAND_LIB})
endif()

# Add compiler optimizations for release builds
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

# Include testing only in debug mode
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    # Add test executable if needed
    # add_executable(TestPitchShifter tests/TestPitchShifter.cpp)
    # target_link_libraries(TestPitchShifter PolyphonicPitchShifter)
endif()

# Installation rules
if(APPLE)
    install(TARGETS PolyphonicPitchShifter 
            DESTINATION ~/Library/Audio/Plug-Ins/Components)
else()
    install(TARGETS PolyphonicPitchShifter
            DESTINATION ${CMAKE_INSTALL_PREFIX}/lib)
endif()