# Host for building the open QNX 6.5.0 x86 cross toolchain (GNU binutils + GCC 4.4.2).
# Debian 8 ships GCC 4.9, which still compiles GCC 4.4.2 cleanly.
FROM debian/eol:jessie
RUN apt-get update && apt-get install -y --no-install-recommends --force-yes \
      build-essential bison flex m4 libgmp-dev libmpfr-dev wget xz-utils bzip2 file python python3 \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /work
