#!/bin/sh
# Stage 2 (after build-sysroot.sh): libgcc (static, with the unwinder), crtbegin.o/crtend.o and the C++ runtime
# (libsupc++ + libstdc++, static) for i486-pc-nto-qnx6.5.0. Programs link the C++ runtime statically, so they do
# not depend on the SDP's Dinkum libcpp.so.4 headers.
set -e
T=i486-pc-nto-qnx6.5.0
P=/opt/qnx-open
S=$P/$T/sysroot
L=$S/x86/lib
export PATH=$P/bin:$PATH
cd /work/src/b-gcc
if [ ! -f $P/lib/gcc/$T/4.4.2/libgcc.a ]; then
  make -j"$(nproc)" all-target-libgcc MAKEINFO=true
  make install-target-libgcc MAKEINFO=true
fi
LGCC=$P/lib/gcc/$T/4.4.2
# crtend.o from GCC's crtstuff.c (the nto target config only builds crtbegin.o)
$T-gcc -O2 -fno-omit-frame-pointer -fPIC -fno-asynchronous-unwind-tables -fno-exceptions -DCRT_END -DIN_GCC \
  -DCROSS_DIRECTORY_STRUCTURE -I. -I./gcc -I../gcc-4.4.2/gcc -I../gcc-4.4.2/include -I./$T/libgcc \
  -c ../gcc-4.4.2/gcc/crtstuff.c -o $L/crtend.o
cp $LGCC/crtbegin.o $L/crtbegin.o
# libstdc++ is configured on its own: the top-level GCC configure ran before the sysroot could link
# GCC 4.4 knows only QNX 6.1/6.2 here (with Dinkum ctype internals): use its generic OS layer instead
sed -i 's|os_include_dir="os/qnx/qnx6.1"|os_include_dir="os/generic"|; /os\/generic"$/{n;s|c_model=c|c_model=c_global|}' \
  /work/src/gcc-4.4.2/libstdc++-v3/configure.host
cd /work/src && rm -rf b-libstdcxx && mkdir b-libstdcxx && cd b-libstdcxx
../gcc-4.4.2/libstdc++-v3/configure --host=i486-pc-nto-qnx6.2 --build=x86_64-pc-linux-gnu --prefix=$P --libdir=$P/$T/lib \
  --with-gxx-include-dir=$P/$T/include/c++/4.4.2 --disable-shared --enable-static --disable-multilib \
  --disable-nls --disable-libstdcxx-pch --enable-threads=posix --enable-clocale=generic \
  --enable-cstdio=stdio --disable-wchar_t --disable-libstdcxx-time \
  CC=$T-gcc CXX=$T-g++ AR=$T-ar RANLIB=$T-ranlib CFLAGS=-O2 CXXFLAGS="-O2 -g0"
make -j"$(nproc)" MAKEINFO=true
make install MAKEINFO=true
# configured as qnx6.2 (see above): expose its target headers under the real triplet
ln -sfn i486-pc-nto-qnx6.2 $P/$T/include/c++/4.4.2/$T
echo RUNTIME_DONE
