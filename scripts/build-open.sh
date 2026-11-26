#!/bin/sh
# Build the head-unit programs with the open QNX 6.5.0 x86 toolchain (toolchain/README.md), no QNX SDP or VM.
# Same steps and flags as scripts/build-qnx.sh; started by scripts/build_open.py inside the toolchain container,
# in a folder prepared the same way (src/, reference/, mbedtls-3.6.7/).
# Differences to the SDP build: gcc/g++ instead of qcc, the C++ runtime (GNU libstdc++ 4.4.2) is linked
# statically instead of Dinkum libcpp.so.4, and popen() comes from libqnxopen.a.
# Programs: $PROGRAMS (default: supported programs and the stub-linked HMI plugin).
set -e
T=i486-pc-nto-qnx6.5.0
W=`pwd`
OUT=$W/out
A=$W/src/android_auto
PROGRAMS=${PROGRAMS:-"oneplus_native native_service decode_runner fifo_http nbt_knob nbt_hwvideo nbt_cluster nbt_key nbt_a2i_beep nbt_decoder nbt_reset libNbtAndroidAuto.so"}
CC="$T-gcc"
CXX="$T-g++ -O1 -Wall -D_QNX_SOURCE -std=gnu++0x"
LIBS="-lqnxopen"
AR=$T-ar
TLSINC="-I$W/mbedtls-3.6.7/include"
TLSLIBS="$W/mbedtls-3.6.7/library/libmbedtls.a $W/mbedtls-3.6.7/library/libmbedx509.a $W/mbedtls-3.6.7/library/libmbedcrypto.a"
AAINC="$TLSINC -I$W/reference/src -I$W/reference/include"
mkdir -p $OUT
want() { case " $PROGRAMS " in *" $1 "*) return 0;; esac; return 1; }

if want oneplus_native; then
  echo "== Mbed TLS 3.6.7 static libraries"
  if ! (cd mbedtls-3.6.7 && make lib CC="$CC" AR=$AR CFLAGS='-O2 -D_QNX_SOURCE -std=gnu99' >$W/mbedtls-build.log 2>&1); then
    tail -20 $W/mbedtls-build.log; exit 1
  fi
  echo "== NBT-AAD core: libnbt_aa_core.a"
  mkdir -p core
  for c in aa_session aa_cryptor audio/audio_acks sdr_builder crash_handler; do
    $CXX $AAINC -c reference/src/$c.cpp -o core/`basename $c`.o
  done
  rm -f libnbt_aa_core.a
  $AR rcs libnbt_aa_core.a core/aa_session.o core/aa_cryptor.o core/audio_acks.o core/sdr_builder.o core/crash_handler.o
  echo "== oneplus_native"
  cd $A
  $CC -Wall -Wextra -DNBT_AA_TLS_HANDSHAKE -DNBT_AA_CAPTURE -c oneplus_aoa.c -o $W/oneplus_aoa.o
  $CXX $AAINC aa_video_capture.cpp aa_media_out.cpp aa_mic.cpp tls_usb_probe.cpp \
    $W/oneplus_aoa.o $W/core/aa_cryptor.o $W/libnbt_aa_core.a \
    $TLSLIBS -lsocket -lm -lusbdi $LIBS -o $OUT/oneplus_native
  cd $W
fi
cd $A
if want native_service; then echo "== native_service"; $CC -O2 -Wall -Wextra nbt_native_service.c $LIBS -o $OUT/native_service; fi
if want decode_runner; then echo "== decode_runner"; $CC -O2 -Wall -Wextra nbt_decode_runner.c $LIBS -o $OUT/decode_runner; fi
if want fifo_http; then echo "== fifo_http"; $CC -Wall -Wextra nbt_fifo_http.c -lsocket $LIBS -o $OUT/fifo_http; fi
if want accessory_window; then echo "== accessory_window"; $CC -Wall -Wextra accessory_window.c $LIBS -o $OUT/accessory_window; fi
cd $A/knob
if want nbt_knob; then echo "== nbt_knob"; $CXX -Iref nbt_knob.cpp ref/dsi_client.cpp ref/most/client.cpp ref/car/ccs_input.cpp $LIBS -o $OUT/nbt_knob; fi
if want nbt_hwvideo; then echo "== nbt_hwvideo"; $CXX -Iref nbt_hwvideo.cpp ref/dsi_client.cpp ref/most/client.cpp $LIBS -o $OUT/nbt_hwvideo; fi
if want nbt_cluster; then echo "== nbt_cluster"; $CXX -Iref nbt_cluster.cpp ref/dsi_client.cpp ref/most/client.cpp -lm $LIBS -o $OUT/nbt_cluster; fi
if want nbt_key; then echo "== nbt_key"; $CXX -Iref nbt_key.cpp ref/dsi_client.cpp ref/most/client.cpp $LIBS -o $OUT/nbt_key; fi
cd $W
if want nbt_a2i_beep; then echo "== nbt_a2i_beep"; $CC -Wall $A/audio/nbt_a2i_beep.c -lm $LIBS -o $OUT/nbt_a2i_beep; fi
if want nbt_decoder; then
  echo "== nbt_decoder"
  FFCC="$CC" FFWC= FFAR="$AR" FFCACHE="$W/ffmpeg-cache" sh build-ffmpeg-qnx.sh
  $CC -O2 -Wall -Wextra -std=gnu99 -I$W/ffmpeg-lib/include -I$W/ffmpeg $A/decoder/nbt_decoder.c \
    $W/ffmpeg-lib/libavcodec.a $W/ffmpeg-lib/libavutil.a -lm $LIBS -o $OUT/nbt_decoder
