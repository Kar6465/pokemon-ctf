#!/bin/bash
# =============================================================
# Step 2: Package game.js into a self-contained PDF.
# Run from the pokemon-ctf root directory.
# Requires: game/out/game.js (run scripts/build_emscripten.sh first).
# =============================================================
#
# Self-contained: we no longer depend on the doompdf repo. We ship our own bridge
# (patch/pokemon_pre.js) and PDF generator (scripts/generate_pdf.py), which is the
# same doompdf technique adapted to Pokemon's renderer, controls, and grid:
#
#   compiled.js = pokemon_pre.js + game.js      (bridge first, then the game)
#   pokemon_ctf.pdf = generate_pdf.py(compiled.js)
#
# pokemon_pre.js defines Module, paints the RGBA framebuffer into a 360x200 grid
# of PDF text fields (update_framebuffer), maps the on-screen buttons / key_input
# field to pokemon_key(), and drives pokemon_tick() via app.setInterval. The C
# side (Gfx::Present + the _pokemon_tick/_pokemon_key exports) is built by
# build_emscripten.sh.
# =============================================================
set -e

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
GAME_JS="$ROOT_DIR/game/out/game.js"
BRIDGE="$ROOT_DIR/patch/pokemon_pre.js"
GEN="$ROOT_DIR/scripts/generate_pdf.py"
VENV="$ROOT_DIR/.venv"

if [ ! -f "$GAME_JS" ]; then
    echo "[!] Error: $GAME_JS not found. Run scripts/build_emscripten.sh first."
    exit 1
fi
if [ ! -f "$BRIDGE" ]; then
    echo "[!] Error: $BRIDGE missing."; exit 1
fi

# pdfrw is the only Python dependency of generate_pdf.py.
if [ ! -d "$VENV" ]; then
    python3 -m venv "$VENV"
fi
source "$VENV/bin/activate"
pip3 install pdfrw -q

mkdir -p "$ROOT_DIR/out"
COMPILED="$ROOT_DIR/out/compiled.js"
echo "[*] Concatenating bridge + game.js ..."
cat "$BRIDGE" "$GAME_JS" > "$COMPILED"

echo "[*] Generating PDF ..."
python3 "$GEN" "$COMPILED" "$ROOT_DIR/pokemon_ctf.pdf"

echo ""
echo "================================================"
echo " Wrote: $ROOT_DIR/pokemon_ctf.pdf"
echo " Open in a Chromium-based browser (Chrome/Edge)."
echo " Controls: D-pad / type z q s d, space, enter."
echo " (Battles still need the Phase 3b tick refactor;"
echo "  see PORTING.md. The overworld is playable.)"
echo "================================================"
