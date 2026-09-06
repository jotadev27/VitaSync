#!/usr/bin/env bash
#
# Cross-compiles VitaSync for Windows (MinGW64) and lays out a
# self-contained folder next to the .exe: the Qt DLLs it needs, the
# platform/image-format plugins, the QML modules it imports, and a qt.conf
# so it runs by double-clicking, no installer, no PATH changes.
#
# Needs a MinGW64 Qt6 cross-toolchain, e.g. on Fedora:
#   sudo dnf install mingw64-gcc-c++ mingw64-qt6-qtbase \
#       mingw64-qt6-qtdeclarative mingw64-qt6-qttools mingw64-zlib
#
# Usage: tools/make_portable_windows.sh [output-dir]

set -euo pipefail

SOURCE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUTPUT_DIR="${1:-$SOURCE_DIR/installer/windows-portable}"
BUILD_DIR="$SOURCE_DIR/build-windows"

MINGW_SYSROOT="/usr/x86_64-w64-mingw32/sys-root/mingw"
MINGW_BIN="$MINGW_SYSROOT/bin"
MINGW_QT_PLUGINS="$MINGW_SYSROOT/lib/qt6/plugins"
MINGW_QT_QML="$MINGW_SYSROOT/lib/qt6/qml"
TOOLCHAIN_FILE="$MINGW_SYSROOT/lib/cmake/Qt6/qt.toolchain.cmake"

# System DLLs Windows itself provides -- never bundled.
is_system_dll() {
    case "$(printf '%s' "$1" | tr '[:upper:]' '[:lower:]')" in
        kernel32.dll|user32.dll|gdi32.dll|shell32.dll|advapi32.dll|\
        ole32.dll|oleaut32.dll|ws2_32.dll|msvcrt.dll|ntdll.dll|\
        comctl32.dll|comdlg32.dll|winmm.dll|imm32.dll|shlwapi.dll|\
        version.dll|dwmapi.dll|uxtheme.dll|setupapi.dll|crypt32.dll|\
        secur32.dll|netapi32.dll|userenv.dll|iphlpapi.dll|dnsapi.dll|\
        mswsock.dll|d3d9.dll|d3d11.dll|dxgi.dll|opengl32.dll|glu32.dll|\
        bcrypt.dll|ncrypt.dll|api-ms-win-*|shcore.dll|propsys.dll)
            return 0 ;;
    esac
    return 1
}

log() { printf '  %s\n' "$*"; }

printf '\n== Cross-compiling for Windows (MinGW64) ==\n'
cmake -S "$SOURCE_DIR" -B "$BUILD_DIR" -G Ninja \
      -DCMAKE_BUILD_TYPE=Release -DVSP_BUILD_TESTS=OFF \
      -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" >/dev/null
cmake --build "$BUILD_DIR" >/dev/null
EXE="$(find "$BUILD_DIR" -maxdepth 2 -iname 'vitasync.exe' | head -1)"
log "built $EXE"

printf '\n== Laying out the bundle ==\n'
rm -rf "$OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR/assets/covers"
cp "$EXE" "$OUTPUT_DIR/"
cp "$SOURCE_DIR/assets/covers/titles.json" "$OUTPUT_DIR/assets/covers/"
log "binary and offline title database in place"

# --- Qt plugins ------------------------------------------------------------
# Same set the Linux bundle ships, minus anything X11/Wayland-specific that
# has no meaning on Windows.
for category in platforms imageformats iconengines; do
    if [ -d "$MINGW_QT_PLUGINS/$category" ]; then
        mkdir -p "$OUTPUT_DIR/$category"
        cp -a "$MINGW_QT_PLUGINS/$category/." "$OUTPUT_DIR/$category/"
    fi
done
# Only the real platform plugin is needed at runtime; qdirect2d and qminimal
# are alternate backends this app never selects.
rm -f "$OUTPUT_DIR/platforms/qdirect2d.dll" "$OUTPUT_DIR/platforms/qminimal.dll"
rm -f "$OUTPUT_DIR"/imageformats/qsvg.dll.debug 2>/dev/null || true
log "plugins: $(find "$OUTPUT_DIR" -maxdepth 2 -iname '*.dll' | wc -l) files"

