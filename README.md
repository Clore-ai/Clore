# CLORE Blockchain

CLORE is a cryptocurrency blockchain that started as proof of work and transaitioned to proof-of-stake consensus reenforced with multiple validators.

## Features

- **Proof-of-Stake (PoS)**: Energy-efficient consensus mechanism
- **Validators**: Network infrastructure nodes with additional functionality
- **Cross-Platform**: Supports Linux, macOS, and Windows

## Recent Updates

### Proof of Stake Implementation

- **4-Phase PoS Transition**: Safety-first approach with PoW security during infrastructure buildup
- **DEPLOYMENT_POS**: BIP9 miner signaling for network readiness (currently disabled)
- **ENABLE_POS_VALIDATORS**: Validator infrastructure activation (Phase 1)
- **ENABLE_POS_STAKING**: Staking logic activation with 600-block depth (Phase 2)
- **ENABLE_POS_REWARDS**: Pure PoS enforcement, PoW permanently disabled (Phase 3)
- Enhanced staking infrastructure with real-time weight calculation
- Advanced PoS RPC command suite

### Multi-Platform Build

- **Cross-platform builds**: Linux, macOS, Windows from Mac M1
- **Multiple architectures**: x64 and ARM64 support
- **Professional packaging**: DEB, RPM, ZIP, APP bundles
- **Automated checksums**: SHA256 verification for all packages

## Building

### Single Platform Build

```bash
./autogen.sh
./configure
make
```

### Multi-Platform Release Build

The `release-multiplatform.sh` script builds release packages for multiple platforms and architectures from a single host environment.

#### Supported Build Hosts

| Host OS | Linux  | macOS  | Windows |
| ------- | ------ | ------ | ------- |
| macOS   | Docker | Native | MinGW   |
| Linux   | Native | No     | MinGW   |

#### Prerequisites

**macOS Host:**

```bash
# Install Xcode command line tools
xcode-select --install

# Install Homebrew
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install dependencies
brew install bash mingw-w64

# Install Docker Desktop (for Linux builds)
# Download from https://docker.com
```

**Linux Host (Ubuntu 22.04+):**

```bash
# Install build dependencies
sudo apt-get update
sudo apt-get install build-essential libtool autotools-dev automake \
    pkg-config libssl-dev libevent-dev libboost-all-dev \
    wget patch \
    gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64

# Install Docker (for cross-platform builds)
sudo apt-get install docker.io
sudo usermod -aG docker $USER
sudo systemctl start docker
```

#### Build Configuration

Edit the platform flags at the top of `release-multiplatform.sh`:

```bash
BUILD_LINUX_X64=true      # Linux x86_64 (Intel/AMD servers)
BUILD_LINUX_ARM64=true    # Linux ARM64 (AWS Graviton, containers)
BUILD_MACOS_X64=true      # Intel Macs (Rosetta compatible)
BUILD_MACOS_ARM64=true    # Apple Silicon Macs (M1/M2/M3)
BUILD_WINDOWS_X64=true    # Windows x86_64
```

#### Usage Examples

**Build All Platforms:**

```bash
./release-multiplatform.sh
```

**Build Specific Platforms:**

```bash
# Edit script to set BUILD_LINUX_X64=true, others=false
./release-multiplatform.sh

# Or use clean to reset between builds
./release-multiplatform.sh clean
```

**View Available Options:**

```bash
./release-multiplatform.sh help
```

#### Output Structure

```
release-packages/
├── linux-amd64/
│   └── clore-x.x.x-linux-x64.tar.gz
├── linux-arm64/
│   └── clore-x.x.x-linux-arm64.tar.gz
├── macos/
│   ├── clore-x.x.x-macos-x64.tar.gz
│   └── clore-x.x.x-macos-arm64.tar.gz
├── windows/
│   └── clore-x.x.x-windows-x64.zip
└── SHA256SUMS
```

#### Platform Compatibility

**Binary Compatibility:**

- ARM64 Linux: Only works on ARM64 Linux systems
- x64 Linux: Only works on x86_64 Linux systems
- ARM64 macOS: Only works on Apple Silicon Macs
- x64 macOS: Works on Intel Macs and Apple Silicon (via Rosetta)
- x64 Windows: Works on x86_64 Windows systems

**Build Host Limitations:**

- macOS builds can only be performed on macOS hosts
- Linux builds use Docker for cross-platform support
- Windows builds require MinGW-w64 cross-compiler

#### Troubleshooting

**Docker Issues:**

```bash
# macOS: Start Docker Desktop
open -a Docker

# Linux: Start Docker service
sudo systemctl start docker
```

**Missing Dependencies:**

```bash
# macOS: Install via Homebrew
brew install autoconf automake libtool wget

# Linux: Install via apt
sudo apt-get install build-essential autotools-dev wget patch
```

**Berkeley DB Errors:**

Berkeley DB 4.8 is automatically built from source during the build process. If you encounter BDB-related errors:

```bash
# Ensure required tools are installed
# macOS:
brew install wget

# Linux:
sudo apt-get install wget patch

# The build script will automatically download and compile Berkeley DB 4.8
```

**Cross-Compilation Failures:**

- Boost sleep implementation errors on macOS cross-compilation are known issues
- Run builds on matching architecture hosts for best results
- Check MinGW-w64 installation for Windows build failures

For comprehensive build setup, see [MULTIPLATFORM_BUILD_SETUP.md](MULTIPLATFORM_BUILD_SETUP.md).

## Consensus Upgrade System

CLORE uses a **dual upgrade mechanism** providing both safety and precision in network upgrades:

