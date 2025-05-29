# Building Clore Chain on Linux (Ubuntu 22.04)

This document outlines the step-by-step process to build the Clore blockchain from source, using Berkeley DB 4.8, and strip binaries to reduce size.

---

## System Requirements

Ensure the following dependencies are installed:

```bash
sudo apt-get update && sudo apt-get install -y \
  build-essential \
  libtool \
  autotools-dev \
  automake \
  pkg-config \
  libssl-dev \
  libevent-dev \
  bsdmainutils \
  python3
```

---

## Setup Environment

```bash
CLORE_ROOT=$(pwd)
BDB_PREFIX="${CLORE_ROOT}/db4"
mkdir -p $BDB_PREFIX
```

---

## Install Berkeley DB 4.8

```bash
wget -c 'http://download.oracle.com/berkeley-db/db-4.8.30.NC.tar.gz'
echo '12edc0df75bf9abd7f82f821795bcee50f42cb2e5f76a6a281b85732798364ef  db-4.8.30.NC.tar.gz' | sha256sum -c
# Expected output: db-4.8.30.NC.tar.gz: OK

tar -xzvf db-4.8.30.NC.tar.gz
chmod a+w ./db-4.8.30.NC/dbinc/atomic.h
cp ./depends/patches/atomic.h db-4.8.30.NC/dbinc/

cd db-4.8.30.NC/build_unix/
../dist/configure --enable-cxx --disable-shared --with-pic --prefix=$BDB_PREFIX
make
make install
```

---

## Build Clore Core

```bash
cd $CLORE_ROOT
./autogen.sh
CONFIG_SITE=$PWD/depends/x86_64-pc-linux-gnu/share/config.site \
  ./configure \
  --prefix=$PWD/depends/x86_64-pc-linux-gnu \
  --enable-cxx \
  --disable-shared \
  --disable-tests \
  --disable-gui-tests \
  --with-pic \
  LDFLAGS="-L${BDB_PREFIX}/lib/" \
  CPPFLAGS="-I${BDB_PREFIX}/include/"

make -j$(nproc)
make deploy
```

---

## Strip Binaries (Optional but Recommended)

Strip symbols and debugging info to reduce binary size:

```bash
strip src/clore-cli
strip src/clore_blockchaind
```

> This step reduces the daemon size from \~190MB to \~10MB.
>
> ⚠️ **Warning:** This makes debugging harder. Keep a non-stripped version for development.

---

## Done!

Your Clore chain binaries are now built and optimized for deployment.

---

For issues or contributions, please open an issue or PR in the GitHub repository.
