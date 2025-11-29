#!/bin/bash

# Vosk ASR Deployment Script - Using Precompiled Binaries
# Much faster and easier than building from source

set -e

INSTALL_DIR="${INSTALL_DIR:-$HOME/vosk}"
MODEL_SIZE="${MODEL_SIZE:-small}"

echo "╔════════════════════════════════════════════════════════════╗"
echo "║        Vosk ASR Deployment (Precompiled Binaries)         ║"
echo "╠════════════════════════════════════════════════════════════╣"
echo "║  Installing Vosk for real-time telephony transcription    ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo ""

# Check dependencies
echo "[1/5] Checking dependencies..."
for cmd in wget unzip; do
    if ! command -v $cmd &> /dev/null; then
        echo "❌ Missing: $cmd"
        echo "Install with: sudo apt-get install -y wget unzip"
        exit 1
    fi
done
echo "✅ All dependencies found"

# Create installation directory
echo ""
echo "[2/5] Creating installation directory..."
mkdir -p "$INSTALL_DIR"
cd "$INSTALL_DIR"
echo "✅ Directory: $INSTALL_DIR"

# Download precompiled Vosk library for Linux
echo ""
echo "[3/5] Downloading precompiled Vosk library..."

VOSK_VERSION="0.3.45"
VOSK_ARCHIVE="vosk-linux-x86_64-${VOSK_VERSION}.zip"
VOSK_URL="https://github.com/alphacep/vosk-api/releases/download/v${VOSK_VERSION}/${VOSK_ARCHIVE}"

if [ -d "vosk" ]; then
    echo "⚠️  vosk directory exists, skipping..."
else
    echo "Downloading from: $VOSK_URL"
    wget --continue "$VOSK_URL"
    unzip -q "$VOSK_ARCHIVE"
    rm "$VOSK_ARCHIVE"
    
    # Rename to standard directory name
    mv vosk-linux-x86_64-${VOSK_VERSION} vosk 2>/dev/null || mv vosk-* vosk 2>/dev/null || true
fi

if [ ! -f "vosk/libvosk.so" ]; then
    echo "❌ libvosk.so not found after extraction"
    exit 1
fi

echo "✅ Vosk library downloaded"

# Download model
echo ""
echo "[4/5] Downloading Vosk model..."

mkdir -p models

case "$MODEL_SIZE" in
    small)
        MODEL_NAME="vosk-model-small-en-us-0.15"
        MODEL_URL="https://alphacephei.com/vosk/models/${MODEL_NAME}.zip"
        MODEL_SIZE_INFO="40MB"
        ;;
    medium)
        MODEL_NAME="vosk-model-en-us-0.22"
        MODEL_URL="https://alphacephei.com/vosk/models/${MODEL_NAME}.zip"
        MODEL_SIZE_INFO="1.8GB"
        ;;
    *)
        echo "❌ Unknown model size: $MODEL_SIZE (use: small or medium)"
        exit 1
        ;;
esac

echo "Downloading $MODEL_NAME (~${MODEL_SIZE_INFO})..."

if [ -d "models/$MODEL_NAME" ]; then
    echo "⚠️  Model already exists, skipping..."
else
    cd models
    wget --continue "$MODEL_URL"
    unzip -q "${MODEL_NAME}.zip"
    rm "${MODEL_NAME}.zip"
    cd ..
fi

echo "✅ Model downloaded"

# Create convenience scripts
echo ""
echo "[5/5] Creating convenience scripts..."

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
ls -lh vosk/libvosk.so 2>/dev/null || echo "  Not found"
echo ""
echo "Headers:"
ls -lh vosk/vosk_api.h 2>/dev/null || echo "  Not found"
echo ""
echo "Models:"
ls -d models/*/ 2>/dev/null || echo "  No models found"
echo ""
echo "To use in your project:"
echo "  Include: $(pwd)/vosk/vosk_api.h"
echo "  Link:    $(pwd)/vosk/libvosk.so"
echo "  Model:   $(pwd)/models/<model-name>"
EOF
chmod +x "$INSTALL_DIR/info.sh"

# Activate script
cat > "$INSTALL_DIR/activate.sh" << EOF
#!/bin/bash
# Source this file to set up Vosk environment
export VOSK_HOME="$INSTALL_DIR"
export VOSK_MODEL_PATH="$INSTALL_DIR/models/$MODEL_NAME"
export LD_LIBRARY_PATH="$INSTALL_DIR/vosk:\$LD_LIBRARY_PATH"
export PKG_CONFIG_PATH="$INSTALL_DIR/vosk:\$PKG_CONFIG_PATH"

echo "Vosk environment activated:"
echo "  VOSK_HOME: \$VOSK_HOME"
echo "  VOSK_MODEL_PATH: \$VOSK_MODEL_PATH"
EOF
chmod +x "$INSTALL_DIR/activate.sh"

# Summary
echo ""
echo "╔════════════════════════════════════════════════════════════╗"
echo "║                 ✅ Installation Complete!                  ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo ""
echo "📦 Installed components:"
echo "   - Vosk C++ library: vosk/libvosk.so"
echo "   - Model: models/$MODEL_NAME"
echo "   - Headers: vosk/vosk_api.h"
echo ""
echo "🔧 Integration paths (for CMakeLists.txt):"
echo "   Library: $INSTALL_DIR/vosk/libvosk.so"
echo "   Include: $INSTALL_DIR/vosk"
echo "   Model:   $INSTALL_DIR/models/$MODEL_NAME"
echo ""
echo "📝 Helper scripts created:"
echo "   ./info.sh     - Show installation details"
echo "   ./activate.sh - Set environment variables"
echo ""
echo "🚀 Next step: Update ASR CMakeLists.txt"
echo ""

