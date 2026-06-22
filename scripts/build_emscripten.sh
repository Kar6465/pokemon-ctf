#!/bin/bash
# =============================================================
# Step 1: Compile Pokemon-Grey (patched) to asm.js
# Run this from inside the game/ directory after setup.sh copies files there
# =============================================================
#
# IMPORTANT CAVEAT (read scripts/README or the top-level README): Pokemon-Grey
# renders with immediate-mode OpenGL (glBegin/glVertex3f/GL_QUADS in
# Graphics.cpp and SDL_GL_CreateContext in main.cpp). The Chrome PDF viewer
# (pdfium) has NO WebGL/canvas, so SDL_GL_CreateContext returns NULL and the
# game cannot draw inside a PDF as-is. This script produces a *web* asm.js build
# (runnable in a normal browser page) and is the correct first step, but turning
# it into a working in-PDF game additionally requires porting the renderer to a
# software RGBA framebuffer and adding doompdf-style glue (see build_pdf.sh).
# =============================================================
set -e

EMSDK_DIR="$HOME/emsdk"
# Must be the *fastcomp* SDK: only the old fastcomp backend can emit asm.js
# (-s WASM=0). The plain "1.39.20" id on a modern emsdk installs the upstream
# LLVM/wasm backend, which ignores WASM=0 and cannot produce asm.js. doompdf
# uses 1.39.20-fastcomp for exactly this reason.
EMSCRIPTEN_VERSION="1.39.20-fastcomp"

# ---- Install emsdk if not present ----
# IMPORTANT: clone the emsdk repo *pinned to the old 1.39.20 tag*. The current
# emsdk HEAD has removed fastcomp support and refuses to install it:
#   "error: the fastcomp backend is no longer supported"
# Pinning the emsdk checkout to 1.39.20 is exactly what doompdf's build.sh does.
if [ ! -f "$EMSDK_DIR/emsdk" ]; then
    echo "[*] Cloning emsdk (pinned to 1.39.20, the last tag that supports fastcomp)..."
    rm -rf "$EMSDK_DIR"
    git clone --branch 1.39.20 https://github.com/emscripten-core/emsdk.git "$EMSDK_DIR"
fi

cd "$EMSDK_DIR"
./emsdk install $EMSCRIPTEN_VERSION
./emsdk activate $EMSCRIPTEN_VERSION
source ./emsdk_env.sh
cd -

echo "[*] Emscripten: $(emcc --version | head -1)"
mkdir -p out

SRCS="main.cpp Basics.cpp Character.cpp Database.cpp Dialogue.cpp Event.cpp \
      GUI.cpp Graphics.cpp Inputs.cpp Item.cpp Map.cpp Pokemon.cpp Sound.cpp Tile.cpp"

echo "[*] Compiling to asm.js (this takes a few minutes)..."

emcc $SRCS \
    -std=c++14 \
    -O2 \
    -s WASM=0 \
    -s USE_SDL=2 \
    -s USE_SDL_IMAGE=2 \
    -s USE_SDL_TTF=2 \
    -s USE_SDL_MIXER=2 \
    -s SDL2_IMAGE_FORMATS='["png"]' \
    `# png only: no JPEG assets, and emsdk 1.39.20's libjpeg port 404s (dead bintray)` \
    -s TOTAL_MEMORY=268435456 \
    -s SINGLE_FILE=1 \
    -s EXIT_RUNTIME=0 \
    -s ERROR_ON_UNDEFINED_SYMBOLS=0 \
    `# SDL2's threading references pthread_mutex_* which don't resolve in this`\
    `# single-threaded asm.js link; allow them to become harmless stubs` \
    -s EXPORTED_FUNCTIONS='["_main","_pokemon_tick","_pokemon_key"]' \
    -s EXTRA_EXPORTED_RUNTIME_METHODS='["ccall","cwrap","FS"]' \
    --embed-file Characters \
    --embed-file Database \
    --embed-file Fonts \
    --embed-file Map \
    --embed-file Menu \
    --embed-file Save \
    --embed-file Tilesets \
    -o out/game.js

# Notes:
#  * Rendering is now a software framebuffer (no OpenGL), so the LEGACY_GL_EMULATION
#    flag was dropped.
#  * Music/ and Sound/ are intentionally NOT embedded: a PDF has no audio device,
#    and they added several MB to the (already large) single-file JS. Audio init
#    is non-fatal (see main.cpp), so the game runs fine without them.
#  * _pokemon_tick / _pokemon_key are the entry points pokemon_pre.js calls to
#    drive frames and feed input. EXIT_RUNTIME=0 keeps them callable after main()
#    returns (main() just sets the world up under __EMSCRIPTEN__).

echo "[*] Done! -> out/game.js"

# Notes on the flags that changed from the original draft:
#  * Removed "-s ASM_JS=1": deprecated/no-op in 1.39.x; -s WASM=0 already selects
#    the asm.js output. Leaving it in did nothing.
#  * Removed "-s ALLOW_MEMORY_GROWTH=1": memory growth forces emscripten to emit
#    a non-asm.js (non-validating) fallback, which defeats the point of WASM=0.
#    Instead TOTAL_MEMORY is raised to 256 MB up front.
#  * --preload-file -> --embed-file: preload fetches a separate ".data" file over
#    XHR at startup. A PDF has no network/XHR, so preloaded assets never load.
#    --embed-file inlines the bytes into game.js (this makes the file large).
#  * -s SINGLE_FILE=1: forces everything into one .js (no .mem/.wasm/.data
#    sidecars), which is required because the PDF can only carry one JS blob.
