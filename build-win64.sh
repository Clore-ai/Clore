#!/bin/bash

set -e

CLORE_ROOT=$(pwd)
BDB_PREFIX="${CLORE_ROOT}/db4"
HOST="x86_64-w64-mingw32"

echo "Step 1: Install required packages..."
sudo apt update
sudo apt install -y build-essential libtool autotools-dev automake pkg-config bsdmainutils curl nsis \
    g++-mingw-w64-x86-64 mingw-w64-x86-64-dev

echo "Step 2: Build Berkeley DB 4.8..."
mkdir -p $BDB_PREFIX
wget -nc 'http://download.oracle.com/berkeley-db/db-4.8.30.NC.tar.gz'
echo '12edc0df75bf9abd7f82f821795bcee50f42cb2e5f76a6a281b85732798364ef  db-4.8.30.NC.tar.gz' | sha256sum -c
tar -xf db-4.8.30.NC.tar.gz
chmod a+w ./db-4.8.30.NC/dbinc/atomic.h
cp ./depends/patches/atomic.h db-4.8.30.NC/dbinc/

cd db-4.8.30.NC/build_unix/
../dist/configure --enable-cxx --disable-shared --with-pic --prefix=$BDB_PREFIX
make -j$(nproc)
make install
cd $CLORE_ROOT

echo "Step 3: Build depends..."
cd depends
make HOST=$HOST -j$(nproc)
cd ..

echo "Step 4: Configure Clore Core for Windows..."
./autogen.sh
CONFIG_SITE=$PWD/depends/${HOST}/share/config.site \
./configure --host=${HOST} \
  --disable-tests --disable-bench --with-gui=no \
  LDFLAGS="-L${BDB_PREFIX}/lib -static-libgcc -static-libstdc++" \
  CPPFLAGS="-I${BDB_PREFIX}/include"

echo "🚀 Step 5: Building Clore..."
make -j$(nproc)

echo "✅ Build complete. Output files:"
find src/ -name '*.exe'

