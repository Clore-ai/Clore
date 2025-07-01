#!/usr/bin/env bash

# Multi-Platform CLORE Release Builder
# Supports: Linux (x64, ARM64), macOS (x64, ARM64), Windows (x64)
# Works on macOS (Intel/Apple Silicon) and Linux (Ubuntu 22.04+)

# =============================================================================
# PLATFORM BUILD CONFIGURATION - Set to true/false to enable/disable builds
# =============================================================================
#
# USAGE EXAMPLES:
#   Build everything:           Leave all flags as 'true'
#   macOS only:                 Set BUILD_MACOS_*=true, others=false
#   Linux servers only:         Set BUILD_LINUX_X64=true, others=false
#   Apple Silicon + containers: Set BUILD_MACOS_ARM64=true + BUILD_LINUX_ARM64=true
#
# NOTES:
#   - macOS builds can ONLY be performed on macOS hosts (not on Linux)
#   - macOS x64 cross-compilation from Apple Silicon may fail due to Boost issues
#   - For best Intel Mac builds, run this script on an actual Intel Mac
#   - Linux builds use Docker for cross-platform support (automatic fallback to native)
#   - Windows builds require MinGW-w64 (brew install mingw-w64 or apt-get install gcc-mingw-w64)
#   - Failed builds will be skipped automatically and won't stop other builds
#
# =============================================================================

BUILD_LINUX_X64=false     # Build for Linux x86_64 (Intel/AMD servers) - Set to true for Ubuntu 22.04
BUILD_LINUX_ARM64=false    # Build for Linux ARM64 (AWS Graviton, Apple Silicon containers)
BUILD_MACOS_X64=true      # Build for Intel Macs (cross-compilation issues)
BUILD_MACOS_ARM64=false    # Build for Apple Silicon Macs (M1/M2/M3) - only on macOS hosts
BUILD_WINDOWS_X64=false    # Build for Windows x64 - Set to true for Ubuntu with MinGW

# =============================================================================

# Check bash version - we need 4.0+ for associative arrays
if [ "${BASH_VERSION%%.*}" -lt 4 ]; then
    echo "Error: This script requires Bash 4.0 or later for associative arrays."
    echo "Current Bash version: $BASH_VERSION"
    echo ""
    echo "On macOS, install newer bash with:"
    echo "  brew install bash"
    echo "Then run with: /usr/local/bin/bash $0"
    exit 1
fi

VERSION=1.0.0
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/release-builds"
RELEASE_DIR="$SCRIPT_DIR/release-packages"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Platform detection
OS_TYPE=$(uname -s)
ARCH_TYPE=$(uname -m)

# Function definitions (must be defined before use)
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

# Build configuration - using associative array (requires Bash 4.0+)
declare -A TARGETS=()

# Populate targets based on user configuration and host capabilities
[[ "$BUILD_LINUX_X64" == "true" ]] && TARGETS["linux-x64"]="x86_64-linux-gnu"
[[ "$BUILD_LINUX_ARM64" == "true" ]] && TARGETS["linux-arm64"]="aarch64-linux-gnu"
[[ "$BUILD_WINDOWS_X64" == "true" ]] && TARGETS["windows-x64"]="x86_64-w64-mingw32"

# Strict platform validation - panic if incompatible builds are enabled
if [[ "$OS_TYPE" != "Darwin" ]]; then
    # macOS builds can ONLY be built on macOS hosts
    if [[ "$BUILD_MACOS_X64" == "true" ]]; then
        error "BUILD_MACOS_X64=true but running on $OS_TYPE host. macOS builds can ONLY be built on macOS hosts. Set BUILD_MACOS_X64=false or run this script on a macOS machine."
    fi
    if [[ "$BUILD_MACOS_ARM64" == "true" ]]; then
        error "BUILD_MACOS_ARM64=true but running on $OS_TYPE host. macOS builds can ONLY be built on macOS hosts. Set BUILD_MACOS_ARM64=false or run this script on a macOS machine."
    fi
fi

# Populate targets based on validated configuration
if [[ "$OS_TYPE" == "Darwin" ]]; then
    [[ "$BUILD_MACOS_X64" == "true" ]] && TARGETS["macos-x64"]="x86_64-apple-darwin"
    [[ "$BUILD_MACOS_ARM64" == "true" ]] && TARGETS["macos-arm64"]="arm64-apple-darwin"
fi

# Function definitions moved to top of script

# Validate that at least one target is enabled
if [[ ${#TARGETS[@]} -eq 0 ]]; then
    error "No build targets enabled! Please set at least one BUILD_*=true flag at the top of this script."
fi

# Strict dependency validation - panic early if critical tools are missing
case "$OS_TYPE" in
    "Linux")
        if [[ "$BUILD_WINDOWS_X64" == "true" ]] && ! command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
            error "BUILD_WINDOWS_X64=true but MinGW-w64 cross-compiler not found on Linux host. Install with: sudo apt-get install gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64"
        fi
        ;;
    "Darwin")
        # Architecture-specific validation for macOS hosts
        if [[ "$ARCH_TYPE" == "arm64" ]]; then
            # Apple Silicon specific warnings
            if [[ "$BUILD_LINUX_X64" == "true" ]]; then
                warning "Apple Silicon → Linux x64: Cross-architecture build via Docker emulation will be very slow and may fail"
                warning "Consider building on a Linux x64 machine or disabling BUILD_LINUX_X64"
            fi
            if [[ "$BUILD_WINDOWS_X64" == "true" ]]; then
                error "Apple Silicon → Windows x64: Cross-architecture MinGW compilation is not reliable. Set BUILD_WINDOWS_X64=false or use an Intel Mac/Windows machine"
            fi
            if [[ "$BUILD_MACOS_X64" == "true" ]]; then
                warning "Apple Silicon → Intel Mac: Cross-compilation has known Boost issues and may fail"
            fi
        elif [[ "$ARCH_TYPE" == "x86_64" ]]; then
            # Intel Mac specific warnings  
            if [[ "$BUILD_LINUX_ARM64" == "true" ]]; then
                warning "Intel Mac → Linux ARM64: Cross-architecture build via Docker emulation will be slow"
            fi
            if [[ "$BUILD_MACOS_ARM64" == "true" ]]; then
                warning "Intel Mac → Apple Silicon: Cross-compilation may have compatibility issues"
            fi
        fi
        
        if [[ "$BUILD_WINDOWS_X64" == "true" ]] && ! command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
            error "BUILD_WINDOWS_X64=true but MinGW-w64 cross-compiler not found on macOS host. Install with: brew install mingw-w64"
        fi
        ;;
    *)
        error "Unsupported build host OS: $OS_TYPE. This script only supports macOS (Darwin) and Linux hosts."
        ;;
esac

