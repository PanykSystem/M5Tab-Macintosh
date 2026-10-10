#!/bin/bash
# Translate hot ROM code and write src/basilisk/aot/aot_rom_generated.cpp.
#
#   tools/aot/run_translate.sh PATCHED_ROM [aot_translate.py options]
#
# Uses the hot list tools/aot/hot_q650.txt unless --hot options are given.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
BUILD="${AOT_BUILD_DIR:-$ROOT/.pio/aot-tools}"
ROM="$1"
shift
mkdir -p "$BUILD"
if [ ! -x "$BUILD/dump_optable" ] || [ "$HERE/dump_optable.cpp" -nt "$BUILD/dump_optable" ]; then
  c++ -w -I "$ROOT/src/basilisk/uae_cpu" -I "$ROOT/tools/cpu_gen" -o "$BUILD/dump_optable" \
    "$HERE/dump_optable.cpp" "$ROOT/src/basilisk/uae_cpu/readcpu.cpp" \
    "$ROOT/src/basilisk/uae_cpu/generated/cpudefs.cpp"
fi
"$BUILD/dump_optable" > "$BUILD/optable.tsv"
SOURCE=(--hot "$HERE/hot_q650.txt")
for argument in "$@"; do
  if [ "$argument" = "--hot" ]; then SOURCE=(); fi
done
python3 "$HERE/aot_translate.py" --rom "$ROM" --optable "$BUILD/optable.tsv" "${SOURCE[@]}" \
  --out "${AOT_OUT:-$ROOT/src/basilisk/aot/aot_rom_generated.cpp}" --report "$BUILD/report.json" "$@"
