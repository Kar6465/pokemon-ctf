# Pokémon-in-a-PDF CTF — Project State

A Pokémon fan game (**Pokemon-Grey**, C++/SDL2) running **inside a PDF** using the
doompdf asm.js trick, as a CTF challenge. The flag is hidden in the asm.js memory.

> **This file is the source of truth for continuity.** It is auto-loaded every
> session. Keep "Changelog" and "TODO" updated after meaningful changes.

---

## Current status — IT WORKS

The overworld **renders and is playable inside `pokemon_ctf.pdf` in Chrome/Edge**:
walk with **WASD** (or the on-screen D-pad), `Space`/OK to interact, `T` team, `E` bag.
The map scrolls as you walk; doors/dialogue use the same (working) tick model.

**Deliverable:** ship **only `pokemon_ctf.pdf`** (self-contained: game + assets +
flag embedded). Players don't need the source. Current size ~196 MB.

### Game world / map connectivity (decoded from Map/*.txt `# TP` warps)
- 3 maps: Map 1 town (32×24), Map 2 a small interior (16×14), Map 3 a route (32×40).
- Town → Route: warp tiles (13/14/15, 0) at the TOP-CENTER edge → walk up to enter.
- Town → Interior: ONE warp at town tile (8,8) = the LEFT house's door → Map 2. The
  right house + lab are decorative (no warp). So "homes don't work" = only the left
  house is enterable, on its exact door tile.
- 1 NPC + 1 dialogue (`#TEST`, starter choice). No grass-encounter code found, so the
  route likely won't auto-trigger (battle-freezing) fights. It IS a small tech demo.

### What works in the PDF
- Walking / map scrolling, the HUD (clock, money, badges), on-screen D-pad + buttons.
- Interact (`Space`), Team menu (`T`), Bag (`E`), dialogue — all tick-driven.

### What does NOT work in the PDF
- **Battles** — `FightGUI::Battle()` runs a *nested blocking loop*; the PDF's
  timer-driven model can't re-enter it, so entering a battle **hangs**. Avoid tall
  grass / trainer fights when demoing. (This is "Phase 3b" — see TODO.)

## The CTF flag
- `FLAG{p0k3m0n_1n_4_pdf_gg}` XOR'd with repeating key `GREYKEY`.
- Stored **already-encoded** as `g_flag_enc[]` in `patch/GUI.cpp` (no plaintext in
  the binary). Written to `g_flag_buf` on a battle win, but also **statically
  recoverable from the embedded JS** — so the challenge does NOT require playing.
- Intended solve: extract the JS, find `GREYKEY` + the 26 encoded bytes, XOR.

---

## How to rebuild

The asm.js build only works with **Emscripten 1.39.20-fastcomp** (asm.js, not WASM —
the PDF JS engine has no WebAssembly). Apple Silicon can't run fastcomp natively, so
we build in **Docker (colima, linux/amd64)** with crash-retry shims (the emulated
2020 clang + node segfault randomly under qemu).

```bash
# JS / PDF-layout only change (patch/pokemon_pre.js, scripts/generate_pdf.py): no recompile
bash scripts/build_pdf.sh

# C++ source change: refresh into game/, drop that object, rebuild, repackage
cp patch/<File>.cpp game/ ; rm -f game/build/<File>.o
docker rm -f pokemon-build 2>/dev/null
docker run --platform linux/amd64 --name pokemon-build \
  -v "$PWD":/work -v pokemon-emsdk:/root/emsdk -w /work ubuntu:22.04 \
  bash /work/scripts/_docker_build_robust.sh > build.log 2>&1
bash scripts/build_pdf.sh
```
Objects are cached in `game/build/*.o`; only changed files recompile. `Graphics.cpp`
builds at `-O2` (hot rasterizer); everything else `-O0` with an `-O0` link (fast, no
slow minify). A full `-O2` build would be faster+smaller but the emulated minify link
is very slow (~1h).

### Key build gotchas (all already fixed)
- emsdk must be cloned at the `1.39.20` tag; modern emsdk removed fastcomp.
- clang/clang++ **and** node are wrapped with retry shims; node also needs
  `--jitless` (V8 JIT crashes under qemu) + a big heap.
- Drop `jpg` from SDL2_IMAGE_FORMATS (libjpeg port 404s on dead bintray).
- `SDL_Init` only `SDL_INIT_TIMER` under `__EMSCRIPTEN__` (video/audio need
  canvas/AudioContext → abort).
- `pokemon_pre.js` polyfills `crypto` / `performance` / `console` (random_device,
  SDL timer) — the PDF sandbox lacks them.
