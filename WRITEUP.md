# Pokémon in a PDF — Writeup

A C++/SDL2 Pokémon fan game (Pokemon-Grey) compiled to asm.js and embedded in a
PDF. It runs and is playable in Chrome. A flag is hidden in the embedded script.

Flag: `FLAG{p0k3m0n_1n_4_pdf_gg}`

---

## How it runs in a PDF

PDFs can carry JavaScript (form logic). Chrome's PDF engine (pdfium) executes it.
That engine has JS, form fields, and `app.setInterval`. It has no canvas, WebGL,
DOM, WebAssembly, network, or audio.

So the game is built as **asm.js** (plain JS, not WASM) with Emscripten 1.39.20
fastcomp, and embedded as the PDF's document-open script. Three pieces make it work:

- **Rendering:** the OpenGL renderer was replaced with a software rasterizer
  (`Gfx` in `patch/Graphics.cpp`) that draws into a CPU RGBA buffer. Each frame the
  buffer is converted to ASCII (one character per pixel, picked by brightness) and
  written into a 480×270 grid of text form-fields (`field_0..field_269`).
- **Input:** form buttons and a text field call an exported `pokemon_key(code,down)`
  that fills a key table the game polls each frame.
- **Loop:** `main()` sets up and returns; `app.setInterval` calls an exported
  `pokemon_tick()` once per frame.

The bridge that wires this to pdfium is `patch/pokemon_pre.js` (~200 lines, readable,
sits at the top of the embedded script). Everything after it is Emscripten output.

---

## Build notes (short)

asm.js needs fastcomp, which is x86-only and removed from modern emsdk. On Apple
Silicon it runs under qemu, where the 2020 clang and node segfault randomly. The
build (`scripts/_docker_build_robust.sh`) wraps clang/node in retry-shims and runs
node `--jitless`. Other fixes: drop the dead `jpg` libjpeg port; init only
`SDL_INIT_TIMER`; polyfill `crypto`/`performance`/`console`; `-s
EMULATE_FUNCTION_POINTER_CASTS=1`; fix a `new[]`/`free()` heap bug in `Map::GoTo`
that crashed map warps under Emscripten. Full list in `CLAUDE.md`.

---

## Solving it

The flag is XOR-encoded with the repeating 7-byte key `GREYKEY` and stored in the
embedded script as obfuscated "save data." You recover it statically — no need to
run the game.

### 1. Read the embedded script

The script is stored uncompressed, so `strings` reads it. (`mutool show … js`
returns nothing here — it only dumps document-level JS, and this is a page
open-action.)
```
strings pokemon_ctf.pdf | grep -A6 SAVE_KEY    # straight to the data
strings pokemon_ctf.pdf | less                 # or read the whole bridge header
```
Most of the script is a huge compiled blob; the useful part is the short,
human-written header at the top.

### 2. Find the obfuscated data

In that header there are two values next to each other:
```js
var SAVE_KEY = "GREYKEY";
var SAVE_REC = [0x01, 0x1e, 0x04, ...];   // 26 bytes
```
A comment states the record is XOR'd against the key. They're plain text, so even
`grep` finds them directly in the PDF:
```
grep -a SAVE_KEY pokemon_ctf.pdf
grep -a SAVE_REC pokemon_ctf.pdf
```

### 3. XOR to decode

```python
key = b"GREYKEY"
rec = [0x01,0x1e,0x04,0x1e,0x30,0x35,0x69,0x2c,0x61,0x28,0x69,0x25,0x1a,
       0x68,0x29,0x0d,0x71,0x06,0x3b,0x21,0x3f,0x18,0x35,0x22,0x24,0x4b]
print("".join(chr(rec[i] ^ key[i % 7]) for i in range(25)))
# FLAG{p0k3m0n_1n_4_pdf_gg}
```

Or run the included solver straight on the PDF:
```
node solve/solve.js pokemon_ctf.pdf
# FLAG{p0k3m0n_1n_4_pdf_gg}
```

(`GREYKEY` is the key XOR is also applied to the same bytes inside the compiled
C++ at runtime, but the static path above is the intended solve.)

---

## Limitations

- Battles freeze in the PDF. `FightGUI::Battle()` is a nested blocking loop the
  timer-driven model can't re-enter. Avoid tall grass.
- The base game is a small demo: 3 maps (town, one interior, one route), 1 NPC, 1
  dialogue, plus a full battle engine. The flag does not depend on any of it.
- ~196 MB file (`-O0` build with embedded assets). Too big for the git repo —
  distribute it as a GitHub Release asset, not in the source tree.
