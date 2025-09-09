# Building CLORE on Linux (Ubuntu 22.04)

## Prerequisites
Ubuntu 22.04 or compatible Linux distribution

## Quick Build Steps

### 1. Install Dependencies
```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential libtool autotools-dev automake pkg-config \
    libssl-dev libevent-dev bsdmainutils python3 \
    libboost-system-dev libboost-filesystem-dev libboost-chrono-dev \
    libboost-program-options-dev libboost-test-dev libboost-thread-dev \
    libzmq3-dev wget patch git
```
Download Clore git
Enter Clore Files

CLORE_ROOT=$PWD
export BDB_PREFIX="$CLORE_ROOT/db4"

### 2. Build Berkeley DB 4.8
```bash
# Download and extract
wget 'http://download.oracle.com/berkeley-db/db-4.8.30.NC.tar.gz'
echo '12edc0df75bf9abd7f82f821795bcee50f42cb2e5f76a6a281b85732798364ef  db-4.8.30.NC.tar.gz' | sha256sum -c
tar -xzvf db-4.8.30.NC.tar.gz

mv -r db-4.8.30.NC $CLORE_ROOT/db4

# Apply patches
chmod a+w db-4.8.30.NC/dbinc/atomic.h
cp depends/patches/atomic.h db-4.8.30.NC/dbinc/
cp depends/config.guess db-4.8.30.NC/dist/
cp depends/config.sub db-4.8.30.NC/dist/
chmod +x db-4.8.30.NC/dist/config.{guess,sub}

# Build and install to local directory
cd db-4.8.30.NC/build_unix/
../dist/configure --enable-cxx --disable-shared --with-pic \
    --prefix=$CLORE_ROOT/db4 --with-mutex=POSIX/pthreads
make -j$(nproc)
make install
cd ../..
rm -rf db-4.8.30.NC*
```

### 3. Configure and Build CLORE
```bash
# Set Berkeley DB location
export BDB_PREFIX="$CLORE_ROOT/db4"

# Generate build files
./autogen.sh

# Configure (with wallet support for staking functionality)
./configure \
    --enable-cxx \
    --disable-shared \
    --disable-tests \
    --disable-gui-tests \
    --with-pic \
    --disable-bench \
    --without-miniupnpc \
    --enable-zmq \
    --enable-wallet \
    BDB_LIBS="-L${BDB_PREFIX}/lib -ldb_cxx-4.8" \
    BDB_CFLAGS="-I${BDB_PREFIX}/include" \
    LDFLAGS="-L${BDB_PREFIX}/lib" \
    CPPFLAGS="-I${BDB_PREFIX}/include" \
    CXXFLAGS="-fPIC -I${BDB_PREFIX}/include -DBOOST_SPIRIT_THREADSAFE -DHAVE_BUILD_INFO -D__STDC_FORMAT_MACROS" \
    LIBS="-ldb_cxx-4.8 -lboost_system -lzmq"

# Build
make -j$(nproc)
```

### 4. Install Binaries
```bash
# Optional: strip debug symbols for smaller size
strip src/clore_blockchaind src/clore-cli

# Install to system (optional)
sudo make install

# Or copy to custom location
mkdir -p ~/clore-bin
cp src/clore_blockchaind ~/clore-bin/
cp src/clore-cli ~/clore-bin/
```

## Running CLORE

```bash
# Start daemon - if DEBUG add  -printtoconsole and remove  -daemon
./clore_blockchaind -daemon

# Check status
./clore-cli getinfo

# Stop daemon
./clore-cli stop
```

## Build Options

### Disable Wallet Support
Replace `--enable-wallet` with `--disable-wallet` (not recommended - staking requires wallet)

### Enable Tests
Replace `--disable-tests` with `--enable-tests`

### Enable Benchmarks  
Replace `--disable-bench` with `--enable-bench`

## Troubleshooting

- **Linking errors**: Wallet support is required for staking functionality in CLORE.
- **Missing dependencies**: Install dev packages: `sudo apt-get install libboost-all-dev`
- **Berkeley DB issues**: Ensure BDB_PREFIX points to your db4 installation

## Tested On
- Ubuntu 22.04 LTS
- Build time: ~20-30 minutes on 4-core CPU
