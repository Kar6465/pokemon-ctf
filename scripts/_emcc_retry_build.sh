#!/bin/bash
# ============================================================================
# Robust emscripten (fastcomp asm.js) build for FLAKY / EMULATED hosts.
#
# On Apple Silicon the x86-only fastcomp clang runs under qemu and segfaults
# *stochastically*, on a different source/port each run. This script works
# around that instead of fighting it:
#   - compile each .cpp to its own .o, retrying that ONE file until it succeeds
#   - object files live in build/ (persisted), so finished files are never redone
#   - link once (also retried)
# Combined with a persisted emsdk + port cache, the build converges reliably even
# though any individual clang invocation may crash.
#
# Run from the game/ directory, with emsdk already activated in the environment.
# On a real x86_64 host you don't need this — use build_emscripten.sh.
# ============================================================================
set -u
export EMCC_CORES=1   # serial port builds are far more stable under emulation

SRCS="main Basics Character Database Dialogue Event GUI Graphics Inputs Item Map Pokemon Sound Tile"
MAXTRIES=40
mkdir -p build out

# -O0: under emulation the heavy JS-minify passes (run in jitless node) take
# forever; -O0 skips them. The build is bigger/uglier but functionally identical,
# which is all we need to run it in a PDF. (Cached -O2 objects link fine with an
# -O0 link.)
# png only: the game has no JPEGs, and the libjpeg port in emsdk 1.39.20 fetches
# from the dead dl.bintray.com (404), which would break the build for nothing.
CFLAGS=(-std=c++14 -O0 -s WASM=0 -s USE_SDL=2 -s USE_SDL_IMAGE=2 -s USE_SDL_TTF=2
        -s USE_SDL_MIXER=2 -s SDL2_IMAGE_FORMATS='["png"]')

retry() {
  local n=0
  until "$@"; do
    n=$((n + 1))
    if [ "$n" -ge "$MAXTRIES" ]; then echo "  !! gave up after $n tries: $*" >&2; return 1; fi
    echo "  .. retry $n (emulated clang crash) " >&2
  done
}

echo "[*] emcc: $(emcc --version | head -1)"

# ---- compile each source to its own object, with per-file retries ----
# Graphics.cpp holds the per-pixel software rasterizer (the hot loop), so build
# it at -O2 even though the rest stay -O0; this is the cheap speed win without a
# full -O2 rebuild + slow minify link. (Last -O flag wins, so -O2 overrides the
# -O0 in CFLAGS for this file.)
for s in $SRCS; do
  if [ -f "build/$s.o" ]; then echo "[skip] build/$s.o (already built)"; continue; fi
  OPT=""; [ "$s" = "Graphics" ] && OPT="-O2"
  echo "[cc]  $s.cpp ${OPT:+(opt $OPT)}"
  retry emcc "${CFLAGS[@]}" $OPT -c "$s.cpp" -o "build/$s.o" || exit 1
done

# ---- link (NO outer retry: crashes are now handled inside the clang/node shims,
#      so any link failure here is a real, deterministic error — fail fast) ----
echo "[link] -> out/game.js  (embeds assets; this step is the heavy one)"
OBJS=""; for s in $SRCS; do OBJS="$OBJS build/$s.o"; done
emcc "${CFLAGS[@]}" \
  -s TOTAL_MEMORY=268435456 -s SINGLE_FILE=1 -s EXIT_RUNTIME=0 \
  -s EMULATE_FUNCTION_POINTER_CASTS=1 \
  `# the game calls function pointers through casts (mismatched signatures);` \
  `# asm.js rejects those as "invalid function pointer ... signature" unless` \
  `# emscripten emulates them. Link-time transform, so no recompile needed.` \
  -s EXPORTED_FUNCTIONS='["_main","_pokemon_tick","_pokemon_key"]' \
  -s EXTRA_EXPORTED_RUNTIME_METHODS='["ccall","cwrap","FS","HEAPU8","getValue"]' \
  `# expose HEAPU8 so the CTF is solvable: a player extracts the JS, runs it, and` \
  `# scans Module.HEAPU8 for the GREYKEY-XOR'd flag bytes (the intended solve).` \
  --embed-file Characters --embed-file Database --embed-file Fonts \
  --embed-file Map --embed-file Menu --embed-file Save --embed-file Tilesets \
  $OBJS -o out/game.js || exit 1

echo "[*] DONE -> out/game.js  ($(du -h out/game.js | cut -f1))"
