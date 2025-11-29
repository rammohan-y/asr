#!/bin/bash
# Script to manually install nlohmann/json and WebSocket++ from GitHub
# These are header-only libraries that can be installed locally

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
THIRD_PARTY_DIR="${SCRIPT_DIR}/lib/third_party"

echo "╔════════════════════════════════════════════════════════════╗"
echo "║      Installing Third-Party Header-Only Libraries         ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo ""

# Create third_party directory
mkdir -p "$THIRD_PARTY_DIR"
cd "$THIRD_PARTY_DIR"

# Install nlohmann/json
echo "[1/2] Installing nlohmann/json..."
if [ -d "json" ]; then
    echo "⚠️  nlohmann/json already exists, updating..."
    cd json
    git pull || echo "Warning: Could not update, using existing version"
    cd ..
else
    echo "📥 Cloning nlohmann/json from GitHub..."
    git clone --depth 1 https://github.com/nlohmann/json.git
    echo "✅ nlohmann/json installed"
fi

# Install WebSocket++
echo ""
echo "[2/2] Installing WebSocket++..."
if [ -d "websocketpp" ]; then
    echo "⚠️  WebSocket++ already exists, updating..."
    cd websocketpp
    git pull || echo "Warning: Could not update, using existing version"
    cd ..
else
    echo "📥 Cloning WebSocket++ from GitHub..."
    git clone --depth 1 https://github.com/zaphoyd/websocketpp.git
    echo "✅ WebSocket++ installed"
fi

echo ""
echo "╔════════════════════════════════════════════════════════════╗"
echo "║                 ✅ Installation Complete!                  ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo ""
echo "📦 Installed libraries:"
echo "   - nlohmann/json:  $THIRD_PARTY_DIR/json"
echo "   - WebSocket++:    $THIRD_PARTY_DIR/websocketpp"
echo ""
echo "📝 Next steps:"
echo "   1. Run cmake to configure your project"
echo "   2. CMakeLists.txt will automatically find these libraries"
echo ""

