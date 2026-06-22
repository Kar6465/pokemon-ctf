# Pokémon Inside a PDF — An In-Depth Writeup

> A complete Pokémon fan game (Pokemon-Grey, C++/SDL2) compiled and embedded so it
> **runs, renders, and is playable inside a single PDF file**, opened in Chrome —
> with a hidden CTF flag recoverable by reverse-engineering the program's memory.

This document explains, in depth: *why this is even possible*, *how it was built*,
*every wall we hit and how we got past it*, and *how the CTF is solved*.

---

## 1. The premise: a PDF is a (terrible, wonderful) computer

Most people think of a PDF as a static document. But the PDF specification includes
an **embedded JavaScript** feature (originally for form validation — "auto-fill this
field, check that date"). Chrome's built-in PDF viewer (pdfium) ships a JavaScript
engine to run it.

That engine is extremely restricted. It has:

- ✅ JavaScript execution
- ✅ form fields (text boxes, buttons) readable/writable from JS
- ✅ `app.setInterval(...)` — a timer
- ❌ no `<canvas>`, no WebGL, no DOM, no network, no Web Audio, **no WebAssembly**
- ❌ no `crypto`, no `performance`, no `console`, no `document`/`window`

The whole project is an exercise in: *given only "run JS + write to text fields + a
timer," can you run a real C++/SDL game?* The answer — following the lead of
[doompdf](https://github.com/ading2210/doompdf), which did this for DOOM — is **yes**,
if you solve three problems: **run the code**, **draw the pixels**, and **read input**.

---

## 2. Architecture: how a C++ game becomes a PDF

```
Pokemon-Grey (C++/SDL2)
      │  Emscripten 1.39.20 (fastcomp)
      ▼
   game.js  (asm.js — plain JavaScript, NOT WebAssembly)
      │  +  pokemon_pre.js  (the bridge)
      ▼
   generate_pdf.py  →  pokemon_ctf.pdf
```

### 2a. Run the code → asm.js (not WASM)
We compile the C++ to **asm.js** — a stylized but 100% valid subset of JavaScript
that encodes a whole C program. Why not WebAssembly? Because pdfium's JS engine has
no WASM. asm.js is "just JavaScript," so it runs. The entire game — logic, assets,
memory — becomes one big `.js` blob, embedded into the PDF as the page's
**OpenAction** (a script that runs when the document opens).

### 2b. Draw the pixels → a grid of text fields
There is no screen. So — doompdf's core trick — the PDF contains a **grid of text
form-fields**, one per screen row (`field_0`, `field_1`, …). Each frame, JS writes a
long string into each field's `.value`, choosing a character per pixel by brightness:

```
bright → "_"   ::   "?"   "//"   "b"   "#"  ← dark
```

From a distance, that wall of characters *is* a low-res grayscale image. (Those exact
glyphs were chosen by doompdf because they render at equal width in Chrome's
text-field font.)

**The original Pokemon-Grey rendered with OpenGL** — which pdfium does not have. So
the single biggest piece of work was **rewriting the renderer**: we replaced the
entire immediate-mode OpenGL backend (`glBegin/glVertex/glEnd`, textured quads) with
a **software rasterizer** (`Gfx` in `patch/Graphics.cpp`) that draws into a CPU RGBA
buffer. Conveniently, the GL code was *already* a 2D textured-quad blitter, so the
port is a faithful 1:1 software rasterizer: every `Draw::*` computes 4 screen-space
corners + UVs and hands them to one `BlitQuad()` (two triangles, barycentric UV
interpolation, nearest sampling, alpha blend). The framebuffer is rendered directly
at the text-field grid resolution and pushed to JS via `EM_ASM(update_framebuffer)`.

### 2c. Read input → buttons + a typing field
No keyboard events exist. The PDF has **form buttons** (a D-pad + OK/ESC/TEAM/BAG)
whose click actions call into the JS, plus a text field you type into. Those feed an
exported C function `pokemon_key(code, down)`, which fills a key-state table that the
game polls each frame instead of reading SDL events.

### 2d. The loop → a timer
A PDF has no `requestAnimationFrame` and no SDL event loop. DOOM exposes a tick
function called from a timer; we do the same: `main()` sets the game up and returns,
and `app.setInterval("pokemon_tick()", 40)` drives one frame per tick. (`main.cpp`
was split into `SetupGame()` + `Frame()` so the native build keeps its `while` loop
while the PDF build is purely tick-driven.)

---

## 3. The build odyssey (this was the hard part)

asm.js can only be produced by Emscripten's old **fastcomp** backend, pinned to
version **1.39.20**. Modern Emscripten removed fastcomp entirely. And fastcomp's
binaries are **x86-64 only** — which matters a lot on an Apple Silicon Mac.

We build inside Docker (`colima`, `--platform linux/amd64`). The walls, in order:

1. **`the fastcomp backend is no longer supported`** — modern `emsdk` refuses to
   install fastcomp. Fix: clone `emsdk` pinned to the `1.39.20` *tag*.
2. **Random `clang` segfaults under emulation** — the 2020-era x86 clang, run under
   qemu on ARM, **segfaults stochastically** (different file each run), so a
   monolithic build never finishes. Fix: a **shim** that wraps `clang`/`clang++` and
   **retries on any crash signature**. This made even the 50-file SDL2 port build.
3. **Dead download** — Emscripten 1.39.20's libjpeg port fetches from
   `dl.bintray.com`, which JFrog shut down in 2021 (404). Fix: the game has no JPEGs,
   so drop `jpg` from the SDL_image formats.
4. **`node` crashes too** — the link step runs Emscripten's JS compiler/minifier in
   `node`, whose V8 **JIT also crashes under qemu**, plus OOMs on the huge file. Fix:
   shim `node` with `--jitless` (pure interpreter — emulation-stable) + a big heap +
   retry.
5. **`undefined symbol: pthread_mutex_unlock`** during link. A misguided
   `ERROR_ON_UNDEFINED_SYMBOLS=0` "fixed" it but **silently stubbed freetype**, so
   font init aborted (`FT_New_Memory`). Fix: remove that flag; the objects link
   cleanly.
6. **Runtime aborts in the PDF**, fixed one by one as each missing browser API
   surfaced:
   - `std::random_device` → polyfill `crypto.getRandomValues`.
   - `SDL_Init` timer → polyfill `performance.now`.
   - `SDL_Init(VIDEO|AUDIO)` reaches for `document`/`AudioContext` → only init
     `SDL_INIT_TIMER` under Emscripten (we don't use SDL video/audio).
   - `"Library not initialized"` → must still init the SDL *library* (timer).
   - `"Invalid function pointer signature 'iii'"` → `-s EMULATE_FUNCTION_POINTER_CASTS=1`.

Only after all of that does the overworld actually render inside the PDF.

---

## 4. The CTF

### The flag
`FLAG{p0k3m0n_1n_4_pdf_gg}`, XOR'd with the repeating 7-byte key `GREYKEY`. The
**encoded** bytes (`g_flag_enc[]` in `patch/GUI.cpp`) and the key are compiled into
the program — the **plaintext is never present** (verified: `strings`/`grep` on the
JS find nothing). `write_ctf_flag()` also copies the bytes into a live buffer on a
battle win, but the data is in the program's static memory from startup regardless.

### Intended solve
1. Open `pokemon_ctf.pdf` in Chrome → it's a playable game → realize it contains
   embedded JavaScript.
2. **Extract the JS** from the PDF (it's the OpenAction): `mutool show … js`,
   `pdf-parser.py`, `pikepdf`, or `strings`.
3. Recognize **asm.js**. `grep FLAG` → nothing. It's not a freebie; it's a memory RE.
4. The flag is XOR-obfuscated in the program **heap**. Run the JS, read
   `Module.HEAPU8` (exported), and XOR-scan for the `FLAG{` pattern using `GREYKEY`:
   ```js
   flag[i] = heap[N + i] ^ "GREYKEY".charCodeAt(i % 7)
   ```
   → `FLAG{p0k3m0n_1n_4_pdf_gg}`. (See `solve/solve.js`.)

### Difficulty knobs
- Easier: tell solvers "XOR key is 7 bytes, in memory."
- Harder: don't; force them to dump the heap, `strings` it to spot `GREYKEY`, and
  derive the obfuscation. (Truly hardening it would mean computing the key at runtime
  so it isn't in the file at all.)

---

## 5. Honest limitations

- **Battles don't run in the PDF.** `FightGUI::Battle()` is a nested blocking loop;
  the PDF's timer-driven model can't re-enter it, so a battle freezes the game. Avoid
  tall grass when demoing. (Porting it to the tick model is the main remaining work.)
- **The base game is a tech demo.** Pokemon-Grey has only **3 small maps**, **1 NPC**,
  and **1 scripted dialogue** (the starter choice) — plus a full battle engine. The
  small world is all the upstream repo ever had; we changed *how* it runs, not its
  content.
- **The file is large (~196 MB).** It's an unminified (`-O0`) asm.js build with
  ~17 MB of embedded art. A full `-O2`/`-Os` build would shrink it a lot, but the
  size-optimization (minify) pass is brutally slow under emulation.

---

## 6. How to build / run / solve

```bash
# Build (x86-64 Linux, or Docker on any Intel host; on Apple Silicon use the
# shimmed Docker flow in scripts/_docker_build_robust.sh):
bash scripts/build_all.sh        # deps + clone + patch + asm.js + PDF
# → pokemon_ctf.pdf

# Play:  open pokemon_ctf.pdf in Chrome/Edge. WASD or the D-pad. Avoid tall grass.

# Solve: extract the JS from the PDF, then
node --max-old-space-size=8192 solve/solve.js game/out/game.js
```

See `CLAUDE.md` for the live project state, the full rebuild recipe, and every
build gotcha; `PORTING.md` for the renderer-port design.