echo -e "${BLUE}=== CLORE Multi-Platform Release Builder v${VERSION} ===${NC}"
echo -e "${BLUE}Build Environment: ${OS_TYPE} ${ARCH_TYPE}${NC}"
echo -e "${BLUE}Bash Version: ${BASH_VERSION}${NC}"

# Show supported build targets for current host
case "$OS_TYPE" in
    "Darwin")
        echo -e "${BLUE}Host Capabilities: Linux (Docker), macOS (native), Windows (MinGW)${NC}"
        ;;
    "Linux")
        echo -e "${BLUE}Host Capabilities: Linux (native), Windows (MinGW) - macOS builds not supported${NC}"
        ;;
    *)
        echo -e "${YELLOW}Host Capabilities: Unknown - some builds may fail${NC}"
        ;;
esac
echo ""
echo -e "${BLUE}Enabled Build Targets:${NC}"
if [[ ${#TARGETS[@]} -gt 0 ]]; then
    for target in "${!TARGETS[@]}"; do
        echo -e "  ${GREEN}✅ $target${NC}"
    done
else
    echo -e "  ${RED}❌ No targets enabled${NC}"
fi
echo ""

# Verify we're on a supported build environment
case "$OS_TYPE" in
    "Darwin")
        build_log "macOS build environment detected"
        ;;
    "Linux")
        build_log "Linux build environment detected"
        # Check if we're on Ubuntu/Debian
        if ! command -v apt-get >/dev/null 2>&1; then
            warning "This script is optimized for Ubuntu/Debian Linux. Other distributions may require manual dependency installation."
        fi
        ;;
    *)
        warning "Unsupported build environment: ${OS_TYPE}. This script is optimized for macOS and Linux (Ubuntu 22.04+)."
        warning "Some features may not work correctly."
        ;;
esac

# Clean previous builds
echo -e "${BLUE}Cleaning previous builds...${NC}"
rm -rf "$BUILD_DIR" "$RELEASE_DIR"
mkdir -p "$BUILD_DIR" "$RELEASE_DIR"

# Functions moved to top of script

# Check build dependencies
check_dependencies() {
    build_log "Checking build dependencies..."
    
    # Check for basic build tools
    command -v make >/dev/null 2>&1 || error "make is required but not installed"
    command -v autoconf >/dev/null 2>&1 || error "autoconf is required but not installed"
    command -v automake >/dev/null 2>&1 || error "automake is required but not installed"
    command -v git >/dev/null 2>&1 || error "git is required but not installed"
    
    # Platform-specific dependency checks
    case "$OS_TYPE" in
        "Darwin")
            check_macos_dependencies
            ;;
        "Linux")
            check_linux_dependencies
            ;;
        *)
            warning "Unknown OS type: $OS_TYPE. Dependency checking may be incomplete."
            ;;
    esac
    
    # Check Docker for cross-platform Linux builds
    check_docker_availability
}

# Check macOS-specific dependencies
check_macos_dependencies() {
    build_log "Checking macOS dependencies..."
    
    # Check for Xcode command line tools
    xcode-select -p >/dev/null 2>&1 || error "Xcode command line tools required. Install with: xcode-select --install"
    
    # Check for required system dependencies
    local missing_deps=()
    
    # Check for essential libraries that should be available on macOS
    if ! command -v pkg-config >/dev/null 2>&1; then
        missing_deps+=("pkg-config")
    fi
    
    # Check for Homebrew (recommended for dependencies)
    if command -v brew >/dev/null 2>&1; then
        build_log "Homebrew detected - checking for system dependencies..."
        
        # Check for boost libraries
        if ! brew list boost >/dev/null 2>&1; then
            warning "Boost not found via Homebrew. Install with: brew install boost"
            missing_deps+=("boost")
        fi
        
        # Check for openssl
        if ! brew list openssl@3 >/dev/null 2>&1 && ! brew list openssl@1.1 >/dev/null 2>&1; then
            warning "OpenSSL not found via Homebrew. Install with: brew install openssl"
            missing_deps+=("openssl")
        fi
        
        # Check for libevent
        if ! brew list libevent >/dev/null 2>&1; then
            warning "libevent not found via Homebrew. Install with: brew install libevent"
            missing_deps+=("libevent")
        fi
        
    else
        warning "Homebrew not found. Install from https://brew.sh for easier dependency management."
        warning "Required dependencies: boost, openssl, libevent, pkg-config"
    fi
    
    if [[ ${#missing_deps[@]} -gt 0 ]]; then
        warning "Missing dependencies: ${missing_deps[*]}"
        warning "Install with: brew install ${missing_deps[*]}"
        warning "Build may fail without these dependencies"
    fi
}

# Check Linux-specific dependencies
check_linux_dependencies() {
    build_log "Checking Linux dependencies..."
    
    # Check if we can use sudo
    local can_sudo=false
    if command -v sudo >/dev/null 2>&1; then
        if sudo -n true 2>/dev/null; then
            can_sudo=true
        else
            warning "Some commands may require sudo privileges. You may be prompted for your password."
            if sudo -v; then
                can_sudo=true
            fi
        fi
    fi

    # Update package lists if we have sudo
    if [[ "$can_sudo" == "true" ]]; then
        build_log "Updating package lists..."
        sudo apt-get update
    fi
    
    # Essential build tools
    local essential_packages=(
        "build-essential"
        "libtool"
        "autotools-dev"
        "automake"
        "pkg-config"
        "libssl-dev"
        "libevent-dev"
        "bsdmainutils"
        "python3"
        "libboost-system-dev"
        "libboost-filesystem-dev"
        "libboost-chrono-dev"
        "libboost-program-options-dev"
        "libboost-test-dev"
        "libboost-thread-dev"
        "libzmq3-dev"
        "wget"
        "patch"
        "git"
    )

    # Package creation tools
    local packaging_packages=(
        "dpkg-dev"
        "rpm"
    )

    # Windows cross-compilation tools (only if Windows build is enabled)
    local windows_packages=()
    if [[ "$BUILD_WINDOWS_X64" == "true" ]]; then
        windows_packages+=(
            "gcc-mingw-w64-x86-64"
            "g++-mingw-w64-x86-64"
        )
    fi

    # Combine all required packages
    local required_packages=("${essential_packages[@]}")
    required_packages+=("${packaging_packages[@]}")
    required_packages+=("${windows_packages[@]}")

    # Check for missing packages
    local missing_packages=()
    for package in "${required_packages[@]}"; do
        if ! dpkg -l | grep -q "^ii.*$package" 2>/dev/null; then
            missing_packages+=("$package")
        fi
    done

    # Install missing packages if we have sudo
    if [[ ${#missing_packages[@]} -gt 0 ]]; then
        echo -e "${YELLOW}Missing required packages: ${missing_packages[*]}${NC}"
        
        if [[ "$can_sudo" == "true" ]]; then
            build_log "Installing missing packages..."
            if ! sudo apt-get install -y "${missing_packages[@]}"; then
                error "Failed to install some packages. Please install them manually: ${missing_packages[*]}"
            fi
        else
            warning "Cannot automatically install missing packages without sudo privileges"
            warning "Please run: sudo apt-get install ${missing_packages[*]}"
            warning "Then run this script again"
            exit 1
        fi
    fi

    # Verify MinGW installation if Windows build is enabled
    if [[ "$BUILD_WINDOWS_X64" == "true" ]]; then
        if ! command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
            error "MinGW-w64 installation failed or not properly configured"
            error "Please install manually: sudo apt-get install gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64"
            exit 1
        else
            build_log "MinGW-w64 detected - Windows cross-compilation available"
        fi
    fi

    build_log "Linux dependency check completed"
}

# Check Docker availability for containerized builds
check_docker_availability() {
    # Check for Docker for Linux builds (required when cross-compiling from different architectures)
    if ! command -v docker >/dev/null 2>&1; then
        warning "Docker not found. Cross-platform Linux builds will be limited."
        case "$OS_TYPE" in
            "Darwin")
                warning "Install Docker Desktop from https://docker.com"
                ;;
            "Linux")
                warning "Install Docker with: sudo apt-get install docker.io"
                warning "Add user to docker group: sudo usermod -aG docker \$USER"
                ;;
        esac
        
        # Only disable Docker builds if host architecture doesn't match target
        if [[ "$ARCH_TYPE" != "x86_64" ]] && [[ -n "${TARGETS[linux-x64]}" ]]; then
            warning "Docker required for cross-platform x64 build from $ARCH_TYPE host"
            unset TARGETS["linux-x64"]
        fi
        if [[ "$ARCH_TYPE" != "aarch64" && "$ARCH_TYPE" != "arm64" ]] && [[ -n "${TARGETS[linux-arm64]}" ]]; then
            warning "Docker required for cross-platform ARM64 build from $ARCH_TYPE host"
            unset TARGETS["linux-arm64"]
        fi
    elif ! docker info >/dev/null 2>&1; then
        warning "Docker is not running."
        case "$OS_TYPE" in
            "Darwin")
                warning "Please start Docker Desktop"
                ;;
            "Linux")
                warning "Start Docker with: sudo systemctl start docker"
                ;;
        esac
        # Apply same logic as above for disabled targets
        if [[ "$ARCH_TYPE" != "x86_64" ]] && [[ -n "${TARGETS[linux-x64]}" ]]; then
            unset TARGETS["linux-x64"]
        fi
        if [[ "$ARCH_TYPE" != "aarch64" && "$ARCH_TYPE" != "arm64" ]] && [[ -n "${TARGETS[linux-arm64]}" ]]; then
            unset TARGETS["linux-arm64"]
        fi
    else
        build_log "Docker detected and running - containerized builds available"
    fi
}

# Build Linux using Docker containers
build_linux_docker() {
    local target=$1
    local target_dir="$BUILD_DIR/$target"
    local arch=""
    local docker_arch=""
    
    case $target in
        "linux-x64") 
            arch="x64"
            docker_image="ubuntu:22.04"
            ;;
        "linux-arm64") 
            arch="arm64"
            docker_image="arm64v8/ubuntu:22.04"
            ;;
        *)
            error "Unsupported Linux target: $target"
            return 1
            ;;
    esac
    
    build_log "Building $target using Docker ($docker_image)..."
    
    # Check if Docker is running
    if ! docker info >/dev/null 2>&1; then
        error "Docker is not running. Please start Docker Desktop."
        return 1
    fi
    
    mkdir -p "$target_dir"
    
    # Create Dockerfile for the build
    cat > "$target_dir/Dockerfile" << EOF
