#!/bin/bash

# Vosk ASR Deployment Script for Real-Time Telephony Transcription
# This script installs Vosk and downloads optimized models for 8kHz telephony

set -e  # Exit on error

INSTALL_DIR="${INSTALL_DIR:-$HOME/vosk}"
MODEL_SIZE="${MODEL_SIZE:-small}"  # small, medium, or large

echo "╔════════════════════════════════════════════════════════════╗"
echo "║           Vosk ASR Deployment Script                      ║"
echo "╠════════════════════════════════════════════════════════════╣"
echo "║  Installing Vosk for real-time telephony transcription    ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo ""

# Check dependencies
echo "[1/6] Checking dependencies..."
MISSING_DEPS=()

for cmd in git make gcc g++ wget unzip; do
    if ! command -v $cmd &> /dev/null; then
        MISSING_DEPS+=("$cmd")
    fi
done

if [ ${#MISSING_DEPS[@]} -ne 0 ]; then
    echo "❌ Missing dependencies: ${MISSING_DEPS[*]}"
    echo ""
    echo "Install them with:"
    echo "  sudo apt-get update"
    echo "  sudo apt-get install -y build-essential git wget unzip"
    exit 1
fi

echo "✅ All dependencies found"

# Create installation directory
echo ""
echo "[2/6] Creating installation directory..."
mkdir -p "$INSTALL_DIR"
cd "$INSTALL_DIR"
echo "✅ Directory: $INSTALL_DIR"

# Clone and build Vosk API
echo ""
echo "[3/6] Cloning Vosk API..."
if [ -d "vosk-api" ]; then
    echo "⚠️  vosk-api directory exists, pulling latest..."
    cd vosk-api
    git pull
    cd ..
else
    git clone https://github.com/alphacep/vosk-api.git
fi

echo ""
echo "[4/6] Building Vosk C++ library..."
cd vosk-api/src
if [ -f "Makefile" ] && [ -f "libvosk.so" ]; then
    echo "⚠️  Cleaning previous build..."
    make clean || true
fi

make -j$(nproc)

if [ ! -f "libvosk.so" ]; then
    echo "❌ Build failed - libvosk.so not found"
    exit 1
fi

echo "✅ Vosk library built successfully"

# Download model
echo ""
echo "[5/6] Downloading Vosk model..."

cd "$INSTALL_DIR"
mkdir -p models

case "$MODEL_SIZE" in
    small)
        MODEL_NAME="vosk-model-small-en-us-0.15"
        MODEL_URL="https://alphacephei.com/vosk/models/${MODEL_NAME}.zip"
        MODEL_SIZE_MB="40"
        ;;
    medium)
        MODEL_NAME="vosk-model-en-us-0.22"
        MODEL_URL="https://alphacephei.com/vosk/models/${MODEL_NAME}.zip"
        MODEL_SIZE_MB="1.8G"
        ;;
    large)
        MODEL_NAME="vosk-model-en-us-0.22-lgraph"
        MODEL_URL="https://alphacephei.com/vosk/models/${MODEL_NAME}.zip"
        MODEL_SIZE_MB="128"
        ;;
    *)
        echo "❌ Unknown model size: $MODEL_SIZE"
        echo "   Use: small, medium, or large"
        exit 1
        ;;
esac

echo "Downloading $MODEL_NAME (~${MODEL_SIZE_MB}MB)..."

if [ -d "models/$MODEL_NAME" ]; then
    echo "⚠️  Model already exists: models/$MODEL_NAME"
    echo "   Skipping download..."
else
    cd models
    wget --continue "$MODEL_URL"
    unzip -q "${MODEL_NAME}.zip"
    rm "${MODEL_NAME}.zip"
    cd ..
    echo "✅ Model downloaded and extracted"
fi

# Create convenience scripts
echo ""
echo "[6/6] Creating convenience scripts..."