fi
if want nbt_reset; then echo "== nbt_reset"; $CC -O2 -Wall -Wextra src/idrive/reboot/nbt_reset.c -lsocket $LIBS -o $OUT/nbt_reset; fi
# HMI plugin libNbtAndroidAuto.so, without the NBT's HMI libraries: it is linked against STUB libraries that only carry
# the names (SONAME) and the symbols it imports (libAppFramework.A.so, libCoreFramework.A.so, Dinkum libcpp-ne.so.4).
# The result has the same NEEDED list as the SDP-built plugin, so on the car HmiMain3D's real libraries resolve every
# symbol; nothing of the stubs is ever loaded. -z defs: any import missing from the stubs fails the build.
if want libNbtAndroidAuto.so; then
  echo "== HMI plugin libNbtAndroidAuto.so (stub-linked)"
  S=$W/hmi-stubs
  mkdir -p $S
  cat > $S/app.c <<'EOS'
void _ZN3bmw11BasicStringIcEC1EPKc(void) {}
void _ZN3bmw11BasicStringIcED1Ev(void) {}
void _ZN3bmw9PluginApp10addHandlerEjjRKNS_8DelegateIbRKNS_6ParcelEvvvEE(void) {}
void _ZN3bmw9PluginApp11updateModelEjRKNS_6TextIDE(void) {}
void _ZN3bmw9PluginApp11updateModelEji(void) {}
void _ZN3bmw9PluginApp5startEv(void) {}
void _ZN3bmw9PluginApp12triggerEventEj(void) {}
void _ZN3bmw9PluginApp4postEPNS_8RunnableEj(void) {}
void _ZN3bmw9PluginAppC1ERNS_11IDispatcherENS_11BasicStringIcEERKNS_6BundleES7_(void) {}
void _ZN3bmw9PluginAppD1Ev(void) {}
void _ZNK3bmw6Parcel8readBoolERb(void) {}
void _ZN3bmw9PluginApp33cdsAddPropertyChangedEventHandlerERNS_11BasicStringIcEEjRKNS_8DelegateIbRKNS1_ItEEvvvEE(void) {}
EOS
  echo 'void _ZN3bmw6TextIDC1Ej(void) {}' > $S/core.c
  printf 'void _Znwj(void) {}\nvoid _ZdlPv(void) {}\nvoid __cxa_pure_virtual(void) {}\n' > $S/cpp.c
  $CC -shared -fPIC -nostdlib -Wl,-soname,libAppFramework.A.so $S/app.c -o $S/libAppFramework.A.so
  $CC -shared -fPIC -nostdlib -Wl,-soname,libCoreFramework.A.so $S/core.c -o $S/libCoreFramework.A.so
  $CC -shared -fPIC -nostdlib -Wl,-soname,libcpp-ne.so.4 $S/cpp.c -o $S/libcpp-ne.so.4
  cd $W/src/idrive
  $T-g++ -O2 -Wall -Wextra -D_QNX_SOURCE -DNBT_OPEN_TOOLCHAIN -shared -fPIC -fno-exceptions -fno-rtti -nodefaultlibs -Wl,-z,defs \
    aa_entry_plugin.cpp -o $OUT/libNbtAndroidAuto.so -L$S -lAppFramework.A -lCoreFramework.A $S/libcpp-ne.so.4 -lc -lgcc
  cd $W
fi
echo "== built"
ls -l $OUT
echo BUILD_OPEN_DONE