### vDeployments (BIP9 Version Bits)

- **DEPLOYMENT_POS**: Miner signaling for PoS readiness (bit 11)
- **Threshold-based activation**: Requires miner consensus (currently disabled with threshold 999999999)
- **Time-bounded windows**: Has start/timeout periods for activation attempts
- **Purpose**: Ensures network has upgraded software before enabling PoS

### vUpgrades (Height-Based Network Upgrades)

- **ENABLE_POS_VALIDATORS**: Validator infrastructure activation (Height: 1,000,001,439)
- **ENABLE_POS_STAKING**: Staking logic activation with 600-block depth (Height: 1,000,002,879)
- **ENABLE_POS_REWARDS**: Pure PoS enforcement, PoW disabled (Height: 1,000,004,319)
- **1440-block spacing**: Gradual transition with time for infrastructure buildup
- **Deterministic activation**: Takes effect exactly at specified block heights

### PoS Transition Strategy

**Safety-First Approach**: PoW mining continues to secure the network while PoS infrastructure builds up gradually. Only when both validators and staking are fully operational does the network transition to PoS-only consensus.

| Phase       | Height          | Mining Status  | Infrastructure      | Purpose                    |
| ----------- | --------------- | -------------- | ------------------- | -------------------------- |
| **Phase 0** | < 1,000,001,439 | **PoW Only**   | None                | Traditional PoW mining     |
| **Phase 1** | 1,000,001,439   | **PoW Secure** | Validators Online   | Infrastructure buildup     |
| **Phase 2** | 1,000,002,879   | **PoW Secure** | + Staking Active    | Dual consensus preparation |
| **Phase 3** | 1,000,004,319   | **PoS Only**   | Full Infrastructure | Complete PoS transition    |

For detailed technical information, see [CONSENSUS_UPGRADE_MECHANISMS.md](CONSENSUS_UPGRADE_MECHANISMS.md).

## Validator System

### Network Authorization

- **Address-based**: Uses CLORE wallet addresses for authorization
- **Network-specific**: Different authorized lists per network
- **Flexible management**: Easy addition/removal of authorized nodes

### RPC Commands (20 total)

Core validator management commands plus advanced features:

- `validatorlist`, `getvalidatorcount`, `getvalidatorstatus`
- `listauthorizedvalidators`, `checkvalidatorauth`
- `createvalidatorconfig`, `addvalidatorconfig`, `removevalidatorconfig`

## Staking System

### PoS Features

- **Real-time weight calculation**: Dynamic staking power assessment
- **Hybrid validator-staking**: Combined infrastructure benefits
- **Professional analytics**: Comprehensive reward tracking
- **Flexible control**: Start/stop staking with status feedback

### Staking RPC Commands

- `getstakinginfo`: Enhanced real-time staking status
- `getvalidatorstakinginfo`: Validator-PoS hybrid information
- `getstakingrewards`: Complete analytics suite
- `setstaking`: Professional staking control
- `getposinfo`: PoS transition status

## Network Configuration

### Mainnet (Production)

- **DEPLOYMENT_POS**: Block 999,999,999 (BIP9 signaling - currently disabled)
- **ENABLE_POS_VALIDATORS**: Block 1,000,001,439 (Validator infrastructure)
- **ENABLE_POS_STAKING**: Block 1,000,002,879 (Staking logic activation)
- **ENABLE_POS_REWARDS**: Block 1,000,004,319 (Pure PoS enforcement)
- **Spacing**: 1440 blocks between phases (~1 day at 1 min/block)

### Testnet (Testing Environment)

- **ENABLE_POS_VALIDATORS**: Block 1,000 (Validator infrastructure)
- **ENABLE_POS_STAKING**: Block 2,440 (Staking logic activation)
- **ENABLE_POS_REWARDS**: Block 3,880 (Pure PoS enforcement)
- **Spacing**: 1440 blocks for proper testing of full transition

### Regtest (Development)

- **ENABLE_POS_VALIDATORS**: Block 100 (Validator infrastructure)
- **ENABLE_POS_STAKING**: Block 150 (Staking logic activation)
- **ENABLE_POS_REWARDS**: Block 200 (Pure PoS enforcement)
- **Spacing**: 50 blocks for rapid testing and development

## Testing

### Comprehensive Test Suite

- `feature_pos_comprehensive.py`: PoS upgrade sequence validation
- `feature_pos_upgrade_sequence.py`: Multi-node synchronization testing
- `feature_validator_lifecycle.py`: Complete validator workflow
- `feature_validator_multinode.py`: Network consistency validation

Perfect 3-node synchronization achieved (100% success rate).

## Documentation

- **[Consensus Upgrades](CONSENSUS_UPGRADE_MECHANISMS.md)**: Technical details of upgrade system
- **[Build Setup](MULTIPLATFORM_BUILD_SETUP.md)**: Multi-platform build instructions
- **[PoS Changelog](CHANGELOG-POS.md)**: Detailed implementation history

## Development Status

- ✅ **Phase 1**: Validator infrastructure foundation
- ✅ **Phase 2**: Address-based authorization system
- ✅ **Phase 3**: Pure PoS consensus implementation
- 🆕 **Multi-Platform**: Cross-platform build system

## Support

- **Repository**: [GitHub](https://github.com/clore-ai/clore)
- **Website**: [clore.ai](https://clore.ai)
- **Issues**: [Report bugs](https://github.com/clore-ai/clore/issues)

---

_CLORE: Powering the future of decentralized computing with proof-of-stake consensus._
