#!/bin/sh
# Stage 1: GNU binutils 2.20.1 and the GCC 4.4.2 compilers (C, C++) for i486-pc-nto-qnx6.5.0, from the official
# GNU releases (downloaded and checked by toolchain/setup.sh) plus toolchain/patches. Runs inside the toolchain
# container; installs to /opt/qnx-open (build/qnx-open). libgcc and libstdc++ follow in build-runtime.sh, after
# runtime/build-sysroot.sh has made the sysroot linkable.
set -e
T=i486-pc-nto-qnx6.5.0
P=/opt/qnx-open
SRC=/work/src
mkdir -p $SRC && cd $SRC
[ -d binutils-2.20.1 ] || tar xjf binutils-2.20.1.tar.bz2
if [ ! -d gcc-4.4.2 ]; then
  tar xjf gcc-4.4.2.tar.bz2
  for p in /work/tc/patches/gcc-4.4.2-*.patch; do (cd gcc-4.4.2 && patch -p1 < $p); done
fi

if [ ! -x $P/bin/$T-ld ]; then
  rm -rf b-binutils && mkdir b-binutils && cd b-binutils
  ../binutils-2.20.1/configure --target=$T --prefix=$P --with-sysroot=$P/$T/sysroot --disable-nls --disable-werror MAKEINFO=true
  make -j"$(nproc)" MAKEINFO=true && make install MAKEINFO=true
  cd ..
fi

mkdir -p $P/$T/sysroot/usr/include
rm -rf b-gcc && mkdir b-gcc && cd b-gcc
../gcc-4.4.2/configure --target=$T --prefix=$P --with-sysroot=$P/$T/sysroot \
  --enable-languages=c,c++ --disable-nls --disable-libssp --disable-libgomp --disable-libmudflap \
  --disable-shared --enable-threads=posix --disable-multilib --with-arch=i486 --with-tune=i686 \
  --disable-decimal-float --disable-libstdcxx-pch MAKEINFO=true \
  CFLAGS="-O2 -fgnu89-inline" CXXFLAGS="-O2"
make -j"$(nproc)" all-gcc MAKEINFO=true
make install-gcc MAKEINFO=true
echo TOOLCHAIN_STAGE1_DONE