- `-s EMULATE_FUNCTION_POINTER_CASTS=1` (fixes "invalid function pointer signature").
- Do **NOT** use `ERROR_ON_UNDEFINED_SYMBOLS=0` — it silently stubs freetype
  (`FT_New_Memory` abort).

---

## File map
- `patch/` — the patched game sources (copied into `game/` to build).
  - `Graphics.cpp/.h` — software renderer (`Gfx`), renders at 360×200 grid in PDF.
  - `main.cpp` — `SetupGame()`/`Frame()` split, `pokemon_tick` export, emscripten init.
  - `Inputs.cpp` — `pokemon_key()` bridge, key-state table.
  - `GUI.cpp` — CTF flag (`g_flag_enc`, `write_ctf_flag`).
  - `pokemon_pre.js` — JS bridge: framebuffer→ASCII, input map, tick driver, polyfills.
- `scripts/` — build pipeline. `generate_pdf.py` builds the PDF (grid + controls).
- `PORTING.md` — the full renderer-port design & phases.

---

## Solving the CTF (intended path — STATIC, solvable)
The flag is now recoverable WITHOUT running the 190MB blob (the old heap-scan path
was effectively unsolvable). `patch/pokemon_pre.js` carries the obfuscated flag as
greppable "save data" in the readable bridge at the top of the embedded script:
`SAVE_KEY = "GREYKEY"` and `SAVE_REC = [26 bytes]`.
1. Open `pokemon_ctf.pdf` in Chrome → playable game → it has embedded JS.
2. Extract the JS (`mutool show ... js`, or `strings`/`grep` the PDF directly).
3. Find `SAVE_KEY` + `SAVE_REC` near each other in the readable header.
4. XOR: `flag[i] = SAVE_REC[i] ^ "GREYKEY"[i%7]` → `FLAG{p0k3m0n_1n_4_pdf_gg}`.
5. Solver: `node solve/solve.js pokemon_ctf.pdf` (static; verified, prints the flag).
   Player-facing prompt + staged hints live in `CHALLENGE.md`.
(The C++ `g_flag_enc`/`GREYKEY` heap version still exists but is the hard/runtime
flavor; the static save-data path above is the intended solve.)

## Changelog (most recent first)
- **Made the CTF actually solvable:** embedded the obfuscated flag as greppable
  `SAVE_KEY`/`SAVE_REC` in pokemon_pre.js (the heap-only path was unsolvable in
  practice). Added CHALLENGE.md (prompt + hints); solve/solve.js is now a static
  XOR solver that runs on the PDF directly (verified → FLAG{...}).
- D-pad buttons now PULSE on click (key_btn, ~280ms) instead of key_down/key_up —
  a quick tap was too brief for the game's input poll, so the D-pad did nothing.
- Fixed `Map::GoTo()` freeing `new[]` arrays (`_map`, `_collision`) with `free()` —
  UB that corrupts Emscripten's heap and crashed EVERY map transition (doors/warps)
  in the PDF. Now `delete[]`. Unlocks entering the left house + walking to the route.
- Render grid raised 360×200 → **480×270** for readability (GRID_W/H in Graphics.cpp
  + generate_pdf.py must stay in sync). Slightly slower (more cells/frame).
- Added `WRITEUP.md` (in-depth project + CTF writeup).
- Exposed `HEAPU8`/`getValue` so the CTF heap-scan solve is clean; added solve/solve.js.
- Movement: always-run (hold Shift) for faster walking; key-hold 260 ms + 40 ms tick
  for input reliability; WASD remap (w→up, a→left).
- Removed the on-page flag hint.
- Render directly at 360×200 grid (~10× fewer pixels/frame) + `Graphics.cpp` at `-O2`.
- Fixed full startup chain: crypto/performance polyfills, SDL timer-only init,
  EMULATE_FUNCTION_POINTER_CASTS, clean link (no undefined-symbol stubbing).
- Ported renderer OpenGL→software; tick loop + input bridge; self-contained PDF gen.

## TODO / not done
- **Movement still feels slow / sometimes sticks** — input is via PDF text-field
  keystrokes (janky by nature); holding the D-pad is smoother. Further speed: a full
  `-O2` build, or raise the player's base walk speed in the game code.
- **Battles in-PDF (Phase 3b)** — convert `FightGUI::Battle()` (and nested dialogue
  loops) from blocking `while` loops into the `Setup`/`Frame` tick/state-machine model.
  Required if battles must be playable in-PDF (not required for the flag).
- **Size** — 196 MB. A full `-O2`/`-Os` build (dead-code elim) would shrink it a lot,
  but the emulated minify link is slow/risky.
- Decide flag design: keep static-recoverable (current) vs. force a fight (runtime-only
  flag + Phase 3b).
