cmake_minimum_required(VERSION 3.10)
project(PolyphonicPitchShifter)

set(CMAKE_CXX_STANDARD 14)

include_directories("${CMAKE_SOURCE_DIR}/AudioUnitSDK/include/AudioUnitSDK")

# Check for macOS
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64")
endif()

# Find RubberBand
find_library(RUBBERBAND_LIB rubberband REQUIRED)
include_directories(${RUBBERBAND_INCLUDE_DIR})

# Set the include directory for the AudioUnitSDK
set(AUDIOUNIT_SDK_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/AudioUnitSDK/include")
include_directories(${AUDIOUNIT_SDK_INCLUDE_DIR})

# macOS frameworks
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

if(APPLE)
    add_library(PolyphonicPitchShifter MODULE ${SOURCE_FILES})
    set_target_properties(PolyphonicPitchShifter PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION "component"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/Info.plist"
    )

    target_link_libraries(PolyphonicPitchShifter
        ${COREAUDIO_FRAMEWORK}
        ${AUDIOUNIT_FRAMEWORK}
        ${AUDIOTOOLBOX_FRAMEWORK}
        ${COREFOUNDATION_FRAMEWORK}
        ${RUBBERBAND_LIB}
    )
else()
    add_library(PolyphonicPitchShifter SHARED ${SOURCE_FILES})
    target_link_libraries(PolyphonicPitchShifter ${RUBBERBAND_LIB})
endif()

set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    # add_executable(TestPitchShifter tests/TestPitchShifter.cpp)
    # target_link_libraries(TestPitchShifter PolyphonicPitchShifter)
endif()

if(APPLE)
    install(TARGETS PolyphonicPitchShifter
            ... # Install details
    )
endif()
