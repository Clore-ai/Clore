# CLORE Multi-Platform Build Setup Guide

This guide helps you set up the build environment for creating CLORE releases across multiple platforms from a Mac M1 system.

## Overview

The `release-multiplatform.sh` script can build CLORE for:

- **Linux**: x64 and ARM64 architectures
- **macOS**: Intel (x64) and Apple Silicon (ARM64)
- **Windows**: x64 architecture

## Prerequisites

### 1. Xcode Command Line Tools

```bash
xcode-select --install
```

### 2. Homebrew Package Manager

```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

### 3. Modern Bash (Required)

```bash
# Install Bash 5.x (macOS ships with old Bash 3.x)
brew install bash

# Optional: Set as default shell
echo "/usr/local/bin/bash" | sudo tee -a /etc/shells
chsh -s /usr/local/bin/bash
```

**Note**: The script requires Bash 4.0+ for associative arrays. If you prefer not to change your default shell, you can run the script with: `/usr/local/bin/bash release-multiplatform.sh`

### 4. Essential Build Tools

```bash
brew install autoconf automake libtool pkg-config
```

### 5. Cross-Compilation Tools

#### For Windows Builds

```bash
brew install mingw-w64
```

#### For Linux Builds (Optional - Advanced)

```bash
# Install cross-compilation toolchain for Linux
brew install gcc-cross-compile
```

#### For Package Creation

```bash
# For creating DEB packages
brew install dpkg

# For creating ZIP files
brew install zip

# For creating installers (optional)
brew install nsis
```

## Build Dependencies

### Core Dependencies

```bash
# Berkeley DB (for wallet functionality)
brew install berkeley-db4

# Boost libraries
brew install boost

# OpenSSL
brew install openssl

# ZeroMQ
brew install zeromq

# libevent
brew install libevent
```

### Additional Dependencies

```bash
# For better performance
brew install ccache

# For creating compressed archives
brew install xz

# For RPM package creation (if needed)
brew install rpm
```

## Usage

### Basic Build

```bash
./release-multiplatform.sh
```

### Available Commands

```bash
# Build all platforms (default)
./release-multiplatform.sh build

# Clean build directories
./release-multiplatform.sh clean

# Show help
./release-multiplatform.sh help
```

### Build Output

The script creates the following directory structure:

```
release-packages/
├── linux-amd64/
│   ├── clore-1.0.0-amd64.deb
│   ├── clore-1.0.0-linux-x64.tar.gz
│   └── clore-1.0.0-amd64.rpm (if rpmbuild available)
├── linux-arm64/
│   ├── clore-1.0.0-arm64.deb
│   └── clore-1.0.0-linux-arm64.tar.gz
├── macos/
│   ├── clore-1.0.0-macos-x64.tar.gz
│   └── clore-1.0.0-macos-arm64.tar.gz
├── windows/
│   └── clore-1.0.0-windows-x64.zip
└── SHA256SUMS (checksums for all packages)
```

## Supported Platforms

| Platform | Architecture  | Status          | Package Types      |
| -------- | ------------- | --------------- | ------------------ |
| Linux    | x64           | ✅ Full Support | DEB, RPM, TAR.GZ   |
| Linux    | ARM64         | ✅ Full Support | DEB, RPM, TAR.GZ   |
| macOS    | x64 (Intel)   | ✅ Full Support | APP, TAR.GZ        |
| macOS    | ARM64 (M1/M2) | ✅ Full Support | APP, TAR.GZ        |
| Windows  | x64           | ✅ Full Support | ZIP, EXE (planned) |

## Cross-Compilation Notes

### macOS to Linux

- Uses Clang with appropriate target triplets
- Requires static linking for portability
- Limited to userspace applications

### macOS to Windows

- Uses MinGW-w64 cross-compiler
- Produces native .exe files
- Includes all necessary DLLs

### Architecture Cross-Compilation

- Intel Mac → Apple Silicon: Uses `-arch arm64`
- Apple Silicon → Intel: Uses `-arch x86_64`
- Both produce universal binaries

## Troubleshooting

### Common Issues

#### Missing Dependencies

```bash
# Check if all tools are available
./release-multiplatform.sh help

# Install missing tools
brew install <missing-package>
```

#### Build Failures

```bash
# Clean and retry
./release-multiplatform.sh clean
./release-multiplatform.sh build

# Check logs in build directories
ls -la release-builds/
```

#### Cross-Compilation Issues

```bash
# Verify cross-compilation tools
which x86_64-w64-mingw32-gcc
which x86_64-w64-mingw32-g++

# Check environment
env | grep -E "(CC|CXX|CFLAGS|LDFLAGS)"
```

### Performance Tips

#### Use ccache for Faster Builds

```bash
# Install ccache
brew install ccache

# Configure
export CC="ccache clang"
export CXX="ccache clang++"
```

#### Parallel Builds

The script automatically detects CPU cores and uses parallel make jobs:

```bash
# Manual override if needed
export MAKEOPTS="-j8"
```

## Advanced Configuration

### Custom Build Options

Edit the script to modify build configurations:

```bash
# In configure_build() function
configure_args="--enable-static --disable-shared --your-custom-option"
```

### Adding New Platforms

To add support for new platforms:

1. Add entry to `TARGETS` array
2. Add case in `configure_build()` function
3. Add packaging logic in appropriate function

### Environment Variables

```bash
# Override default paths
export BUILD_DIR="/custom/build/path"
export RELEASE_DIR="/custom/release/path"

# Override version
export VERSION="1.0.1"
```

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
          brew install autoconf automake libtool pkg-config mingw-w64
      - name: Build releases
        run: ./release-multiplatform.sh
      - name: Upload artifacts
        uses: actions/upload-artifact@v3
        with:
          name: clore-releases
          path: release-packages/
```

## Support

For issues with the build system:

1. Check the [Issues](https://github.com/clore-ai/clore/issues) page
2. Verify all dependencies are installed
3. Check that you're using the latest version of the script
4. Provide full build logs when reporting issues

## Contributing

To improve the build system:

1. Fork the repository
2. Create a feature branch
3. Test on multiple platforms
4. Submit a pull request with detailed description

---

**Note**: This build system is optimized for Mac M1 but should work on other macOS systems with appropriate modifications to the cross-compilation setup.
