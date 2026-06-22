#!/bin/bash
# =============================================================
# Pokemon CTF — One-time setup
# Run this once from the pokemon-ctf directory on your Linux machine.
# It clones Pokemon-Grey and copies the patched files over it.
# =============================================================
set -e

echo "[*] Installing system dependencies..."
sudo apt-get install -y git python3 python3-venv make g++ \
    libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev libsdl2-mixer-dev

echo "[*] Cloning Pokemon-Grey..."
if [ ! -d "game" ]; then
    git clone https://github.com/AMasquelier/Pokemon-Grey.git game
fi

echo "[*] Applying CTF patch (copying patched source files)..."
cp patch/*.cpp game/
cp patch/*.h   game/
cp patch/Makefile game/

echo ""
echo "Setup complete! Next steps:"
echo ""
echo "  1. cd game && bash ../scripts/build_emscripten.sh"
echo "     (compiles the game to asm.js — takes ~10 min first time)"
echo ""
echo "  2. cd .. && bash scripts/build_pdf.sh"
echo "     (packages everything into pokemon_ctf.pdf)"
echo ""
echo "  3. Open pokemon_ctf.pdf in Chrome"
