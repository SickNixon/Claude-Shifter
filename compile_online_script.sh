#!/bin/bash
# Script for compiling the PolyphonicPitchShifter plugin on online environments
# This is designed to work on GitHub Codespaces, Replit, or other similar environments

echo "Detecting environment..."

# Check if we're on GitHub Codespaces
if [ -n "$CODESPACES" ]; then
    echo "GitHub Codespaces detected!"
    
    # Install dependencies
    echo "Installing dependencies..."
    sudo apt-get update
    sudo apt-get install -y build-essential cmake librubberband-dev
    
    # Build using CMake
    echo "Building with CMake..."
    mkdir -p build
    cd build
    cmake ..
    make -j4
    
    echo "AudioUnit component built!"
    
# Check if we're on Replit
elif [ -n "$REPL_ID" ]; then
    echo "Replit environment detected!"
    
    # Install dependencies
    echo "Installing dependencies..."
    sudo apt-get update
    sudo apt-get install -y build-essential cmake librubberband-dev
    
    # Build using CMake
    echo "Building with CMake..."
    mkdir -p build
    cd build
    cmake ..
    make -j4
    
    echo "AudioUnit component built in Replit environment!"
    
# Default to generic Linux environment
else
    echo "Generic environment detected!"
    
    # Try to install dependencies
    echo "Attempting to install dependencies..."
    if command -v apt-get &> /dev/null; then
        sudo apt-get update
        sudo apt-get install -y build-essential cmake librubberband-dev
    elif command -v yum &> /dev/null; then
        sudo yum -y install gcc gcc-c++ make cmake rubberband-devel
    elif command -v brew &> /dev/null; then
        brew install cmake rubberband
    else
        echo "WARNING: Could not identify package manager. Please install build-essential, cmake and librubberband-dev manually."
    fi
    
    # Build using CMake
    echo "Building with CMake..."
    mkdir -p build
    cd build
    cmake ..
    make -j4
    
    echo "Build completed! Check for errors above."
fi

echo "If the build was successful, you'll need to copy the component to your Mac."
echo "For local testing, copy it to ~/Library/Audio/Plug-Ins/Components/"