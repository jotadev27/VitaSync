#!/usr/bin/env bash
#
# Builds a self-contained Linux folder that runs without installing anything:
# the binary, the Qt libraries and plugins it needs, the QML modules it imports
# and the offline title database, plus a launcher that points Qt at all of it.
#
# Low-level system libraries are deliberately NOT bundled. glibc, libstdc++ and
# the graphics stack have to match the machine actually running the program;
# shipping our copies of those is the classic way to make a "portable" build
# fail on exactly the machines it was meant to help.
#
# Usage: tools/make_portable_linux.sh [output-dir]

set -euo pipefail

SOURCE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUTPUT_DIR="${1:-$SOURCE_DIR/manual-testing/linux}"
BUILD_DIR="$SOURCE_DIR/build-portable"

QT_LIB_DIR="$(qmake6 -query QT_INSTALL_LIBS)"
QT_PLUGIN_DIR="$(qmake6 -query QT_INSTALL_PLUGINS)"
QT_QML_DIR="$(qmake6 -query QT_INSTALL_QML)"

# Provided by the host, never by us.
#
# Beyond the usual glibc/graphics set, the media codec stack is excluded too.
# Bundling FFmpeg and every codec it links would more than double the download
# for one feature -- a preview frame from a dropped video -- and every Linux
# desktop already has those libraries. Where they are missing, Qt simply fails
# to load its media backend and videos fall back to a drawn plate, which the
# app handles as a normal outcome rather than an error.
is_system_library() {
    case "$1" in
        ld-linux*|libc.so*|libm.so*|libdl.so*|libpthread.so*|librt.so*|\
        libstdc++.so*|libgcc_s.so*|libresolv.so*|libutil.so*|\
        libGL.so*|libEGL.so*|libGLX.so*|libGLdispatch.so*|libOpenGL.so*|\
        libdrm.so*|libgbm.so*|libglapi.so*|\
        libX11*|libxcb*|libXau*|libXdmcp*|libXext*|libXrender*|libXi*|\
        libXfixes*|libXcursor*|libXrandr*|libXinerama*|libSM*|libICE*|\
        libwayland*|libxkbcommon*|\
        libglib-2.0*|libgobject-2.0*|libgio-2.0*|libgmodule-2.0*|libffi*|\
        libdbus-1*|libsystemd*|libudev*|\
        libfontconfig*|libfreetype*|libz.so*|libbz2*|libexpat*|\
        libselinux*|libpcre*|liblzma*|libcap*|libgcrypt*|libgpg-error*|\
        \
        libav*|libsw*|libpostproc*|libx264*|libx265*|libvpx*|libaom*|libdav1d*|\
        libSvtAv1*|libopenh264*|libvorbis*|libogg*|libopus*|libtheora*|\
        libmp3lame*|libspeex*|libgsm*|libopenjp*|libxvid*|libzimg*|librubberband*|\
        libsoxr*|libsrt*|librist*|libzvbi*|libcodec2*|liblpcnet*|libfreedv*|\
        libplacebo*|libshaderc*|libvulkan*|libva*|libvdpau*|libdrm*|libmfx*|\
        libbluray*|libdc1394*|libchromaprint*|libsnappy*|libnuma*|libjack*|\
        libpulse*|libasound*|libpipewire*|libspa*|libsndfile*|libmpg123*|\
        librsvg*|libglycin*|libjxl*|libbrotli*|libheif*|libde265*|libaom_*|\
        libgdk_pixbuf*|libcairo*|libpango*|libpixman*)
            return 0 ;;
    esac
    return 1
}

log() { printf '  %s\n' "$*"; }

printf '\n== Building Release ==\n'
cmake -S "$SOURCE_DIR" -B "$BUILD_DIR" -G Ninja \
      -DCMAKE_BUILD_TYPE=Release -DVSP_BUILD_TESTS=OFF >/dev/null
cmake --build "$BUILD_DIR" >/dev/null
log "built $BUILD_DIR/desktop/vitasync"

printf '\n== Laying out the bundle ==\n'
rm -rf "$OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR"/{bin,lib,plugins,qml}
mkdir -p "$OUTPUT_DIR/bin/assets/covers"

cp "$BUILD_DIR/desktop/vitasync" "$OUTPUT_DIR/bin/"
cp "$SOURCE_DIR/assets/covers/titles.json" "$OUTPUT_DIR/bin/assets/covers/"
log "binary and offline title database in place"

# --- Qt plugins ----------------------------------------------------------
# Only the categories this app can actually reach: a window system to draw on,
# image formats for cover art, and the desktop theme integration.
# platformthemes is deliberately absent. Its GTK plugin drags in the whole of
# GTK for the sake of native file dialogs and a palette this app never uses --
# every control here is drawn by the app itself. Without it Qt falls back to
# its own Quick file dialog, which is bundled, looks like the rest of the app
# and has one less way to differ between machines.
for category in platforms imageformats iconengines multimedia \
                xcbglintegrations wayland-shell-integration \
                wayland-graphics-integration-client wayland-decoration-client \
                platforminputcontexts; do
    if [ -d "$QT_PLUGIN_DIR/$category" ]; then
        mkdir -p "$OUTPUT_DIR/plugins/$category"
        cp -a "$QT_PLUGIN_DIR/$category/." "$OUTPUT_DIR/plugins/$category/"
    fi
