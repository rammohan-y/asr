#!/bin/bash
# Installation script for Debian/Ubuntu dependencies
# This script installs all required dependencies to build the Vosk ASR WebSocket project

set -e

echo "Installing dependencies for Debian/Ubuntu..."

# Update package list
sudo apt-get update

# Install build essentials (gcc, g++, make)
sudo apt-get install -y \
    build-essential \
    cmake \
    pkg-config

# Install Boost libraries
sudo apt-get install -y \
    libboost-system-dev \
    libboost-thread-dev

# Install WebSocket++ (header-only library, usually available via package)
# If not available, it may need to be installed manually
sudo apt-get install -y \
    libwebsocketpp-dev || echo "Warning: libwebsocketpp-dev not found in repositories. You may need to install WebSocket++ manually."

# Install nlohmann/json (header-only library)
sudo apt-get install -y \
    nlohmann-json3-dev || \
    sudo apt-get install -y nlohmann-json-dev || \
    echo "Warning: nlohmann-json package not found. You may need to install it manually or use the single header file."

# Install additional dependencies
sudo apt-get install -y \
    libpthread-stubs0-dev

echo ""
echo "Dependencies installed successfully!"
echo ""
echo "Note: Vosk library must be installed separately using deploy_vosk_prebuilt.sh"
echo "CMake will look for Vosk at: \$HOME/vosk/vosk/libvosk.so"