FROM ${docker_image}

# Install build dependencies
RUN apt-get update && apt-get install -y \\
    build-essential \\
    libtool \\
    autotools-dev \\
    automake \\
    pkg-config \\
    libssl-dev \\
    libevent-dev \\
    bsdmainutils \\
    python3 \\
    libboost-system-dev \\
    libboost-filesystem-dev \\
    libboost-chrono-dev \\
    libboost-program-options-dev \\
    libboost-test-dev \\
    libboost-thread-dev \\
    libzmq3-dev \\
    wget \\
    patch \\
    git \\
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY . .

# Build Berkeley DB 4.8 from source
RUN mkdir -p /build/db4 && \\
    wget -c 'http://download.oracle.com/berkeley-db/db-4.8.30.NC.tar.gz' && \\
    echo '12edc0df75bf9abd7f82f821795bcee50f42cb2e5f76a6a281b85732798364ef  db-4.8.30.NC.tar.gz' | sha256sum -c && \\
    tar -xzvf db-4.8.30.NC.tar.gz && \\
    chmod a+w ./db-4.8.30.NC/dbinc/atomic.h && \\
    cp ./depends/patches/atomic.h db-4.8.30.NC/dbinc/ && \\
    # Update config.guess and config.sub for modern architectures \\
    cp ./depends/config.guess db-4.8.30.NC/dist/config.guess && \\
    cp ./depends/config.sub db-4.8.30.NC/dist/config.sub && \\
    chmod +x db-4.8.30.NC/dist/config.guess db-4.8.30.NC/dist/config.sub && \\
    cd db-4.8.30.NC/build_unix/ && \\
    ../dist/configure --enable-cxx --disable-shared --with-pic --prefix=/build/db4 --with-mutex=POSIX/pthreads && \\
    make -j\$(nproc) && \\
    ranlib libdb-4.8.a && \\
    ranlib libdb_cxx-4.8.a && \\
    make install && \\
    cd /build && \\
    rm -f db-4.8.30.NC.tar.gz && \\
    rm -rf db-4.8.30.NC

# Build project using native system dependencies (no depends system needed)
RUN export BDB_PREFIX="/build/db4" && \\
    ./autogen.sh && \\
    ./configure \\
      --enable-cxx \\
      --disable-shared \\
      --disable-tests \\
      --disable-gui-tests \\
      --with-pic \\
      --without-bench \\
      --without-miniupnpc \\
      --enable-zmq \\
      --enable-wallet \\
      BDB_LIBS="-L\${BDB_PREFIX}/lib -ldb_cxx-4.8" \\
      BDB_CFLAGS="-I\${BDB_PREFIX}/include" \\
      LDFLAGS="-L\${BDB_PREFIX}/lib -L/usr/lib" \\
      CPPFLAGS="-I\${BDB_PREFIX}/include" \\
      CXXFLAGS="-fPIC -I\${BDB_PREFIX}/include -DBOOST_SPIRIT_THREADSAFE -DHAVE_BUILD_INFO -D__STDC_FORMAT_MACROS" \\
      LIBS="-ldb_cxx-4.8 -lboost_system -lzmq" && \\
    make -C src/wallet -j\$(nproc) && \\
    make -j\$(nproc)

