#!/usr/bin/env bash
set -euo pipefail

strict=0
binary=""
linux_system_dependency() {
    local dependency="$1"
    local name="${dependency##*/}"
    case "$name" in
        linux-vdso.so.*|ld-linux*.so*|ld-musl-*.so*|\
        libc.so.*|libm.so.*|libdl.so.*|libpthread.so.*|librt.so.*|libutil.so.*|libresolv.so.*|\
        libgcc_s.so.*|libstdc++.so.*|libatomic.so.*|\
        libX11.so.*|libXau.so.*|libXdmcp.so.*|libXext.so.*|libXcursor.so.*|libXi.so.*|\
        libXfixes.so.*|libXrandr.so.*|libXrender.so.*|libXss.so.*|libxcb.so.*|\
        libwayland-client.so.*|libwayland-cursor.so.*|libwayland-egl.so.*|\
        libxkbcommon*.so.*|libdecor-*.so.*|libasound.so.*|libpulse*.so.*|libudev.so.*|\
        libdbus-*.so.*|libGL*.so.*|libEGL.so.*|libvulkan.so.*|\
        libdrm.so.*|libgbm.so.*)
            return 0 ;;
        *) return 1 ;;
    esac
}

while (($#)); do
    case "$1" in
        --strict) strict=1 ;;
        --binary)
            shift
            (($#)) || { echo "missing value for --binary" >&2; exit 2; }
            binary="$1"
            ;;
        *)
            if [[ -n "$binary" ]]; then
                echo "unexpected argument: $1" >&2
                exit 2
            fi
            binary="$1"
            ;;
    esac
    shift
done

if [[ -z "$binary" ]]; then
    if [[ -x build/default/tools/studio/opendbc.app/Contents/MacOS/opendbc ]]; then
        binary=build/default/tools/studio/opendbc.app/Contents/MacOS/opendbc
    elif [[ -x build/default/tools/studio/opendbc ]]; then
        binary=build/default/tools/studio/opendbc
    else
        echo "OpenDBC executable not found; pass --binary <path>" >&2
        exit 2
    fi
fi
if [[ ! -f "$binary" ]]; then
    echo "not a file: $binary" >&2
    exit 2
fi

violations=()
if [[ "$(uname -s)" == Darwin ]]; then
    if ! header_report=$(otool -hv "$binary" 2>&1); then
        printf 'otool -hv failed for %s:\n%s\n' "$binary" "$header_report" >&2
        exit 2
    fi
    if ! grep -Eq '(^|[[:space:]])EXECUTE([[:space:]]|$)' <<<"$header_report"; then
        printf 'not a Mach-O executable: %s\n%s\n' "$binary" "$header_report" >&2
        exit 2
    fi
    if ! dependency_report=$(otool -L "$binary" 2>&1); then
        printf 'otool -L failed for %s:\n%s\n' "$binary" "$dependency_report" >&2
        exit 2
    fi
    while IFS= read -r dependency; do
        [[ -z "$dependency" ]] && continue
        printf '%s\n' "$dependency"
        case "$dependency" in
            /usr/lib/*|/System/Library/*) ;;
            *) violations+=("dependency: $dependency") ;;
        esac
    done < <(printf '%s\n' "$dependency_report" | tail -n +2 |
        sed -E 's/^[[:space:]]*([^[:space:]]+).*/\1/')

    if ! load_command_report=$(otool -l "$binary" 2>&1); then
        printf 'otool -l failed for %s:\n%s\n' "$binary" "$load_command_report" >&2
        exit 2
    fi
    while IFS= read -r rpath; do
        [[ -z "$rpath" ]] && continue
        printf 'rpath: %s\n' "$rpath"
        case "$rpath" in
            @executable_path*|@loader_path*) ;;
            *) violations+=("rpath: $rpath") ;;
        esac
    done < <(printf '%s\n' "$load_command_report" |
        awk '/cmd LC_RPATH/{getline; getline; print $2}')
elif command -v ldd >/dev/null 2>&1; then
    if ! dependency_report=$(ldd "$binary" 2>&1); then
        printf 'ldd failed for %s:\n%s\n' "$binary" "$dependency_report" >&2
        exit 2
    fi
    while IFS= read -r dependency; do
        [[ -z "$dependency" ]] && continue
        printf '%s\n' "$dependency"
        if [[ "$dependency" == *"not found"* ]]; then
            violations+=("dependency: $dependency")
            continue
        fi
        if ! linux_system_dependency "$dependency"; then
            violations+=("dependency: $dependency")
            continue
        fi
        case "$dependency" in
            linux-vdso.so.*|/lib/*|/lib64/*|/usr/lib/*) ;;
            *) violations+=("non-system path: $dependency") ;;
        esac
    done < <(printf '%s\n' "$dependency_report" | awk '
        /=>/ { if ($3 == "not") print $1 " => not found"; else print $3; next }
        /^[[:space:]]*\// { print $1 }
        /^[[:space:]]*linux-vdso/ { print $1 }
    ')

    if ! command -v readelf >/dev/null 2>&1; then
        echo "readelf is required for the Linux relocation gate" >&2
        exit 2
    fi
    if ! dynamic_report=$(readelf -d "$binary" 2>&1); then
        printf 'readelf -d failed for %s:\n%s\n' "$binary" "$dynamic_report" >&2
        exit 2
    fi
    while IFS= read -r rpath; do
        [[ -z "$rpath" ]] && continue
        printf 'rpath: %s\n' "$rpath"
        case "$rpath" in
            "\$ORIGIN"*) ;;
            *) violations+=("rpath: $rpath") ;;
        esac
    done < <(printf '%s\n' "$dynamic_report" | awk '
        /\((RPATH|RUNPATH)\)/ { value=$NF; gsub(/[][]/, "", value); print value }
    ' | tr ':' '\n')
else
    echo "unsupported dependency inspector on this platform" >&2
    exit 2
fi

if ((${#violations[@]})); then
    printf 'non-system dynamic linkage detected:\n' >&2
    printf '  %s\n' "${violations[@]}" >&2
    ((strict)) && exit 1
fi

printf 'OpenDBC static-link gate passed%s.\n' "$([[ $strict == 1 ]] && printf ' (strict)' || true)"