# Info script
cat > "$INSTALL_DIR/info.sh" << 'EOF'
#!/bin/bash
echo "╔═══════════════════════════════════════════════════════════╗"
echo "║                 Vosk Installation Info                   ║"
echo "╚═══════════════════════════════════════════════════════════╝"
echo ""
echo "Installation directory: $(pwd)"
echo ""
echo "Library:"
ls -lh vosk-api/src/libvosk.so
echo ""
echo "Headers:"
ls -lh vosk-api/src/vosk_api.h
echo ""
echo "Models:"
ls -d models/*/ 2>/dev/null || echo "  No models found"
echo ""
echo "To use in your project:"
echo "  Include: $(pwd)/vosk-api/src/vosk_api.h"
echo "  Link:    $(pwd)/vosk-api/src/libvosk.so"
echo "  Model:   $(pwd)/models/<model-name>"
EOF
chmod +x "$INSTALL_DIR/info.sh"

# Test script
cat > "$INSTALL_DIR/test_vosk.sh" << EOF
#!/bin/bash
cd "\$(dirname "\$0")"

MODEL_PATH="\$(ls -d models/*/ | head -1)"
if [ -z "\$MODEL_PATH" ]; then
    echo "❌ No model found"
    exit 1
fi

echo "Testing Vosk with model: \$MODEL_PATH"
echo ""

# Create simple test program
cat > test_vosk.c << 'TESTEOF'
#include <stdio.h>
#include "vosk-api/src/vosk_api.h"

int main() {
    printf("Vosk version: %s\n", vosk_get_version());
    return 0;
}
TESTEOF

gcc test_vosk.c -I. -L./vosk-api/src -lvosk -o test_vosk -Wl,-rpath,\$(pwd)/vosk-api/src
./test_vosk
rm test_vosk test_vosk.c

echo ""
echo "✅ Vosk is working!"
EOF
chmod +x "$INSTALL_DIR/test_vosk.sh"

# Activate script for environment
cat > "$INSTALL_DIR/activate.sh" << EOF
#!/bin/bash
# Source this file to set up Vosk environment
export VOSK_HOME="$INSTALL_DIR"
export VOSK_MODEL_PATH="$INSTALL_DIR/models/$MODEL_NAME"
export LD_LIBRARY_PATH="$INSTALL_DIR/vosk-api/src:\$LD_LIBRARY_PATH"

echo "Vosk environment activated:"
echo "  VOSK_HOME: \$VOSK_HOME"
echo "  VOSK_MODEL_PATH: \$VOSK_MODEL_PATH"
echo "  LD_LIBRARY_PATH: \$LD_LIBRARY_PATH"
EOF
chmod +x "$INSTALL_DIR/activate.sh"

# Summary
echo ""
echo "╔════════════════════════════════════════════════════════════╗"
echo "║                 ✅ Installation Complete!                  ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo ""
echo "📦 Installed components:"
echo "   - Vosk C++ library: vosk-api/src/libvosk.so"
echo "   - Model: models/$MODEL_NAME"
echo "   - Headers: vosk-api/src/vosk_api.h"
echo ""
echo "🔧 Integration paths:"
echo "   Library: $INSTALL_DIR/vosk-api/src/libvosk.so"
echo "   Include: $INSTALL_DIR/vosk-api/src"
echo "   Model:   $INSTALL_DIR/models/$MODEL_NAME"
echo ""
echo "📝 Helper scripts created:"
echo "   ./info.sh       - Show installation details"
echo "   ./test_vosk.sh  - Test Vosk installation"
echo "   ./activate.sh   - Set environment variables"
echo ""
echo "🚀 Next steps:"
echo "   1. Test installation: ./test_vosk.sh"
echo "   2. Update your CMakeLists.txt to link Vosk"
echo "   3. Convert your ASR code to use Vosk API"
echo ""
echo "📚 Vosk advantages over Whisper:"
echo "   ✅ Real-time streaming (50-100ms latency)"
echo "   ✅ Optimized for 8kHz telephony"
echo "   ✅ Partial results as user speaks"
echo "   ✅ Lower CPU usage"
echo "   ✅ Smaller models"
echo ""

# Run test if requested
if [ "$1" == "--test" ]; then
    echo "Running test..."
    ./test_vosk.sh
fi

