# CLORE Multi-Platform Build Setup Guide

This guide helps you set up the build environment for creating CLORE releases across multiple platforms from a Mac system.

## Overview

The `release-multiplatform.sh` script can build CLORE for:

- **Linux**: x64 and ARM64 architectures (requires Docker for cross-platform builds)
- **macOS**: Intel (x64) and Apple Silicon (ARM64) - native builds
- **Windows**: x64 architecture (cross-compilation via MinGW)

## Prerequisites

### 1. Xcode Command Line Tools

```bash
# Remove old tools and install latest version
sudo rm -rf /Library/Developer/CommandLineTools
sudo xcode-select --install
```

**Important**: Make sure you have the latest Command Line Tools compatible with your macOS version to avoid build issues.

### 2. Homebrew Package Manager

```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

### 3. Modern Bash (Required)

```bash
# Install Bash 5.x (macOS ships with old Bash 3.x)
brew install bash

# The script requires Bash 4.0+ for associative arrays
# Run with: /usr/local/bin/bash release-multiplatform.sh
```

**Note**: The script requires Bash 4.0+ for associative arrays. You don't need to change your default shell - just run the script with the newer Bash version.

### 4. Essential Build Tools

```bash
brew install autoconf automake libtool pkg-config
```

### 5. Cross-Compilation Tools

#### For Windows Builds

```bash
brew install mingw-w64
```

#### For Linux Builds (Docker Required)

```bash
# Install Docker Desktop for cross-platform Linux builds
# Download from: https://docker.com
```

**Note**: Linux builds from macOS require Docker. Without Docker, only native macOS builds are supported.

#### For Package Creation

```bash
# For creating ZIP files
brew install zip

# For creating DEB packages (optional)
brew install dpkg

# For creating installers (optional)
brew install nsis
```

## Build Dependencies

### Core Dependencies

```bash
# Boost libraries (required)
brew install boost

# OpenSSL (required)
brew install openssl

# ZeroMQ (optional, for ZMQ support)
brew install zeromq

# libevent (required)
brew install libevent
```

### Important: Berkeley DB Handling

**DO NOT install Berkeley DB via Homebrew** - the build script handles this automatically:

```bash
# DON'T DO THIS - it will cause linking conflicts:
# brew install berkeley-db4

# The script automatically builds Berkeley DB 4.8 locally in db4/
# and reuses it on subsequent builds for faster compilation
```

### Additional Dependencies

```bash
# For better performance (optional)
brew install ccache

# For creating compressed archives
brew install xz
```

## Build Optimizations

### Berkeley DB Optimization

The build script now includes smart Berkeley DB handling:

- **First build**: Downloads and compiles Berkeley DB 4.8 to `db4/` directory
- **Subsequent builds**: Reuses existing `db4/` installation, significantly speeding up builds
- **No conflicts**: Automatically overrides any Homebrew Berkeley DB detection

### Build Speed Improvements

- Preserves `db4/` directory between builds
- Uses parallel compilation (`-j$(nproc)`)
- Strips binaries for smaller package sizes

## Usage

### Basic Build

```bash
# Run with modern Bash
/usr/local/bin/bash ./release-multiplatform.sh
```

### Configuration

Edit the build targets in `release-multiplatform.sh`:

```bash
BUILD_LINUX_X64=true      # Requires Docker
BUILD_LINUX_ARM64=false   # Requires Docker  
BUILD_MACOS_X64=true      # Native on macOS
BUILD_MACOS_ARM64=true    # Native on macOS
BUILD_WINDOWS_X64=true    # Cross-compilation via MinGW
```

### Available Commands

```bash
# Build all enabled platforms (default)
/usr/local/bin/bash ./release-multiplatform.sh

# Clean build directories (preserves db4/)
./release-multiplatform.sh clean

# Show help
./release-multiplatform.sh help
```

### Build Output

The script creates the following directory structure:

```
release-packages/
├── linux-amd64/           # (if Docker available)
│   ├── clore-1.0.0-amd64.deb
│   ├── clore-1.0.0-linux-x64.tar.gz
│   └── clore-1.0.0-amd64.rpm (if rpmbuild available)
├── linux-arm64/           # (if Docker available)
│   ├── clore-1.0.0-arm64.deb
│   └── clore-1.0.0-linux-arm64.tar.gz
├── macos/
│   ├── clore-1.0.0-macos-x64.tar.gz
│   ├── clore-1.0.0-macos-x64.zip
│   └── clore-1.0.0-macos-arm64.tar.gz
├── windows/
│   └── clore-1.0.0-windows-x64.zip
└── SHA256SUMS (checksums for all packages)
```

## Supported Platforms

| Platform | Architecture  | Build Method    | Status          | Package Types      |
| -------- | ------------- | --------------- | --------------- | ------------------ |
| Linux    | x64           | Docker          | ✅ Full Support | DEB, RPM, TAR.GZ   |
| Linux    | ARM64         | Docker          | ✅ Full Support | DEB, RPM, TAR.GZ   |
| macOS    | x64 (Intel)   | Native          | ✅ Full Support | TAR.GZ, ZIP        |
| macOS    | ARM64 (M1/M2) | Native          | ✅ Full Support | TAR.GZ, ZIP        |
| Windows  | x64           | Cross-compile   | ✅ Full Support | ZIP, EXE (planned) |

## Cross-Compilation Notes

### macOS Native Builds

- **macOS x64**: Uses `--host=x86_64-apple-darwin` for Intel Macs
- **macOS ARM64**: Native compilation on Apple Silicon
- **Dependencies**: Uses Homebrew libraries with local Berkeley DB
- **No conflicts**: Automatically disables Homebrew Berkeley DB detection

### macOS to Windows

- Uses MinGW-w64 cross-compiler
- Produces native .exe files
- Uses depends system for static linking

### macOS to Linux (Docker)

- Requires Docker Desktop
- Uses containerized Linux environment
- Produces portable static binaries

## Troubleshooting

### Common Issues

#### Berkeley DB Linking Errors

```bash
# OLD ERROR (now fixed):
# ld: warning: directory not found for option '-L/usr/local/opt/berkeley-db/lib'

