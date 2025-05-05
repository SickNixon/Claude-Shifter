cmake_minimum_required(VERSION 3.10)
project(PolyphonicPitchShifter)

# Use C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# macOS universal build (intel + silicon) if desired
if(APPLE)
  set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64")
endif()

# Find RubberBand library
find_library(RUBBERBAND_LIB rubberband REQUIRED)

# Include the AudioUnitSDK headers cloned into your project
set(AUDIOUNIT_SDK_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/AudioUnitSDK/include")
# --- FIX: add this include path so all "#include <AudioUnitSDK/...>" resolve
include_directories(${AUDIOUNIT_SDK_INCLUDE_DIR})

# macOS frameworks
if(APPLE)
  find_library(COREAUDIO_FRAMEWORK   CoreAudio    REQUIRED)
  find_library(AUDIOUNIT_FRAMEWORK   AudioUnit    REQUIRED)
  find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox REQUIRED)
  find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
endif()

# Source files
set(SOURCE_FILES
  PolyphonicPitchShifter.cpp
  PolyphonicPitchShifterVersion.h
)

# Build the AudioUnit plugin as a .component bundle on macOS
if(APPLE)
  add_library(PolyphonicPitchShifter MODULE ${SOURCE_FILES})
  set_target_properties(PolyphonicPitchShifter PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "component"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/Info.plist"
  )
  target_link_libraries(PolyphonicPitchShifter
    PRIVATE
      ${COREAUDIO_FRAMEWORK}
      ${AUDIOUNIT_FRAMEWORK}
      ${AUDIOTOOLBOX_FRAMEWORK}
      ${COREFOUNDATION_FRAMEWORK}
      ${RUBBERBAND_LIB}
  )
else()
  # Fallback for Linux (shared lib)
  add_library(PolyphonicPitchShifter SHARED ${SOURCE_FILES})
  target_link_libraries(PolyphonicPitchShifter PRIVATE ${RUBBERBAND_LIB})
endif()

# Release optimizations
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

# Installation
if(APPLE)
  install(TARGETS PolyphonicPitchShifter
    DESTINATION ~/Library/Audio/Plug-Ins/Components
  )
else()
  install(TARGETS PolyphonicPitchShifter
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib
  )
endif()
