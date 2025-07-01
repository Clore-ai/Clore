#!/usr/bin/env bash

# Simple CLORE Release Packager
# Packages existing binaries into release archives

VERSION=1.0.0
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RELEASE_DIR="$SCRIPT_DIR/release-packages"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

build_log() {
    echo -e "${GREEN}[$(date +'%H:%M:%S')] $1${NC}"
}

error() {
    echo -e "${RED}[ERROR] $1${NC}"
    exit 1
}

warning() {
    echo -e "${YELLOW}[WARNING] $1${NC}"
}

# Check if binaries exist
if [[ ! -f "src/clore_blockchaind" ]]; then
    error "clore_blockchaind binary not found in src/. Please build the project first."
fi

if [[ ! -f "src/clore-cli" ]]; then
    error "clore-cli binary not found in src/. Please build the project first."
fi

# Detect architecture
ARCH=$(uname -m)
case $ARCH in
    x86_64) ARCH_NAME="x64" ;;
    arm64) ARCH_NAME="arm64" ;;
    *) ARCH_NAME="unknown" ;;
esac

# Clean and create release directory
rm -rf "$RELEASE_DIR"
mkdir -p "$RELEASE_DIR"

build_log "Creating macOS release packages..."

# Create macOS package
MACOS_DIR="$RELEASE_DIR/clore-$VERSION-macos-$ARCH_NAME"
mkdir -p "$MACOS_DIR/bin"

# Copy binaries
cp "src/clore_blockchaind" "$MACOS_DIR/bin/"
cp "src/clore-cli" "$MACOS_DIR/bin/"

# Strip binaries
if command -v strip >/dev/null 2>&1; then
    build_log "Stripping binaries..."
    strip "$MACOS_DIR/bin/clore_blockchaind" 2>/dev/null || true
    strip "$MACOS_DIR/bin/clore-cli" 2>/dev/null || true
fi

# Create README
cat > "$MACOS_DIR/README.md" << 'EOF'
# CLORE Blockchain v1.0.0

This package contains the CLORE blockchain binaries for macOS.

## Included Files

- `bin/clore_blockchaind` - CLORE blockchain daemon
- `bin/clore-cli` - CLORE command line interface

## Quick Start

1. Make binaries executable (if needed):
   ```bash
   chmod +x bin/clore_blockchaind bin/clore-cli
   ```

2. Start the daemon:
   ```bash
   ./bin/clore_blockchaind
   ```

3. In another terminal, use the CLI:
   ```bash
   ./bin/clore-cli getinfo
   ```

## Installation

To install system-wide, copy the binaries to `/usr/local/bin/`:
```bash
sudo cp bin/* /usr/local/bin/
```

## Support

For support and documentation, visit: https://github.com/CloreAI/Clore-blockchain
EOF

# Create tar.gz archive
build_log "Creating tar.gz archive..."
cd "$RELEASE_DIR"
tar -czf "clore-$VERSION-macos-$ARCH_NAME.tar.gz" "clore-$VERSION-macos-$ARCH_NAME"

# Create ZIP archive
build_log "Creating ZIP archive..."
zip -r "clore-$VERSION-macos-$ARCH_NAME.zip" "clore-$VERSION-macos-$ARCH_NAME" >/dev/null

cd "$SCRIPT_DIR"

# Show results
build_log "Release packages created successfully!"
echo ""
echo -e "${BLUE}Created packages:${NC}"
ls -la "$RELEASE_DIR"/*.tar.gz "$RELEASE_DIR"/*.zip 2>/dev/null

echo ""
echo -e "${BLUE}Binary sizes:${NC}"
ls -lh "$MACOS_DIR/bin/"

echo ""
echo -e "${GREEN}✅ macOS release packaging completed!${NC}"
echo -e "${BLUE}📦 Packages available in: $RELEASE_DIR${NC}" 