#!/bin/bash

# Path configuration
RES_DIR="../resources"
EDITOR_SRC="$RES_DIR/editor.png"

# Check requirements
if ! command -v magick &> /dev/null; then
    echo "Error: ImageMagick (magick) not found."
    exit 1
fi

# 1. Windows (.ico) generation
echo "Generating Windows icons..."
magick "$EDITOR_SRC" -define icon:auto-resize=256,128,64,48,32,16 "$RES_DIR/editor.ico"
echo "Windows icons created in $RES_DIR"

# 2. macOS (.icns) generation
if [[ "$OSTYPE" == "darwin"* ]]; then
    echo "Generating macOS icons..."

    generate_icns() {
        local src=$1
        local out_name=$2
        local iconset="$RES_DIR/${out_name}.iconset"

        mkdir -p "$iconset"

        sips -z 16 16     "$src" --out "$iconset/icon_16x16.png" > /dev/null
        sips -z 32 32     "$src" --out "$iconset/icon_16x16@2x.png" > /dev/null
        sips -z 32 32     "$src" --out "$iconset/icon_32x32.png" > /dev/null
        sips -z 64 64     "$src" --out "$iconset/icon_32x32@2x.png" > /dev/null
        sips -z 128 128   "$src" --out "$iconset/icon_128x128.png" > /dev/null
        sips -z 256 256   "$src" --out "$iconset/icon_128x128@2x.png" > /dev/null
        sips -z 256 256   "$src" --out "$iconset/icon_256x256.png" > /dev/null
        sips -z 512 512   "$src" --out "$iconset/icon_256x256@2x.png" > /dev/null
        sips -z 512 512   "$src" --out "$iconset/icon_512x512.png" > /dev/null
        sips -z 1024 1024 "$src" --out "$iconset/icon_512x512@2x.png" > /dev/null

        iconutil -c icns "$iconset" -o "$RES_DIR/${out_name}.icns"
        rm -rf "$iconset"
    }

    generate_icns "$EDITOR_SRC" "editor"
    echo "macOS icons created in $RES_DIR"
else
    echo "Skipping .icns generation (macOS required)."
fi

echo "Done."
