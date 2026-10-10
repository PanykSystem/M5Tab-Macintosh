#!/bin/bash
# Build the host differential test for ahead-of-time translated ROM code.
#
#   test/aot_host/build.sh BUILD_DIR GENERATED.cpp
#
# Compiles the real interpreter (newcpu.cpp and the generated handlers), the
# AOT runtime, the generated region code and aot_diff.cpp with the host
# compiler. Objects are cached in BUILD_DIR by source modification time.
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$1"
GENERATED="$2"
CXX="${CXX:-clang++}"
OPT="${AOT_HOST_OPT:--O1}"
mkdir -p "$OUT"
# The 68k handlers rely on two's-complement wraparound (see platformio.ini).
FLAGS=(-std=gnu++17 -w $OPT -fwrapv -include assert.h
  -DEMULATED_68K=1 -DREAL_ADDRESSING=0 -DDIRECT_ADDRESSING=0 -DSAVE_MEMORY_BANKS=1
  -DFPU_IEEE=1 -DFPU_UAE=0 -DFPU_X86=0 -DUSE_JIT=0
  -DCPU_COMPACT_DISPATCH=0 -DCPU_TRACE_CACHE=0 -DCPU_NATIVE_QD_ACCEL=0
  -DCPU_NATIVE_BLOCK_MOVE=1 -DCPU_NATIVE_IS_LAYER=0 -DCPU_TRAP_PROFILE=0
  -DFLIGHT_RECORDER=0 -DENABLE_MON=0 -DAOT_HOST_TEST=1
  -DSIZEOF_SHORT=2 -DSIZEOF_INT=4 -DSIZEOF_LONG=8 -DSIZEOF_LONG_LONG=8 -DSIZEOF_VOID_P=8
  -I "$ROOT/test/aot_host/stubs" -I "$ROOT/src/basilisk" -I "$ROOT/src/basilisk/include"
  -I "$ROOT/src/basilisk/uae_cpu" -I "$ROOT/src/basilisk/uae_cpu/fpu"
  -I "$ROOT/src/basilisk/uae_cpu/generated" -I "$ROOT/src/basilisk/aot")
SOURCES=(
  "$ROOT/src/basilisk/uae_cpu/newcpu.cpp"
  "$ROOT/src/basilisk/uae_cpu/generated/cpuemu.cpp"
  "$ROOT/src/basilisk/uae_cpu/generated/cpustbl.cpp"
  "$ROOT/src/basilisk/uae_cpu/generated/cpudefs.cpp"
  "$ROOT/src/basilisk/uae_cpu/readcpu.cpp"
  "$ROOT/src/basilisk/aot/aot_runtime.cpp"
  "$ROOT/src/basilisk/aot/aot_selftest.cpp"
  "$ROOT/test/aot_host/host_env.cpp"
  "$ROOT/test/aot_host/aot_diff.cpp"
  "$GENERATED"
)
OBJECTS=()
PIDS=()
for source in "${SOURCES[@]}"; do
  object="$OUT/$(basename "$source" .cpp).o"
  OBJECTS+=("$object")
  if [ ! -f "$object" ] || [ "$source" -nt "$object" ] || \
     [ -n "$(find "$ROOT/src/basilisk/aot" "$ROOT/test/aot_host" -name '*.h' -newer "$object" 2>/dev/null)" ]; then
    "$CXX" "${FLAGS[@]}" -c "$source" -o "$object" &
    PIDS+=($!)
  fi
done
for pid in "${PIDS[@]}"; do wait "$pid"; done
"$CXX" "${OBJECTS[@]}" -o "$OUT/aot_diff"
echo "$OUT/aot_diff"
