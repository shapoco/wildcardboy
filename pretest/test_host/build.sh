#!/bin/bash
# Builds and runs the host-side card_profile test with the system compiler.
set -eu
cd "$(dirname "$0")"
ROOT=../..
QCBOR=$ROOT/submodule/QCBOR
LCDTAP=$ROOT/submodule/lcdtap/lib
mkdir -p build
DEFS="-DQCBOR_DISABLE_FLOAT_HW_USE -DUSEFULBUF_DISABLE_ALL_FLOAT -DQCBOR_DISABLE_TAGS -DQCBOR_DISABLE_INDEFINITE_LENGTH_STRINGS"
for c in qcbor_decode qcbor_encode qcbor_err_to_str UsefulBuf ieee754; do
  gcc -std=c99 -O1 $DEFS -I$QCBOR/inc -c $QCBOR/src/$c.c -o build/$c.o
done
g++ -std=c++17 -O1 -Wall -Wextra $DEFS -I../src -I$QCBOR/inc -I$LCDTAP/include \
  profile_test.cpp ../src/card_profile.cpp ../src/crc32.cpp $LCDTAP/src/config.cpp \
  build/*.o -o build/profile_test
if [ $# -gt 0 ]; then
  ./build/profile_test "$@"
else
  ./build/profile_test ../../cards/TinyJoyPad/profile.hex ../../cards/PicoPad1/profile.hex ../../cards/ESPboy/profile.hex ../../cards/XiamoconRP/profile.hex ../../cards/PicoSystem/profile.hex
fi
