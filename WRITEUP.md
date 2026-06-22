# Pokémon in a PDF — Writeup

A C++/SDL2 Pokémon fan game (Pokemon-Grey) compiled to asm.js and embedded in a
PDF. It runs and is playable in Chrome. A flag is hidden in the program's memory.

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
  written into a 480×270 grid of text form-fields. `field_0..field_269`.
- **Input:** form buttons and a text field call an exported `pokemon_key(code,down)`
  that fills a key table the game polls each frame.
- **Loop:** `main()` sets up and returns; `app.setInterval` calls an exported
  `pokemon_tick()` once per frame.

The bridge that wires all this to pdfium is `patch/pokemon_pre.js` (~200 lines).
Everything else in the PDF is the Emscripten output.

---

## Build notes (short)

asm.js needs fastcomp, which is x86-only and removed from modern emsdk. On Apple
Silicon it runs under qemu, where the 2020 clang and node segfault randomly. The
build (`scripts/_docker_build_robust.sh`) wraps clang/node in retry-shims and runs
node `--jitless`. Other fixes: drop the dead `jpg` libjpeg port; init only
`SDL_INIT_TIMER`; polyfill `crypto`/`performance`/`console`; `-s
EMULATE_FUNCTION_POINTER_CASTS=1`; fix a `new[]`/`free()` heap bug in `Map::GoTo`
that crashed map warps under Emscripten. See `CLAUDE.md` for the full list.

---

## Solving it

The flag is XOR-encoded with the repeating 7-byte key `GREYKEY` and stored in the
program's memory. Plaintext never appears in the file. The solve has three parts:
get the code out of the PDF, run it, read the flag out of the heap.

### 1. Get the JavaScript out of the PDF

The game is the PDF's OpenAction script. Pull it out.

mutool:
```
mutool show pokemon_ctf.pdf js > game_js.txt
```

Or with Python (pikepdf), which is robust against odd structure:
```python
import pikepdf
pdf = pikepdf.open("pokemon_ctf.pdf")
# OpenAction on the document or page AA/O; dump every JS action found:
def walk(o, seen=set()):
    if id(o) in seen: return
    seen.add(id(o))
    if isinstance(o, pikepdf.Dictionary):
        if o.get("/S") == pikepdf.Name("/JavaScript") and "/JS" in o:
            js = o["/JS"]
            print(bytes(js) if isinstance(js, pikepdf.String) else js)
        for v in o.values(): walk(v, seen)
    elif isinstance(o, pikepdf.Array):
        for v in o: walk(v, seen)
walk(pdf.Root)
```

Crude fallback: the script is a giant ASCII blob, so `strings`/`binwalk` will also
surface it.

You end up with one large `.js` file.

### 2. Identify it

It's Emscripten asm.js. Tells: `"use asm"`, `Module`, `HEAPU8`, `_malloc`,
`asmGlobalArg`. The flag is not in plaintext:
```
strings game_js.txt | grep -i flag        # nothing
grep -a GREYKEY game_js.txt               # nothing
```
The key and the encoded bytes live in the compiled memory image, not the source
text, so you have to run the program and read its heap.

### 3. Run it and read the heap

The program's static data (the key and the encoded flag bytes) is loaded into the
heap (`Module.HEAPU8`) at startup, before any gameplay. You do not need to play.

Run the JS in a JS engine with the browser/PDF APIs stubbed, then scan the heap.
Two facts make the scan trivial:

- The key `GREYKEY` sits in the heap as a plain 7-byte ASCII string. `strings` on a
  heap dump shows it.
- The encoded flag bytes are 26 bytes that, XORed with the repeating key, start with
  `FLAG{`.

Scan: for every offset `p`, check whether `heap[p..p+5] ^ GREYKEY == "FLAG{"`. When
it matches, decode 25 bytes.

```js
// solve.js  —  node --max-old-space-size=8192 solve.js game.js
var path = process.argv[2];
var Module = { print(){}, printErr(){}, onRuntimeInitialized: scan };
global.Module = Module;
globalThis.getField = () => ({ value: "" });          // stub pdfium form API
global.app = { setInterval(){}, setTimeOut(){} };
global.crypto = { getRandomValues: a => a };           // stub Web Crypto
global.performance = { now: () => 0 };
require(require("path").resolve(path));

function scan() {
  var H = Module.HEAPU8, K = "GREYKEY";
  for (var p = 0; p < H.length - 26; p++) {
    var hit = true;
    for (var j = 0; j < 5; j++)
      if ((H[p + j] ^ K.charCodeAt(j % 7)) !== "FLAG{".charCodeAt(j)) { hit = false; break; }
    if (hit) {
      var f = "";
      for (var k = 0; k < 25; k++) f += String.fromCharCode(H[p + k] ^ K.charCodeAt(k % 7));
      console.log(f);                                   // FLAG{p0k3m0n_1n_4_pdf_gg}
      process.exit(0);
    }
  }
}
```

`solve/solve.js` in this repo is this scanner. The build exports `HEAPU8` so it is
readable.

Note: the JS file is ~190 MB (unoptimized asm.js plus embedded art). Loading it in a
bare node process is slow and occasionally flaky because of the embedded-filesystem
init. A real browser runs it cleanly; if you want the heap interactively, load the
extracted JS in a blank HTML page and inspect `Module.HEAPU8` in DevTools.

### 4. The decode, by hand

If you prefer to do it without the scanner, once you have the 26 encoded bytes
`enc` and the key:

```python
enc = [0x01,0x1e,0x04,0x1e,0x30,0x35,0x69,0x2c,0x61,0x28,0x69,0x25,0x1a,
       0x68,0x29,0x0d,0x71,0x06,0x3b,0x21,0x3f,0x18,0x35,0x22,0x24,0x4b]
key = b"GREYKEY"
print("".join(chr(enc[i] ^ key[i % 7]) for i in range(25)))
# FLAG{p0k3m0n_1n_4_pdf_gg}
```

---

## Limitations

- Battles freeze in the PDF. `FightGUI::Battle()` is a nested blocking loop the
  timer-driven model can't re-enter. Avoid tall grass.
- The base game is a small demo: 3 maps (town, one interior, one route), 1 NPC, 1
  dialogue, plus a full battle engine. The flag does not depend on any of it.
- ~196 MB file. `-O0` build with embedded assets. A full `-O2`/`-Os` build shrinks
  it but the minify step is very slow under emulation.
