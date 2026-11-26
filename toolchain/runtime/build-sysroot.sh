#!/bin/sh
# Assemble the open QNX 6.5.0 x86 sysroot inside the toolchain container (after build-toolchain.sh stage 1).
#   headers   -> $S/usr/include                (toolchain/sysroot, written for this project)
#   crt*.o, link-only stubs of libc.so.3 / libm.so.2 / libsocket.so.3 / libusbdi.so.2 -> $S/x86/lib
#   libqnxopen.a (popen/pclose, static-only on QNX) -> $S/x86/lib
set -e
T=i486-pc-nto-qnx6.5.0
P=/opt/qnx-open
S=$P/$T/sysroot
L=$S/x86/lib
TC=/work/tc
export PATH=$P/bin:$PATH
mkdir -p $S/usr $L $S/x86/usr/lib
# GCC's nto config appends the suffix "x86" to the sysroot without a slash: libraries are searched in sysrootx86/
ln -sfn sysroot/x86 $P/$T/sysrootx86
rm -rf $S/usr/include && cp -R $TC/sysroot/usr/include $S/usr/include
W=/tmp/sysroot-build; rm -rf $W; mkdir -p $W
for f in crt1 crti crtn; do $T-gcc -c $TC/runtime/$f.S -o $L/$f.o; done
# placeholders until libgcc provides the real ones (stage 2)
[ -f $L/crtbegin.o ] || $T-gcc -x c -c /dev/null -o $L/crtbegin.o
[ -f $L/crtend.o ] || $T-gcc -x c -c /dev/null -o $L/crtend.o
python3 $TC/runtime/mkstub.py $S/usr/include $W
stub() { # name soname [extra ld args]
  $T-gcc -O0 -fPIC -fno-builtin -w -c $W/$1.c -o $W/$1.o
  $T-ld -shared -soname $2 -o $L/$2 $W/$1.o $3
  ln -sf $2 $L/$1.so
}
stub libc libc.so.3
stub libm libm.so.2
stub libsocket libsocket.so.3 "--version-script=$W/libsocket.map"
stub libusbdi libusbdi.so.2
$T-gcc -O2 -Wall -c $TC/runtime/popen.c -o $W/popen.o
rm -f $L/libqnxopen.a && $T-ar rcs $L/libqnxopen.a $W/popen.o
# QNX's GCC predefines __unix__/__unix (upstream nto.h only asserts system=unix); add them through a specs file
SPECS=$P/lib/gcc/$T/4.4.2/specs
rm -f $SPECS && $T-gcc -dumpspecs > /tmp/specs
# Link with --exclude-libs,ALL: the static libgcc unwinder (and static libstdc++) must stay private to each program.
# Without it the executable exports __register_frame_info, and BMW's C++ libraries that a program dlopens
# (libsys_dsi_colibry, libwal_nvidia, libcpp.so.4) would register their exception tables with our copy instead of the
# system's. SDP-built executables export no such symbols.
awk '{print} /^\*cpp:$/{getline; print $0 " -D__unix__ -D__unix"} /^\*link:$/{getline; print $0 " --exclude-libs ALL"}' /tmp/specs > $SPECS
echo SYSROOT_DONE
