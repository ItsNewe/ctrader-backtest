#!/bin/bash
# Build script for Linux/macOS

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
GENERATED_DIR="$SCRIPT_DIR/generated"

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${YELLOW}=== cTrader C++ Backtest Engine Build ===${NC}"
echo ""

# Create generated directory if it doesn't exist
if [ ! -d "$GENERATED_DIR" ]; then
  echo -e "${YELLOW}Creating generated/ directory...${NC}"
  mkdir -p "$GENERATED_DIR"
fi

# Download proto files if missing (placeholder)
echo -e "${YELLOW}Checking for proto files...${NC}"
if [ ! -f "$GENERATED_DIR/ctrader.proto" ]; then
  echo -e "${YELLOW}Proto files not found. Skipping download (TODO: implement).${NC}"
  # TODO: Download from cTrader API docs
  # Example: wget -O "$GENERATED_DIR/ctrader.proto" "https://..."
fi

# Generate protobuf C++ files (if proto files exist)
if [ -f "$GENERATED_DIR/ctrader.proto" ]; then
  echo -e "${YELLOW}Generating Protobuf C++ files...${NC}"
  protoc --cpp_out="$GENERATED_DIR" --proto_path="$GENERATED_DIR" \
         "$GENERATED_DIR/ctrader.proto" || {
    echo -e "${RED}Failed to generate Protobuf files${NC}"
    exit 1
  }
  echo -e "${GREEN}✓ Protobuf files generated${NC}"
else
  echo -e "${YELLOW}Skipping Protobuf generation (no .proto files found)${NC}"
fi

# Create build directory
if [ ! -d "$BUILD_DIR" ]; then
  echo -e "${YELLOW}Creating build directory...${NC}"
  mkdir -p "$BUILD_DIR"
fi

# Run CMake configure
echo -e "${YELLOW}Running CMake configure...${NC}"
cd "$BUILD_DIR"
cmake -DCMAKE_BUILD_TYPE=Release .. || {
  echo -e "${RED}CMake configure failed${NC}"
  exit 1
}
echo -e "${GREEN}✓ CMake configured${NC}"

# Build with parallel jobs
echo -e "${YELLOW}Building project...${NC}"
NUM_JOBS=$(nproc || echo 4)
cmake --build . --config Release -- -j "$NUM_JOBS" || {
  echo -e "${RED}Build failed${NC}"
  exit 1
}

echo ""
echo -e "${GREEN}=== Build Successful ===${NC}"
echo -e "${GREEN}Executable: $BUILD_DIR/backtest${NC}"
echo ""
echo "Next steps:"
echo "  1. Run: $BUILD_DIR/backtest"
echo "  2. Debug: gdb $BUILD_DIR/backtest"
echo "  3. Rebuild: cd $SCRIPT_DIR && ./build.sh"
echo ""