# Copy binaries to output directory
RUN mkdir -p /output && \\
    cp src/clore_blockchaind /output/ && \\
    cp src/clore-cli /output/ && \\
    strip /output/*
EOF
    
    # Build using Docker
    build_log "Running Docker build for $target..."
    
    # Set platform flag for cross-compilation
    local platform_flag=""
    case $target in
        "linux-x64") platform_flag="--platform linux/amd64" ;;
        "linux-arm64") platform_flag="--platform linux/arm64" ;;
    esac
    
    docker build $platform_flag -t "clore-build-$arch" -f "$target_dir/Dockerfile" . || {
        error "Docker build failed for $target"
        return 1
    }
    
    # Extract binaries from container
    build_log "Extracting binaries for $target..."
    mkdir -p "$target_dir/bin"
    
    # Run container and copy files
    container_id=$(docker create "clore-build-$arch")
    docker cp "$container_id:/output/." "$target_dir/bin/"
    docker rm "$container_id"
    
    # Clean up Docker image  
    docker rmi "clore-build-$arch" >/dev/null 2>&1 || true
    
    build_log "Docker build completed for $target"
    return 0
}

# Build Berkeley DB 4.8 for native builds
build_bdb4() {
    local target_dir="$1"
    local bdb_prefix="$target_dir/db4"
    
    if [[ -f "$bdb_prefix/lib/libdb_cxx-4.8.a" ]]; then
        build_log "Berkeley DB 4.8 already built for this target"
        return 0
    fi
    
    build_log "Building Berkeley DB 4.8..."
    
    cd "$SCRIPT_DIR"
    
    # Clean any potentially corrupted download files
    rm -f db-4.8.30.NC.tar.gz
    rm -rf db-4.8.30.NC
    rm -rf "$bdb_prefix"
    
    # Create BDB prefix directory
    mkdir -p "$bdb_prefix"
    
    # Fetch the source and verify that it is not tampered with
    build_log "Downloading Berkeley DB 4.8.30..."
    wget -c 'http://download.oracle.com/berkeley-db/db-4.8.30.NC.tar.gz' || error "Failed to download Berkeley DB"
    
    build_log "Verifying checksum..."
    if command -v sha256sum >/dev/null 2>&1; then
        echo '12edc0df75bf9abd7f82f821795bcee50f42cb2e5f76a6a281b85732798364ef  db-4.8.30.NC.tar.gz' | sha256sum -c || error "Berkeley DB checksum verification failed"
    elif command -v shasum >/dev/null 2>&1; then
        echo '12edc0df75bf9abd7f82f821795bcee50f42cb2e5f76a6a281b85732798364ef  db-4.8.30.NC.tar.gz' | shasum -a 256 -c || error "Berkeley DB checksum verification failed"
    else
        warning "No checksum tool found, skipping verification"
    fi
    
    build_log "Extracting Berkeley DB..."
    tar -xzvf db-4.8.30.NC.tar.gz || error "Failed to extract Berkeley DB"
    
    # Copy patch to fix error in db4
    build_log "Applying atomic.h patch..."
    if [[ ! -f "./depends/patches/atomic.h" ]]; then
        error "Required patch file ./depends/patches/atomic.h not found"
    fi
    chmod a+w ./db-4.8.30.NC/dbinc/atomic.h
    cp ./depends/patches/atomic.h db-4.8.30.NC/dbinc/ || error "Failed to apply atomic.h patch"
    
    # Build the library and install to our prefix
    build_log "Configuring Berkeley DB build..."
    cd db-4.8.30.NC/build_unix/
    
    # Platform-specific configuration for Berkeley DB
    # Enable C++ but use old-style headers to avoid atomic conflicts
    local db_configure_args="--enable-cxx --disable-shared --with-pic --prefix=$bdb_prefix"
    
    case "$OS_TYPE" in
        "Darwin")
            # macOS-specific configuration
            build_log "Applying macOS-specific Berkeley DB configuration..."
            # Force pthread mutex implementation on macOS to avoid detection issues
            db_configure_args="$db_configure_args --with-mutex=POSIX/pthreads"
            # Additional flags for modern macOS compatibility and to avoid C++11 atomic conflicts
            export CPPFLAGS="-DMUTEX_THREAD_ONLY -D__STDC_NO_ATOMICS__ $CPPFLAGS"
            # Use C++98 with compatibility for old-style headers
            export CXX="g++ -std=c++98"
            export CXXFLAGS="-std=c++98 -fno-strict-aliasing -Wno-deprecated $CXXFLAGS"
            # Create compatibility for old C++ headers that Berkeley DB expects
            mkdir -p "compat-headers"
            echo '#include <iostream>' > "compat-headers/iostream.h"
            echo '#include <fstream>' > "compat-headers/fstream.h"
            echo '#include <iomanip>' > "compat-headers/iomanip.h"
            export CPPFLAGS="-I$(pwd)/compat-headers $CPPFLAGS"
            ;;
        "Linux")
            # Linux-specific configuration
            db_configure_args="$db_configure_args --with-mutex=POSIX/pthreads"
            ;;
    esac
    
    ../dist/configure $db_configure_args || error "Berkeley DB configure failed"
    
    build_log "Building Berkeley DB (this may take a while)..."
    make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4) || error "Berkeley DB build failed"
    
    # Fix archive indexing issue
    if [[ -f "libdb-4.8.a" ]]; then
        ranlib libdb-4.8.a || true
    fi
    if [[ -f "libdb_cxx-4.8.a" ]]; then
        ranlib libdb_cxx-4.8.a || true
    fi
    
    build_log "Installing Berkeley DB..."
    make install || error "Berkeley DB install failed"
    
    # Clean up source files
    cd "$SCRIPT_DIR"
    rm -f db-4.8.30.NC.tar.gz
    rm -rf db-4.8.30.NC
    
    build_log "Berkeley DB 4.8 build completed successfully"
    
    # Reset compiler environment after Berkeley DB build to allow C++11 for CLORE
    case "$OS_TYPE" in
        "Darwin")
            # Reset to default C++ compiler settings for macOS
            unset CXX
            unset CXXFLAGS
            unset CPPFLAGS
            export CXX="g++"
            ;;
        "Linux")
            # Reset to default C++ compiler settings for Linux
            unset CXX
            unset CXXFLAGS
            unset CPPFLAGS
            export CXX="g++"
            ;;
    esac
}

# Configure build for specific target
configure_build() {
    local target=$1
    local target_dir="$BUILD_DIR/$target"
    
    build_log "Configuring build for $target..."
    mkdir -p "$target_dir"
    
    cd "$SCRIPT_DIR"
    
    # Clean any previous configuration
    make clean >/dev/null 2>&1 || true
    make distclean >/dev/null 2>&1 || true
    
    # Run autogen if needed
    if [[ ! -f configure ]]; then
        build_log "Running autogen.sh..."
        ./autogen.sh || error "autogen.sh failed"
    fi
    
    local configure_args=""
    local host_flag=""
    
    case $target in
        "linux-x64")
            # Handle different host architectures
            if [[ "$ARCH_TYPE" == "x86_64" && "$OS_TYPE" == "Linux" ]]; then
                # Native x64 Linux build using contrib script
                build_log "Building Linux x64 with native dependencies..."
                
                # Install Berkeley DB 4.8 using contrib script (skip if exists)
                export CFLAGS="-Wno-error=implicit-function-declaration"
                if [[ ! -d "${SCRIPT_DIR}/db4" ]]; then
                    build_log "Installing Berkeley DB 4.8..."
                    ./contrib/install_db4.sh . || error "Failed to install BDB4"
                else
                    build_log "Using existing Berkeley DB 4.8 installation in db4/"
                fi
                
                export BDB_PREFIX="${SCRIPT_DIR}/db4"
                
                # Configure using native system dependencies
                build_log "Configuring with Berkeley DB and system dependencies..."
                ./autogen.sh || error "autogen.sh failed"
                
                # Set up library paths for system dependencies
                export PKG_CONFIG_PATH="/usr/lib/pkgconfig:$PKG_CONFIG_PATH"
                export LDFLAGS="-L${BDB_PREFIX}/lib -L/usr/lib/x86_64-linux-gnu"
                export CPPFLAGS="-I${BDB_PREFIX}/include"
                
                # Use system dependencies + Berkeley DB
                configure_args="--enable-cxx"
                configure_args="$configure_args --disable-shared"
                configure_args="$configure_args --disable-tests"
                configure_args="$configure_args --disable-gui-tests"
                configure_args="$configure_args --with-pic"
                configure_args="$configure_args --without-bench"
                configure_args="$configure_args --without-miniupnpc"
                configure_args="$configure_args --enable-zmq"
                configure_args="$configure_args --enable-wallet"
                
                # Berkeley DB paths
                configure_args="$configure_args BDB_LIBS=\"-L${BDB_PREFIX}/lib -ldb_cxx-4.8\""
                configure_args="$configure_args BDB_CFLAGS=\"-I${BDB_PREFIX}/include\""
                
                # Clore library linking order
                configure_args="$configure_args LDADD=\"\$(LIBCLORE_SERVER) \$(LIBCLORE_WALLET) \$(LIBCLORE_COMMON) \$(LIBCLORE_UTIL) \$(LIBCLORE_CRYPTO) \$(LIBUNIVALUE) \$(LIBLEVELDB) \$(LIBLEVELDB_SSE42) \$(LIBMEMENV) \$(BOOST_LIBS) \$(BDB_LIBS) \$(EVENT_PTHREADS_LIBS) \$(EVENT_LIBS) \$(ZMQ_LIBS)\""
                
                # No host flag needed for native build
                host_flag=""
            else
                # Cross-platform build via Docker
                echo -e "${YELLOW}Using Docker for Linux x64 build (cross-platform)...${NC}"
                build_linux_docker "$target"
                return $?
            fi
            ;;
        "linux-arm64")
            # Handle different host architectures
            if [[ ("$ARCH_TYPE" == "aarch64" || "$ARCH_TYPE" == "arm64") && "$OS_TYPE" == "Linux" ]]; then
                # Native ARM64 Linux build using contrib + system dependencies
                build_log "Building Linux ARM64 using contrib/install_db4.sh + native dependencies..."
                
                # Install Berkeley DB 4.8 using contrib script (skip if exists)
                export CFLAGS="-Wno-error=implicit-function-declaration"
                if [[ ! -d "${SCRIPT_DIR}/db4" ]]; then
                    build_log "Installing Berkeley DB 4.8..."
                    ./contrib/install_db4.sh . || error "Failed to install BDB4"
                else
                    build_log "Using existing Berkeley DB 4.8 installation in db4/"
                fi
                
                export BDB_PREFIX="${SCRIPT_DIR}/db4"
                
                # Configure using native system dependencies (no depends system needed)
                build_log "Configuring with Berkeley DB and system dependencies..."
                ./autogen.sh || error "autogen.sh failed"
                
                # Use system dependencies (available via apt-get) + Berkeley DB
                configure_args="--enable-cxx"
                configure_args="$configure_args --disable-shared"
                configure_args="$configure_args --disable-tests"
                configure_args="$configure_args --disable-gui-tests"
                configure_args="$configure_args --with-pic"
                configure_args="$configure_args --without-bench"
                configure_args="$configure_args --without-miniupnpc"
                configure_args="$configure_args --enable-zmq"
                configure_args="$configure_args --enable-wallet"
                configure_args="$configure_args BDB_LIBS=\"-L${BDB_PREFIX}/lib -ldb_cxx-4.8\""
                configure_args="$configure_args BDB_CFLAGS=\"-I${BDB_PREFIX}/include\""
                configure_args="$configure_args LDFLAGS=\"-L${BDB_PREFIX}/lib\""
                configure_args="$configure_args CPPFLAGS=\"-I${BDB_PREFIX}/include\""
            else
                # Cross-platform build via Docker
                echo -e "${YELLOW}Using Docker for Linux ARM64 build (cross-platform)...${NC}"
                build_linux_docker "$target" 
                return $?
            fi
            ;;
        "macos-x64")
            # macOS builds can only be done on macOS hosts
            if [[ "$OS_TYPE" != "Darwin" ]]; then
                error "macOS builds can only be performed on macOS hosts. Current host: $OS_TYPE"
                return 1
            fi
            
            # Use Berkeley DB + native dependencies (no depends system needed)
            build_log "Building macOS x64 with native dependencies..."
            
            # Install Berkeley DB 4.8 using contrib script (skip if exists)
            export CFLAGS="-Wno-error=implicit-function-declaration"
            if [[ ! -d "${SCRIPT_DIR}/db4" ]]; then
                build_log "Installing Berkeley DB 4.8..."
                ./contrib/install_db4.sh . || error "Failed to install BDB4"
            else
                build_log "Using existing Berkeley DB 4.8 installation in db4/"
            fi
            
            export BDB_PREFIX="${SCRIPT_DIR}/db4"
            
            # Configure using native system dependencies
            build_log "Configuring with Berkeley DB and system dependencies..."
            ./autogen.sh || error "autogen.sh failed"
            
            # Set up library paths for Homebrew dependencies
            local brew_prefix="/opt/homebrew"  # Apple Silicon default
            if [[ ! -d "$brew_prefix" ]]; then
                brew_prefix="/usr/local"  # Intel Mac fallback
            fi
            
            # Set environment variables for library detection
            export PKG_CONFIG_PATH="${brew_prefix}/lib/pkgconfig:$PKG_CONFIG_PATH"
            export LDFLAGS="-L${BDB_PREFIX}/lib -L${brew_prefix}/lib"
            export CPPFLAGS="-I${BDB_PREFIX}/include -I${brew_prefix}/include"

            # Override any Homebrew Berkeley DB detection to force use of local BDB_PREFIX
            export BERKELEY_DB_PREFIX="${BDB_PREFIX}"
            export BERKELEY_DB_LIBS="-L${BDB_PREFIX}/lib -ldb_cxx-4.8"
            export BERKELEY_DB_CFLAGS="-I${BDB_PREFIX}/include"
            
            # Use system dependencies (available via Homebrew/macOS) + Berkeley DB
            configure_args="--enable-cxx"
            configure_args="$configure_args --disable-shared"
            configure_args="$configure_args --disable-tests"
            configure_args="$configure_args --disable-gui-tests"
            configure_args="$configure_args --with-pic"
            configure_args="$configure_args --without-bench"
            configure_args="$configure_args --without-miniupnpc"
            configure_args="$configure_args --enable-zmq"
            configure_args="$configure_args --enable-wallet"
            
            # Berkeley DB paths (points to /db4 as user mentioned)
            configure_args="$configure_args BDB_LIBS=\"-L${BDB_PREFIX}/lib -ldb_cxx-4.8\""
            configure_args="$configure_args BDB_CFLAGS=\"-I${BDB_PREFIX}/include\""
            
            # Boost-specific library paths
            configure_args="$configure_args --with-boost=${brew_prefix}"
            configure_args="$configure_args --with-boost-libdir=${brew_prefix}/lib"
            
            # Ensure we target x86_64 specifically
            host_flag="--host=x86_64-apple-darwin"
            ;;
        "macos-arm64")
            # macOS builds can only be done on macOS hosts
            if [[ "$OS_TYPE" != "Darwin" ]]; then
                error "macOS builds can only be performed on macOS hosts. Current host: $OS_TYPE"
                return 1
            fi
            
            # Use Berkeley DB + native dependencies (no depends system needed)
            build_log "Building macOS ARM64 with native dependencies..."
            
            # Install Berkeley DB 4.8 using contrib script (skip if exists)
            export CFLAGS="-Wno-error=implicit-function-declaration"
            if [[ ! -d "${SCRIPT_DIR}/db4" ]]; then
                build_log "Installing Berkeley DB 4.8..."
                ./contrib/install_db4.sh . || error "Failed to install BDB4"
            else
                build_log "Using existing Berkeley DB 4.8 installation in db4/"
            fi
            
            export BDB_PREFIX="${SCRIPT_DIR}/db4"
            
            # Configure using native system dependencies
            build_log "Configuring with Berkeley DB and system dependencies..."
            ./autogen.sh || error "autogen.sh failed"
            
            # Set up library paths for Homebrew dependencies
            local brew_prefix="/opt/homebrew"  # Apple Silicon default
            if [[ ! -d "$brew_prefix" ]]; then
                brew_prefix="/usr/local"  # Intel Mac fallback
            fi
            
            # Set environment variables for library detection
            export PKG_CONFIG_PATH="${brew_prefix}/lib/pkgconfig:$PKG_CONFIG_PATH"
            export LDFLAGS="-L${BDB_PREFIX}/lib -L${brew_prefix}/lib"
            export CPPFLAGS="-I${BDB_PREFIX}/include -I${brew_prefix}/include"

            # Override any Homebrew Berkeley DB detection to force use of local BDB_PREFIX
            export BERKELEY_DB_PREFIX="${BDB_PREFIX}"
            export BERKELEY_DB_LIBS="-L${BDB_PREFIX}/lib -ldb_cxx-4.8"
            export BERKELEY_DB_CFLAGS="-I${BDB_PREFIX}/include"
            
            # Use system dependencies (available via Homebrew/macOS) + Berkeley DB
            configure_args="--enable-cxx"
            configure_args="$configure_args --disable-shared"
            configure_args="$configure_args --disable-tests"
            configure_args="$configure_args --disable-gui-tests"
            configure_args="$configure_args --with-pic"
            configure_args="$configure_args --without-bench"
            configure_args="$configure_args --without-miniupnpc"
            configure_args="$configure_args --enable-zmq"
            configure_args="$configure_args --enable-wallet"
            
            # Berkeley DB paths (points to /db4 as user mentioned)
            configure_args="$configure_args BDB_LIBS=\"-L${BDB_PREFIX}/lib -ldb_cxx-4.8\""
            configure_args="$configure_args BDB_CFLAGS=\"-I${BDB_PREFIX}/include\""
            
            # Boost-specific library paths
            configure_args="$configure_args --with-boost=${brew_prefix}"
            configure_args="$configure_args --with-boost-libdir=${brew_prefix}/lib"
            
            # Native ARM64 build (no cross-compilation needed)
            host_flag=""
            ;;
        "windows-x64")
            build_log "Configuring Windows x64 cross-compilation using depends system (original working method)..."
            
            # Build depends first (this creates the config.site)
            build_log "Building depends for x86_64-w64-mingw32..."
            cd "$SCRIPT_DIR/depends"
            make HOST=x86_64-w64-mingw32 -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4) || error "Failed to build depends"
            cd "$SCRIPT_DIR"
            
            # Use the original working configuration exactly as provided by user
            export CONFIG_SITE="$PWD/depends/x86_64-w64-mingw32/share/config.site"
            configure_args="--prefix=$PWD/depends/x86_64-w64-mingw32"
            configure_args="$configure_args --enable-cxx"
            configure_args="$configure_args --disable-shared"
            configure_args="$configure_args --disable-tests"
            configure_args="$configure_args --with-pic"
            configure_args="$configure_args --without-bench"
            configure_args="$configure_args --enable-zmq"
            configure_args="$configure_args --enable-wallet"
            configure_args="$configure_args --with-tx"
            host_flag="--host=x86_64-w64-mingw32"
            ;;
    esac
    
    # Configure the build
    if [[ -n "$host_flag" ]]; then
        eval "./configure $configure_args $host_flag" || error "Configure failed for $target"
    else
        eval "./configure $configure_args" || error "Configure failed for $target"
    fi
}

# Build binaries for specific target
build_target() {
    local target=$1
    local target_dir="$BUILD_DIR/$target"
    
    build_log "Building $target..."
    
    # Configure and build for this target
    configure_build "$target"
    
    # Check if build completed successfully
    if [[ ! -f "src/clore_blockchaind" && ! -f "src/clore_blockchaind.exe" ]]; then
        error "Build failed - daemon binary not found"
    fi
    
    # Create target directory and copy binaries
    mkdir -p "$target_dir/bin"
    
    # Copy binaries based on platform (server-only, no GUI)
    if [[ $target == windows-* ]]; then
        # Windows binaries have .exe extension
        cp src/clore_blockchaind.exe "$target_dir/bin/" 2>/dev/null || cp src/clore_blockchaind "$target_dir/bin/clore_blockchaind.exe"
        cp src/clore-cli.exe "$target_dir/bin/" 2>/dev/null || cp src/clore-cli "$target_dir/bin/clore-cli.exe"
    else
        # Unix-like systems
        cp src/clore_blockchaind "$target_dir/bin/"
        cp src/clore-cli "$target_dir/bin/"
    fi
    
    # Strip binaries if strip is available
    if command -v strip >/dev/null 2>&1; then
        build_log "Stripping binaries for $target..."
        case $target in
            windows-*)
                if command -v x86_64-w64-mingw32-strip >/dev/null 2>&1; then
                    x86_64-w64-mingw32-strip "$target_dir/bin"/*.exe
                fi
                ;;
            *)
                strip "$target_dir/bin"/* 2>/dev/null || true
                ;;
        esac
    fi
    
    build_log "Build completed for $target"
}

# Create Linux packages (deb and rpm)
create_linux_packages() {
    local target=$1
    local target_dir="$BUILD_DIR/$target"
    local arch_suffix=""
    
    case $target in
        "linux-x64") arch_suffix="amd64" ;;
        "linux-arm64") arch_suffix="arm64" ;;
    esac
    
    build_log "Creating Linux packages for $target..."
    
    local package_dir="$RELEASE_DIR/linux-$arch_suffix"
    mkdir -p "$package_dir"
    
    # Create DEB package
    create_deb_package "$target" "$arch_suffix" "$package_dir"
    
    # Create RPM package (if rpmbuild is available)
    if command -v rpmbuild >/dev/null 2>&1; then
        create_rpm_package "$target" "$arch_suffix" "$package_dir"
    else
        warning "rpmbuild not available, skipping RPM package creation"
    fi
    
    # Create simple tar.gz archive
    create_tarball "$target" "$package_dir"
}

# Create DEB package
create_deb_package() {
    local target=$1
    local arch=$2
    local package_dir=$3
    local target_dir="$BUILD_DIR/$target"
    
    local deb_dir="$package_dir/clore-$VERSION-$arch"
    mkdir -p "$deb_dir/DEBIAN"
    mkdir -p "$deb_dir/usr/local/bin"
    mkdir -p "$deb_dir/usr/share/applications"
    mkdir -p "$deb_dir/usr/share/icons"
    
    # Copy binaries
    cp "$target_dir/bin"/* "$deb_dir/usr/local/bin/"
    
    # Copy icon if it exists
    if [[ -f CLORE_small.png ]]; then
        cp CLORE_small.png "$deb_dir/usr/share/icons/"
    fi
    
    # Note: No desktop file created - server-only build
    
    # Create control file
    cat > "$deb_dir/DEBIAN/control" << EOF
Package: clore
Version: $VERSION
Section: utils
Priority: optional
Architecture: $arch
Depends: libc6, libstdc++6, libboost-system1.74.0, libboost-filesystem1.74.0
Maintainer: CLORE Development Team <dev@clore.ai>
Description: CLORE cryptocurrency daemon and tools
 CLORE is a proof-of-stake cryptocurrency with validator support.
 This package includes the daemon and CLI tools.
Homepage: https://clore.ai
EOF
    
    # Build DEB package
    if command -v dpkg-deb >/dev/null 2>&1; then
        dpkg-deb --build "$deb_dir" "$package_dir/clore-$VERSION-$arch.deb"
        build_log "Created DEB package: clore-$VERSION-$arch.deb"
    else
        warning "dpkg-deb not available, skipping DEB package creation"
    fi
}

# Create RPM package
create_rpm_package() {
    local target=$1
    local arch=$2
    local package_dir=$3
    
    build_log "Creating RPM package for $arch..."
    # RPM creation code here - similar to existing but updated
    # This is complex and might need rpmbuild environment setup
}

# Create tar.gz archive
create_tarball() {
    local target=$1
    local package_dir=$2
    local target_dir="$BUILD_DIR/$target"
    
    local archive_name="clore-$VERSION-$target"
    local archive_dir="$package_dir/$archive_name"
    
    mkdir -p "$archive_dir"
    cp -r "$target_dir/bin" "$archive_dir/"
    
    # Add README
    cat > "$archive_dir/README.txt" << EOF
CLORE v$VERSION - $target

This archive contains the CLORE cryptocurrency binaries:
- clore_blockchaind: The CLORE daemon
- clore-cli: Command line interface

For more information, visit: https://clore.ai

Installation:
1. Extract this archive to your desired location
2. Add the bin/ directory to your PATH
3. Run clore_blockchaind for the daemon

Support: https://github.com/clore-ai/clore
EOF
    
    cd "$package_dir"
    tar -czf "$archive_name.tar.gz" "$archive_name"
    rm -rf "$archive_name"
    
    build_log "Created archive: $archive_name.tar.gz"
}

# Create macOS packages
create_macos_packages() {
    local target=$1
    local target_dir="$BUILD_DIR/$target"
    
    build_log "Creating macOS packages for $target..."
    
    local package_dir="$RELEASE_DIR/macos"
    mkdir -p "$package_dir"
    
    # Note: No GUI App Bundle - server-only build
    
    # Create simple archive for CLI tools
    create_tarball "$target" "$package_dir"
}

# Create Windows packages
create_windows_packages() {
    local target=$1
    local target_dir="$BUILD_DIR/$target"
    
    build_log "Creating Windows packages for $target..."
    
    local package_dir="$RELEASE_DIR/windows"
    mkdir -p "$package_dir"
    
    # Create ZIP archive
    create_windows_zip "$target" "$package_dir"
    
    # Create installer (if makensis is available)
    if command -v makensis >/dev/null 2>&1; then
        create_windows_installer "$target" "$package_dir"
    else
        warning "NSIS not available, skipping Windows installer creation"
    fi
}

# Create Windows ZIP archive
create_windows_zip() {
    local target=$1
    local package_dir=$2
    local target_dir="$BUILD_DIR/$target"
    
    local archive_name="clore-$VERSION-windows-x64"
    local archive_dir="$package_dir/$archive_name"
    
    mkdir -p "$archive_dir"
    cp "$target_dir/bin"/*.exe "$archive_dir/"
    
    # Add Windows-specific README
    cat > "$archive_dir/README.txt" << EOF
CLORE v$VERSION - Windows x64

This archive contains the CLORE cryptocurrency binaries:
- clore_blockchaind.exe: The CLORE daemon
- clore-cli.exe: Command line interface

For more information, visit: https://clore.ai

Installation:
1. Extract this archive to your desired location (e.g., C:\CLORE)
2. Run clore_blockchaind.exe for the daemon
3. For command line usage, open Command Prompt and navigate to the extracted folder

Note: Windows may show security warnings for these files. This is normal for 
cryptocurrency software. You may need to add exceptions to your antivirus.

Support: https://github.com/clore-ai/clore
EOF
    
    cd "$package_dir"
    if command -v zip >/dev/null 2>&1; then
        zip -r "$archive_name.zip" "$archive_name"
    else
        # Fallback to tar if zip not available
        tar -czf "$archive_name.tar.gz" "$archive_name"
    fi
    rm -rf "$archive_name"
    
    build_log "Created Windows archive"
}

# Create Windows installer (NSIS)
create_windows_installer() {
    local target=$1
    local package_dir=$2
    
    build_log "Creating Windows installer..."
    # NSIS installer script would go here
    # This is complex and requires NSIS to be installed
}

# Generate checksums for all packages
generate_checksums() {
    build_log "Generating checksums..."
    
    cd "$RELEASE_DIR"
    find . -type f \( -name "*.deb" -o -name "*.rpm" -o -name "*.tar.gz" -o -name "*.zip" -o -name "*.dmg" \) -exec shasum -a 256 {} \; > SHA256SUMS
    
    build_log "Checksums generated in SHA256SUMS"
}

# Main build process
main() {
    build_log "Starting multi-platform build process..."
    
    # Check dependencies
    check_dependencies
    
    # Build for each target
    local successful_builds=()
    local failed_builds=()
    
    for target in "${!TARGETS[@]}"; do
        build_log "Starting build for $target..."
        
        # Build the target
        if build_target "$target"; then
            successful_builds+=("$target")
            # Create platform-specific packages
            case $target in
                linux-*)
                    create_linux_packages "$target"
                    ;;
                macos-*)
                    create_macos_packages "$target"
                    ;;
                windows-*)
                    create_windows_packages "$target"
                    ;;
            esac
        else
            failed_builds+=("$target")
            warning "Build failed for $target, continuing with other targets..."
        fi
        
        # Clean environment variables for next build
        unset CC CXX CFLAGS CXXFLAGS LDFLAGS BOOST_CPPFLAGS BOOST_LDFLAGS BOOST_THREAD_LIB BOOST_CHRONO_LIB
        unset ac_cv_sleep ac_cv_boost_sleep BOOST_THREAD_SHARED_LIB PKG_CONFIG_PATH
    done
    
    # Generate checksums
    if [[ ${#successful_builds[@]} -gt 0 ]]; then
        generate_checksums
    fi
    
    build_log "Multi-platform build completed!"
    
    # Show summary
    echo -e "\n${BLUE}=== Build Summary ===${NC}"
    
    if [[ ${#successful_builds[@]} -gt 0 ]]; then
        echo -e "${GREEN}✅ Successful builds (${#successful_builds[@]}):${NC}"
        for target in "${successful_builds[@]}"; do
            echo -e "  ${GREEN}✅ $target${NC}"
        done
        echo ""
        build_log "Release packages created in: $RELEASE_DIR"
        find "$RELEASE_DIR" -type f \( -name "*.deb" -o -name "*.rpm" -o -name "*.tar.gz" -o -name "*.zip" -o -name "*.dmg" \) -exec basename {} \; | sort | sed 's/^/  /'
    fi
    
    if [[ ${#failed_builds[@]} -gt 0 ]]; then
        echo ""
        echo -e "${RED}❌ Failed builds (${#failed_builds[@]}):${NC}"
        for target in "${failed_builds[@]}"; do
            echo -e "  ${RED}❌ $target${NC}"
        done
        echo ""
        
        if [[ ${#successful_builds[@]} -eq 0 ]]; then
            echo -e "${RED}No builds completed successfully.${NC}"
        fi
    fi
}

# Handle command line arguments
case "${1:-build}" in
    "clean")
        build_log "Cleaning build directories..."
        rm -rf "$BUILD_DIR" "$RELEASE_DIR"
        ;;
    "build"|"")
        main
        ;;
    "help"|"-h"|"--help")
        echo "CLORE Multi-Platform Release Builder"
        echo ""
        echo "Usage: $0 [command]"
        echo ""
        echo "Commands:"
        echo "  build    Build releases for enabled platforms (default)"
        echo "  clean    Clean build directories"
        echo "  help     Show this help message"
        echo ""
        echo "Platform Configuration (edit flags at top of script):"
        echo "  BUILD_LINUX_X64=$BUILD_LINUX_X64      # Linux x86_64 (Intel/AMD servers)"
        echo "  BUILD_LINUX_ARM64=$BUILD_LINUX_ARM64    # Linux ARM64 (AWS Graviton, containers)"
        echo "  BUILD_MACOS_X64=$BUILD_MACOS_X64      # Intel Macs (also works on Apple Silicon)"
        echo "  BUILD_MACOS_ARM64=$BUILD_MACOS_ARM64    # Apple Silicon Macs (M1/M2/M3)"
        echo "  BUILD_WINDOWS_X64=$BUILD_WINDOWS_X64    # Windows x86_64"
        echo ""
        echo "Build Host Architecture Support:"
        echo "  Apple Silicon (M1/M2/M3):  ✅ macOS ARM64 (native)  ✅ Linux ARM64 (Docker)  ⚠️  Linux x64 (slow)  ❌ Windows x64"
        echo "  Intel Mac:                 ✅ macOS x64 (native)    ✅ Linux x64 (Docker)   ⚠️  Linux ARM64 (slow) ✅ Windows x64 (MinGW)"  
        echo "  Linux x64:                 ✅ Linux x64 (native)    ✅ Windows x64 (MinGW)  ⚠️  Linux ARM64 (slow) ❌ macOS (impossible)"
        echo "  Linux ARM64:               ✅ Linux ARM64 (native)  ⚠️  Linux x64 (slow)    ⚠️  Windows x64 (slow)  ❌ macOS (impossible)"
        echo ""
        echo "Legend: ✅ Fast/Reliable  ⚠️ Slow/May Fail  ❌ Not Possible"
        echo ""
        if [[ ${#TARGETS[@]} -gt 0 ]]; then
            echo "Currently enabled targets:"
            for target in "${!TARGETS[@]}"; do
                echo "  ✅ $target"
            done
        else
            echo "❌ No targets currently enabled!"
        fi
        echo ""
        echo "Binary Compatibility:"
        echo "  • ARM64 Linux ➜ Only works on ARM64 Linux systems"
        echo "  • x64 Linux   ➜ Only works on x86_64 Linux systems"
        echo "  • ARM64 macOS ➜ Only works on Apple Silicon Macs"
        echo "  • x64 macOS   ➜ Works on Intel Macs AND Apple Silicon (via Rosetta)"
        echo "  • x64 Windows ➜ Works on x86_64 Windows systems"
        ;;
    *)
        error "Unknown command: $1. Use '$0 help' for usage information."
        ;;
esac



