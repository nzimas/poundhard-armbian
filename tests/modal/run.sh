#!/bin/sh
# Build and run the PhModal offline checks natively (the same core the UGen compiles).
#   ./tests/modal/run.sh            -> host build
#   CXX=aarch64-...  ./tests/modal/run.sh   -> any other compiler
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"; P="$HERE/../../supercollider/plugins/PhModal"
OUT="${OUT:-$HERE/out}"; mkdir -p "$OUT"
${CXX:-c++} -std=c++17 -O3 -DMODAL_NUM_TYPE=float -I"$P/vendor/modal-synth/include" -I"$P" \
    "$HERE/test_phmodal.cpp" "$P/phmodal_core.cpp" -o "$OUT/test_phmodal" ${LDFLAGS:-}
OUT="$OUT" "$OUT/test_phmodal"
