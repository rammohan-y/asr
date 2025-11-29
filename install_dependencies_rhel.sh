#!/bin/bash
# Installation script for RedHat/CentOS/Oracle Linux dependencies
# This script installs all required dependencies to build the Vosk ASR WebSocket project
# Supports: RedHat Enterprise Linux, CentOS, Oracle Linux, Fedora

set -e

# Detect distribution
if [ -f /etc/oracle-release ]; then
    DISTRO="Oracle Linux"
    OL_VERSION=$(cat /etc/oracle-release | grep -oE '[0-9]+' | head -1)
    echo "Detected: $DISTRO version $OL_VERSION"
elif [ -f /etc/redhat-release ]; then
    DISTRO=$(cat /etc/redhat-release | sed 's/ release.*//')
    echo "Detected: $DISTRO"
else
    DISTRO="RHEL-based"
    echo "Warning: Could not detect distribution, assuming RHEL-based"
fi

echo "Installing dependencies for $DISTRO..."

# Check if running as root or with sudo
if [ "$EUID" -ne 0 ]; then 
    echo "This script requires sudo privileges. Please run with sudo or as root."
    exit 1
fi

# Install EPEL repository (for additional packages)
if [ -f /etc/oracle-release ] && [ -n "$OL_VERSION" ]; then
    # Oracle Linux: try oracle-epel-release first, then fallback to regular epel-release
    if [ "$OL_VERSION" -ge 8 ]; then
        dnf install -y oracle-epel-release-el${OL_VERSION} || \
        dnf install -y epel-release || true
    else
        yum install -y oracle-epel-release-el${OL_VERSION} || \
        yum install -y epel-release || true
    fi
else
    # RedHat/CentOS: use regular epel-release
    yum install -y epel-release || dnf install -y epel-release || true
fi

# Install build essentials
if command -v dnf &> /dev/null; then
    # RHEL 8+/Fedora/Oracle Linux 8+ uses dnf
    dnf groupinstall -y "Development Tools" || \
    dnf install -y gcc gcc-c++ make cmake pkgconfig
    
    # Install Boost libraries
    dnf install -y boost-devel boost-system
    
    # Install nlohmann/json
    dnf install -y nlohmann_json-devel || \
    echo "Warning: nlohmann_json-devel not found. You may need to install nlohmann/json manually."
    
    # WebSocket++ is typically header-only and may need manual installation
    echo "Note: WebSocket++ may need to be installed manually from source or via a third-party repository."
    
else
    # RHEL 7 and older use yum
    yum groupinstall -y "Development Tools" || \
    yum install -y gcc gcc-c++ make cmake pkgconfig
    
    # Install Boost libraries
    yum install -y boost-devel boost-system
    
    # Install nlohmann/json (may not be available in older repos)
    yum install -y nlohmann_json-devel || \
    echo "Warning: nlohmann_json-devel not found. You may need to install nlohmann/json manually."
    
    # WebSocket++ is typically header-only and may need manual installation
    echo "Note: WebSocket++ may need to be installed manually from source or via a third-party repository."
fi

echo ""
echo "Dependencies installed successfully!"
echo ""
echo "Note: Vosk library must be installed separately using deploy_vosk_prebuilt.sh"
echo "CMake will look for Vosk at: \$HOME/vosk/vosk/libvosk.so"
echo ""
echo "If nlohmann/json or WebSocket++ are not available via package manager,"
echo "you may need to install them manually:"
echo "  - nlohmann/json: Download from https://github.com/nlohmann/json"
echo "  - WebSocket++: Download from https://github.com/zaphoyd/websocketpp"

