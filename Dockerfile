FROM ubuntu:22.04

# Accept build arguments
ARG ENABLE_TESTS=false

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    libtool \
    autotools-dev \
    automake \
    pkg-config \
    libssl-dev \
    libevent-dev \
    bsdmainutils \
    python3 \
    libboost-system-dev \
    libboost-filesystem-dev \
    libboost-chrono-dev \
    libboost-program-options-dev \
    libboost-test-dev \
    libboost-thread-dev \
    libzmq3-dev \
    wget \
    patch \
    git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY . .

# Build Berkeley DB 4.8 from source
RUN mkdir -p /build/db4 && \
    wget -c 'http://download.oracle.com/berkeley-db/db-4.8.30.NC.tar.gz' && \
    echo '12edc0df75bf9abd7f82f821795bcee50f42cb2e5f76a6a281b85732798364ef  db-4.8.30.NC.tar.gz' | sha256sum -c && \
    tar -xzvf db-4.8.30.NC.tar.gz && \
    chmod a+w ./db-4.8.30.NC/dbinc/atomic.h && \
    cp ./depends/patches/atomic.h db-4.8.30.NC/dbinc/ && \
    # Update config.guess and config.sub for modern architectures \
    cp ./depends/config.guess db-4.8.30.NC/dist/config.guess && \
    cp ./depends/config.sub db-4.8.30.NC/dist/config.sub && \
    chmod +x db-4.8.30.NC/dist/config.guess db-4.8.30.NC/dist/config.sub && \
    cd db-4.8.30.NC/build_unix/ && \
    ../dist/configure --enable-cxx --disable-shared --with-pic --prefix=/build/db4 --with-mutex=POSIX/pthreads && \
    make -j$(nproc) && \
    ranlib libdb-4.8.a && \
    ranlib libdb_cxx-4.8.a && \
    make install && \
    cd /build && \
    rm -f db-4.8.30.NC.tar.gz && \
    rm -rf db-4.8.30.NC

# Build project using native system dependencies (no depends system needed)
RUN export BDB_PREFIX="/build/db4" && \
    ./autogen.sh && \
    if [ "$ENABLE_TESTS" = "true" ]; then \
        TEST_FLAG="--enable-tests"; \
    else \
        TEST_FLAG="--disable-tests"; \
    fi && \
    ./configure \
      --enable-cxx \
      --disable-shared \
      $TEST_FLAG \
      --disable-gui-tests \
      --with-pic \
      --disable-bench \
      --without-miniupnpc \
      --enable-zmq \
      --disable-wallet \
      BDB_LIBS="-L${BDB_PREFIX}/lib -ldb_cxx-4.8" \
      BDB_CFLAGS="-I${BDB_PREFIX}/include" \
      LDFLAGS="-L${BDB_PREFIX}/lib -L/usr/lib" \
      CPPFLAGS="-I${BDB_PREFIX}/include" \
      CXXFLAGS="-fPIC -I${BDB_PREFIX}/include -DBOOST_SPIRIT_THREADSAFE -DHAVE_BUILD_INFO -D__STDC_FORMAT_MACROS" \
      LIBS="-ldb_cxx-4.8 -lboost_system -lzmq" && \
    make -j$(nproc)

# Copy binaries to output directory
RUN mkdir -p /output && \
    cp src/clore_blockchaind /output/ && \
    cp src/clore-cli /output/ && \
    strip /output/*
