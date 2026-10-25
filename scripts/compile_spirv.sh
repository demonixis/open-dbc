#!/usr/bin/env sh
set -eu

SCRIPT_DIR="$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)"
SRC_DIR="${1:-$SCRIPT_DIR/../shaders}"
OUT_DIR="${2:-$SCRIPT_DIR/../build/shaders}"

mkdir -p "$OUT_DIR"
for SHADER in dbclassic_tri vr_transfer; do
  for STAGE in vert frag; do
    glslangValidator -V --target-env vulkan1.0 -S "$STAGE" \
      "$SRC_DIR/$SHADER.$STAGE.glsl" -o "$OUT_DIR/$SHADER.$STAGE.spv"
  done
done