# --- QML modules -------------------------------------------------------------
# Mirrors make_portable_linux.sh's selection: the app's own QML is compiled
# into the binary, these are only the Qt-owned modules it imports.
mkdir -p "$OUTPUT_DIR/qml"
for module in QtQml QtCore; do
    if [ -d "$MINGW_QT_QML/$module" ]; then
        mkdir -p "$OUTPUT_DIR/qml/$module"
        cp -a "$MINGW_QT_QML/$module/." "$OUTPUT_DIR/qml/$module/"
    fi
done

mkdir -p "$OUTPUT_DIR/qml/QtQuick"
find "$MINGW_QT_QML/QtQuick" -maxdepth 1 -type f -exec cp -a {} "$OUTPUT_DIR/qml/QtQuick/" \;
for submodule in Window Shapes Dialogs Templates Layouts Effects; do
    if [ -d "$MINGW_QT_QML/QtQuick/$submodule" ]; then
        mkdir -p "$OUTPUT_DIR/qml/QtQuick/$submodule"
        cp -a "$MINGW_QT_QML/QtQuick/$submodule/." "$OUTPUT_DIR/qml/QtQuick/$submodule/"
    fi
done

mkdir -p "$OUTPUT_DIR/qml/QtQuick/Controls"
find "$MINGW_QT_QML/QtQuick/Controls" -maxdepth 1 -type f \
     -exec cp -a {} "$OUTPUT_DIR/qml/QtQuick/Controls/" \;
for part in Basic impl; do
    if [ -d "$MINGW_QT_QML/QtQuick/Controls/$part" ]; then
        mkdir -p "$OUTPUT_DIR/qml/QtQuick/Controls/$part"
        cp -a "$MINGW_QT_QML/QtQuick/Controls/$part/." \
              "$OUTPUT_DIR/qml/QtQuick/Controls/$part/"
    fi
done
cp -a "$MINGW_QT_QML"/*.qmltypes "$OUTPUT_DIR/qml/" 2>/dev/null || true
log "qml modules: $(du -sh "$OUTPUT_DIR/qml" | cut -f1)"

# --- DLL dependencies --------------------------------------------------------
# Walk the PE import table from the binary and every plugin/QML DLL just
# copied, the same dependency-closure approach the Linux script uses with
# ldd, but read via objdump since these are PE binaries.
collect_dependencies() {
    local -a queue=("$@")
    local -A seen=()
    local item name

    while [ ${#queue[@]} -gt 0 ]; do
        item="${queue[0]}"
        queue=("${queue[@]:1}")
        [ -n "${seen[$item]:-}" ] && continue
        seen[$item]=1

        while read -r name; do
            [ -z "$name" ] && continue
            is_system_dll "$name" && continue

            local src="$MINGW_BIN/$name"
            [ -e "$src" ] || continue

            local target="$OUTPUT_DIR/$name"
            if [ ! -e "$target" ]; then
                cp -L "$src" "$target"
                queue+=("$target")
            fi
        done < <(x86_64-w64-mingw32-objdump -p "$item" 2>/dev/null \
                  | awk '/DLL Name:/ {print $3}')
    done
}

mapfile -t scan_targets < <(
    printf '%s\n' "$OUTPUT_DIR/vitasync.exe"
    find "$OUTPUT_DIR" -iname '*.dll'
)
collect_dependencies "${scan_targets[@]}"
log "runtime DLLs: $(find "$OUTPUT_DIR" -maxdepth 1 -iname '*.dll' | wc -l) files, $(du -sh "$OUTPUT_DIR" | cut -f1)"

# --- qt.conf ------------------------------------------------------------
# Tells Qt where to find its plugins and QML modules relative to the exe,
# so the app runs by double-clicking with no launcher script and no
# installer-side registry/env setup.
cat > "$OUTPUT_DIR/qt.conf" <<'QTCONF'
[Paths]
Prefix = .
Plugins = .
Imports = qml
Qml2Imports = qml
QTCONF

printf '\n== Done ==\n'
log "$OUTPUT_DIR/vitasync.exe"
log "total: $(du -sh "$OUTPUT_DIR" | cut -f1)"
