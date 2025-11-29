#!/bin/bash

# Vosk ASR WebSocket Server Startup Script
# 
# Usage:
#   ./run_vosk_server.sh              # Normal mode (no audio recording)
#   ./run_vosk_server.sh --save-audio # Enable audio recording to WAV files
#   SAVE_AUDIO=true ./run_vosk_server.sh  # Alternative way to enable audio recording
#
# Environment Variables:
#   LOG_FOLDER       - Directory for log files (default: current directory)
#   RECORDING_FOLDER - Directory for audio recordings (default: current directory)
#   SAVE_AUDIO       - Enable audio recording (true/false)
#   VOSK_MODEL_PATH  - Path to Vosk model

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
EXECUTABLE="${BUILD_DIR}/vosk_asr_ws"

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Parse command line arguments
if [[ "$1" == "--save-audio" ]] || [[ "$1" == "-s" ]]; then
    export SAVE_AUDIO=true
fi

# Set default folders if not already set
export LOG_FOLDER=/var/log
export RECORDING_FOLDER=/home/rammohany/pocs/asr-recordings

echo -e "${GREEN}╔════════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║     Starting Vosk ASR WebSocket Server (Multi-Connect)    ║${NC}"
echo -e "${GREEN}╚════════════════════════════════════════════════════════════╝${NC}"
echo ""

# Check if executable exists
if [ ! -f "$EXECUTABLE" ]; then
    echo -e "${RED}❌ Error: Executable not found at: $EXECUTABLE${NC}"
    echo -e "${YELLOW}Run this first:${NC}"
    echo "  cd $SCRIPT_DIR/build"
    echo "  cmake .."
    echo "  make"
    exit 1
fi

# Check if port 9000 is already in use
if lsof -Pi :9000 -sTCP:LISTEN -t >/dev/null 2>&1 ; then
    echo -e "${YELLOW}⚠️  Port 9000 is already in use${NC}"
    echo ""
    echo "Finding process using port 9000:"
    lsof -i :9000
    echo ""
    read -p "Kill existing process and continue? (y/n) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        PID=$(lsof -ti:9000)
        echo -e "${YELLOW}Killing process $PID...${NC}"
        kill -9 $PID 2>/dev/null || true
        sleep 1
    else
        echo -e "${RED}Exiting...${NC}"
        exit 1
    fi
fi

# Set environment variables
export VOSK_MODEL_PATH="${VOSK_MODEL_PATH:-$HOME/vosk/models/vosk-model-small-en-us-0.15}"
export LD_LIBRARY_PATH="$HOME/vosk/vosk:$LD_LIBRARY_PATH"

# Check if model exists
if [ ! -d "$VOSK_MODEL_PATH" ]; then
    echo -e "${RED}❌ Error: Vosk model not found at: $VOSK_MODEL_PATH${NC}"
    echo -e "${YELLOW}Run this first:${NC}"
    echo "  cd ${SCRIPT_DIR}/.."
    echo "  ./deploy_vosk_prebuilt.sh"
    exit 1
fi

echo -e "${GREEN}✅ Configuration:${NC}"
echo "   Executable: $EXECUTABLE"
echo "   Model: $VOSK_MODEL_PATH"
echo "   Port: 9000"
echo "   Log Folder: $LOG_FOLDER"
if [ "$SAVE_AUDIO" == "true" ]; then
    echo -e "   Audio Recording: ${BLUE}ENABLED${NC}"
    echo "   Recording Folder: $RECORDING_FOLDER"
    echo "   Audio files will be saved as: <UUID>.wav"
else
    echo "   Audio Recording: DISABLED (use --save-audio to enable)"
fi
echo ""

# Start the server
echo -e "${GREEN}🚀 Starting server...${NC}"
echo ""

cd "$BUILD_DIR"
exec "$EXECUTABLE"

