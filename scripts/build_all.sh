#!/bin/bash
# =============================================================
# Pokemon CTF — full end-to-end build for an x86_64 Linux host
# =============================================================
# Runs the whole chain on a real Intel/AMD Linux box (bare metal, a cloud VM,
# or an x86_64 Docker container):
#     deps -> clone Pokemon-Grey -> apply patch -> emscripten asm.js -> PDF
#
# WHY x86_64 LINUX: the asm.js step needs Emscripten 1.39.20 *fastcomp*, whose
# clang/llvm binaries only exist for x86_64. Running them under qemu emulation
# (e.g. on Apple Silicon) makes the 2020-era clang segfault randomly mid-build.
# Native x86_64 Linux runs it reliably in ~10 minutes.
#
# USAGE (from a fresh Ubuntu/Debian x86_64 host):
#     git clone <this repo> pokemon-ctf && cd pokemon-ctf
#     bash scripts/build_all.sh
#
# Or in Docker on an x86_64 machine:
#     docker run --rm -v "$PWD":/work -w /work ubuntu:22.04 \
#         bash -c "apt-get update && apt-get install -y git && bash scripts/build_all.sh"
#
# Output: ./pokemon_ctf.pdf  (plus game/out/game.js, the browser-runnable asm.js)
# =============================================================
set -e

REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_DIR"

# sudo only if present and not already root
SUDO=""
if [ "$(id -u)" -ne 0 ]; then SUDO="sudo"; fi

ARCH="$(uname -m)"
if [ "$ARCH" != "x86_64" ] && [ "$ARCH" != "amd64" ]; then
    echo "[!] WARNING: arch is '$ARCH', not x86_64. The fastcomp toolchain is"
    echo "[!] x86_64-only and will likely crash under emulation here. Continuing,"
    echo "[!] but use a native x86_64 host if it fails."
fi

echo "==== [1/4] system dependencies ===="
export DEBIAN_FRONTEND=noninteractive
$SUDO apt-get update -qq
$SUDO apt-get install -y -qq \
    git python3 python3-venv python3-pip make wget xz-utils bzip2 ca-certificates
# (SDL2 dev packages are NOT needed here: the asm.js build uses Emscripten's own
#  bundled SDL2 ports. They are only needed for the native `make` test build.)

echo "==== [2/4] clone Pokemon-Grey + apply CTF patch ===="
if [ ! -d game ]; then
    git clone --depth 1 https://github.com/AMasquelier/Pokemon-Grey.git game
fi
cp patch/*.cpp patch/*.h game/
cp patch/Makefile game/
echo "    patched sources copied into game/"

echo "==== [3/4] compile to asm.js (Emscripten 1.39.20 fastcomp) ===="
cd "$REPO_DIR/game"
bash "$REPO_DIR/scripts/build_emscripten.sh"

echo "==== [4/4] package into PDF (self-contained) ===="
cd "$REPO_DIR"
bash "$REPO_DIR/scripts/build_pdf.sh" || echo "[build_pdf.sh returned non-zero]"

echo ""
echo "================================================"
echo " DONE"
echo "   asm.js : $REPO_DIR/game/out/game.js"
echo "   PDF    : $REPO_DIR/pokemon_ctf.pdf"
echo "================================================"
echo " The OpenGL renderer has been ported to a software framebuffer, so the"
echo " overworld renders (as ASCII) and is playable in the PDF. NOTE: battles"
echo " still use a nested blocking loop and will hang in-PDF until the Phase 3b"
echo " tick refactor (see PORTING.md). The CTF flag is recoverable from the JS"
echo " regardless (see README)."
