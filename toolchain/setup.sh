#!/bin/sh
# One-time setup of the open QNX 6.5.0 x86 toolchain (GCC 4.4.2), no QNX SDP, licence or VM needed.
# Needs Docker on Linux or WSL. Takes about 15-30 minutes.
# Result: build/qnx-open (compilers + sysroot), used by toolchain/qnx-open and scripts/build_open.py.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC=$ROOT/build/toolchain-src
mkdir -p "$SRC" "$ROOT/build/qnx-open"
if ! command -v sha256sum >/dev/null 2>&1; then
  echo "SHA-256 verification requires sha256sum" >&2
  exit 1
fi
fetch() { # url sha256
  f=$SRC/$(basename "$1")
  [ -f "$f" ] || curl -fL --retry 3 -o "$f" "$1"
  echo "$2  $f" | sha256sum -c - >/dev/null || { echo "SHA-256 mismatch: $f"; exit 1; }
}
fetch https://ftp.gnu.org/gnu/binutils/binutils-2.20.1.tar.bz2 71d37c96451333c5c0b84b170169fdcb138bbb27397dc06281905d9717c8ed64
fetch https://ftp.gnu.org/gnu/gcc/gcc-4.4.2/gcc-4.4.2.tar.bz2 1126b6a7b585b3a178bfb7d559221779eb7eba12ce01bc9ca5896f6f9ee639de
docker build --platform linux/amd64 -t qnx-open-host "$ROOT/toolchain"
Q=$ROOT/toolchain/qnx-open
echo "== 1/3 binutils + gcc 4.4.2";   $Q sh /work/tc/build-toolchain.sh   > "$ROOT/build/toolchain-stage1.log" 2>&1 || { tail -30 "$ROOT/build/toolchain-stage1.log"; exit 1; }
echo "== 2/3 sysroot";                $Q sh /work/tc/runtime/build-sysroot.sh
echo "== 3/3 libgcc + libstdc++";     $Q sh /work/tc/build-runtime.sh     > "$ROOT/build/toolchain-stage2.log" 2>&1 || { tail -30 "$ROOT/build/toolchain-stage2.log"; exit 1; }
$Q sh /work/tc/runtime/build-sysroot.sh >/dev/null   # refresh crtbegin/crtend links and the specs file
$Q i486-pc-nto-qnx6.5.0-gcc -O2 -o /repo/build/hello_nbt /repo/src/hello_nbt.c
echo "toolchain ready: build/qnx-open ($(du -sh "$ROOT/build/qnx-open" | cut -f1)); test program build/hello_nbt"