# SOLUTION: The script now automatically:
# 1. Disables Homebrew Berkeley DB detection in configure.ac
# 2. Forces use of local db4/ installation
# 3. Preserves db4/ between builds for speed
```

#### Build Failures

```bash
# Clean and retry (preserves db4/ for speed)
./release-multiplatform.sh clean
/usr/local/bin/bash ./release-multiplatform.sh

# Check if db4/ exists (should be preserved)
ls -la db4/

# Force rebuild Berkeley DB if needed
rm -rf db4/
/usr/local/bin/bash ./release-multiplatform.sh
```

#### Missing Dependencies

```bash
# Check Homebrew installation
brew doctor

# Install missing tools
brew install <missing-package>

# Update Command Line Tools if needed
sudo rm -rf /Library/Developer/CommandLineTools
sudo xcode-select --install
```

#### Docker Issues (Linux Builds)

```bash
# Check Docker is running
docker version

# Install Docker Desktop if missing
# Download from: https://docker.com

# Without Docker, disable Linux builds:
# Set BUILD_LINUX_X64=false and BUILD_LINUX_ARM64=false
```

### Performance Tips

#### Faster Subsequent Builds

- The script automatically preserves `db4/` directory
- Berkeley DB compilation is skipped on subsequent builds
- Typical build time reduction: 5-10 minutes per build

#### Use ccache for Even Faster Builds

```bash
# Install ccache
brew install ccache

# Configure (optional)
export CC="ccache clang"
export CXX="ccache clang++"
```

#### Parallel Builds

The script automatically detects CPU cores:

```bash
# Uses: make -j$(sysctl -n hw.ncpu)
# Manual override if needed:
export MAKEOPTS="-j8"
```

## Advanced Configuration

### Custom Build Options

Edit the script to modify build configurations:

```bash
# In configure_build() function
configure_args="--enable-static --disable-shared --your-custom-option"
```

### Berkeley DB Customization

```bash
# Force rebuild Berkeley DB
rm -rf db4/

# Use different Berkeley DB version (advanced)
# Modify contrib/install_db4.sh
```

### Environment Variables

```bash
# Override default paths
export BUILD_DIR="/custom/build/path"
export RELEASE_DIR="/custom/release/path"

# Override version
export VERSION="1.0.1"

# Force Berkeley DB rebuild
export FORCE_BDB_REBUILD=1
```

## Recent Improvements (Latest Version)

### ✅ Berkeley DB Optimization
- Automatic detection and reuse of existing `db4/` installation
- Eliminates Homebrew Berkeley DB conflicts
- Significantly faster subsequent builds

### ✅ Fixed Linking Issues
- Modified `configure.ac` to prevent Homebrew Berkeley DB detection
- Ensures consistent use of local Berkeley DB
- No more `/usr/local/opt/berkeley-db/lib` warnings

### ✅ Enhanced Build Process
- Improved error handling and logging
- Better cross-platform compatibility
- Streamlined package creation

### ✅ Optimized for Development
- Preserves build artifacts for faster iteration
- Smart dependency detection
- Reduced build times for testing

## Continuous Integration

### GitHub Actions Example

```yaml
name: Multi-Platform Build
on: [push, pull_request]
jobs:
  build:
    runs-on: macos-latest
    steps:
      - uses: actions/checkout@v3
      - name: Install dependencies
        run: |
          brew install autoconf automake libtool pkg-config mingw-w64 boost openssl libevent
      - name: Build releases
        run: /usr/local/bin/bash ./release-multiplatform.sh
      - name: Upload artifacts
        uses: actions/upload-artifact@v3
        with:
          name: clore-releases
          path: release-packages/
```

## Support

For issues with the build system:

1. Check the [Issues](https://github.com/clore-ai/clore/issues) page
2. Verify all dependencies are installed correctly
3. Ensure you're using Bash 4.0+ (`/usr/local/bin/bash`)
4. Check that Command Line Tools are up to date
5. Provide full build logs when reporting issues

## Contributing

To improve the build system:

1. Fork the repository
2. Create a feature branch
3. Test on multiple platforms if possible
4. Submit a pull request with detailed description

---

**Note**: This build system is optimized for macOS development with cross-platform support via Docker and MinGW. The Berkeley DB optimization makes it particularly efficient for iterative development and testing.