done
# KDE's extra image readers are not ours to redistribute here and are not
# needed: covers are PNG or JPEG.
rm -f "$OUTPUT_DIR"/plugins/imageformats/kimg_*.so

# Qt ships two media backends and loads whichever it finds. Keeping only the
# FFmpeg one halves the download: the GStreamer plugin drags in the whole of
# GStreamer for a job -- decoding one frame of a dropped video -- that FFmpeg
# already does.
rm -f "$OUTPUT_DIR"/plugins/multimedia/libgstreamermediaplugin.so
log "plugins: $(find "$OUTPUT_DIR/plugins" -name '*.so' | wc -l) files"

# --- QML modules ---------------------------------------------------------
# The app's own QML is compiled into the binary; these are Qt's own modules,
# named after the imports in desktop/qml.
for module in QtQml QtCore; do
    if [ -d "$QT_QML_DIR/$module" ]; then
        mkdir -p "$OUTPUT_DIR/qml/$module"
        cp -a "$QT_QML_DIR/$module/." "$OUTPUT_DIR/qml/$module/"
    fi
done

# QtQuick is copied a piece at a time. The module ships styles and submodules
# this app never imports -- FluentWinUI3 alone is larger than everything we
# actually need -- and shipping them would only make the download worse.
mkdir -p "$OUTPUT_DIR/qml/QtQuick"
find "$QT_QML_DIR/QtQuick" -maxdepth 1 -type f -exec cp -a {} "$OUTPUT_DIR/qml/QtQuick/" \;
for submodule in Window Shapes Dialogs Templates Layouts Effects; do
    if [ -d "$QT_QML_DIR/QtQuick/$submodule" ]; then
        mkdir -p "$OUTPUT_DIR/qml/QtQuick/$submodule"
        cp -a "$QT_QML_DIR/QtQuick/$submodule/." "$OUTPUT_DIR/qml/QtQuick/$submodule/"
    fi
done

# Controls, but only the Basic style the app asks for by name in main.cpp,
# plus the shared implementation the Quick file dialog is built from.
mkdir -p "$OUTPUT_DIR/qml/QtQuick/Controls"
find "$QT_QML_DIR/QtQuick/Controls" -maxdepth 1 -type f \
     -exec cp -a {} "$OUTPUT_DIR/qml/QtQuick/Controls/" \;
for part in Basic impl; do
    if [ -d "$QT_QML_DIR/QtQuick/Controls/$part" ]; then
        mkdir -p "$OUTPUT_DIR/qml/QtQuick/Controls/$part"
        cp -a "$QT_QML_DIR/QtQuick/Controls/$part/." \
              "$OUTPUT_DIR/qml/QtQuick/Controls/$part/"
    fi
done

cp -a "$QT_QML_DIR"/*.qmltypes "$OUTPUT_DIR/qml/" 2>/dev/null || true
log "qml modules: $(du -sh "$OUTPUT_DIR/qml" | cut -f1)"

# --- shared libraries ----------------------------------------------------
# Walk the dependency graph from the binary and from every plugin and QML
# module we just copied, since those are dlopened and ldd on the binary alone
# would miss them.
collect_dependencies() {
    local -a queue=("$@")
    local -A seen=()
    local item

    while [ ${#queue[@]} -gt 0 ]; do
        item="${queue[0]}"
        queue=("${queue[@]:1}")
        [ -n "${seen[$item]:-}" ] && continue
        seen[$item]=1

        while read -r name arrow path _; do
            [ "$arrow" != "=>" ] && continue
            [ -z "$path" ] || [ "$path" = "(0x00000000)" ] && continue
            [ -e "$path" ] || continue
            is_system_library "$name" && continue

            local target="$OUTPUT_DIR/lib/$name"
            if [ ! -e "$target" ]; then
                cp -L "$path" "$target"
                queue+=("$path")
            fi
        done < <(ldd "$item" 2>/dev/null || true)
    done
}

mapfile -t scan_targets < <(
    printf '%s\n' "$OUTPUT_DIR/bin/vitasync"
    find "$OUTPUT_DIR/plugins" "$OUTPUT_DIR/qml" -name '*.so' 2>/dev/null
)
collect_dependencies "${scan_targets[@]}"
log "libraries: $(find "$OUTPUT_DIR/lib" -name '*.so*' | wc -l) files, $(du -sh "$OUTPUT_DIR/lib" | cut -f1)"

# --- launcher ------------------------------------------------------------
cat > "$OUTPUT_DIR/vitasync" <<'LAUNCHER'
#!/usr/bin/env bash
# Portable launcher for VitaSync. Points Qt at the copies of its
# libraries, plugins and QML modules that live next to this script, so nothing
# has to be installed on the machine.
set -euo pipefail

HERE="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")" && pwd)"

export LD_LIBRARY_PATH="$HERE/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$HERE/plugins"
export QML_IMPORT_PATH="$HERE/qml"
export QML2_IMPORT_PATH="$HERE/qml"

exec "$HERE/bin/vitasync" "$@"
LAUNCHER
chmod +x "$OUTPUT_DIR/vitasync"
chmod +x "$OUTPUT_DIR/bin/vitasync"

printf '\n== Done ==\n'
log "$OUTPUT_DIR/vitasync"
log "total: $(du -sh "$OUTPUT_DIR" | cut -f1)"
