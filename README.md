# Pokemon CTF in a PDF

A Pokémon fan game (Pokemon-Grey) running inside a PDF file, with a hidden CTF flag
recoverable via reverse engineering the asm.js memory.

## ⚠️ Build host requirement: x86_64 Linux

The asm.js step needs **Emscripten 1.39.20 fastcomp**, whose clang/llvm binaries
exist **only for x86_64**. On Apple Silicon (or any non-x86 host) the toolchain
must run under emulation, where the 2020-era clang **segfaults randomly** mid-build
(confirmed: it crashed on different ports — freetype, libpng — on successive runs).
Use a native **x86_64 Linux** machine: bare metal, a cloud VM, WSL2 on an Intel PC,
or an `--platform linux/amd64` container *on an x86 host*.

## Quick Start (one command, x86_64 Linux)

```bash
git clone <this-repo> pokemon-ctf && cd pokemon-ctf
bash scripts/build_all.sh        # deps + clone + patch + asm.js + PDF
# → pokemon_ctf.pdf  and  game/out/game.js
```

### Or step by step

```bash
# 1. Install system deps (g++/SDL only needed for the native `make` test build)
sudo apt install git python3 python3-venv make g++ \
    libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev libsdl2-mixer-dev

# 2. Clone the base game + apply the CTF patch (or just run ./setup.sh)
git clone https://github.com/AMasquelier/Pokemon-Grey.git game
cp patch/*.cpp patch/*.h game/
cp patch/Makefile game/Makefile

# 3. Compile to asm.js
cd game && bash ../scripts/build_emscripten.sh

# 4. Build the PDF
cd .. && bash scripts/build_pdf.sh

# 5. Open pokemon_ctf.pdf in Chrome
```

## How It Works

```
Pokemon-Grey C++  →  Emscripten 1.39.20  →  game.js (asm.js)
        + patch/pokemon_pre.js (bridge)  →  generate_pdf.py  →  pokemon_ctf.pdf
```

- **asm.js** (not WASM) because PDF viewers only run JavaScript, not WebAssembly
- **Rendering**: the OpenGL renderer was **ported to a software RGBA framebuffer**
  (`Gfx` in `patch/Graphics.cpp`); each frame is downscaled to a 360×200 grid and
  painted into PDF text fields as ASCII (`update_framebuffer` in `pokemon_pre.js`)
- **Input**: on-screen PDF buttons + a text field → `pokemon_key()` in the JS
- **Loop**: a PDF has no event loop, so `app.setInterval` drives `pokemon_tick()`
- **Self-contained**: no doompdf dependency — we ship our own bridge + generator

> **Status (see `PORTING.md` for the full breakdown).** The renderer port is
> **done and verified** — the overworld renders correctly (proven natively). The
> emscripten present path, the tick loop, the input bridge, and PDF packaging are
> all built; PDF generation is verified locally. **Remaining:** `FightGUI::Battle()`
> still runs a nested blocking loop, which can't work under the PDF tick model, so
> **entering a battle hangs in-PDF** until it is refactored into a state machine
> (PORTING.md "Phase 3b"). The flag is recoverable from the JS without the battle
> (see below).

## CTF Challenge (Level 3 — XOR obfuscation)

When the player wins a battle, `write_ctf_flag()` fires once and copies the
XOR-encoded flag bytes into a global byte buffer `g_flag_buf[]`. The flag is
`FLAG{p0k3m0n_1n_4_pdf_gg}` XOR'd with the repeating key `GREYKEY`. The bytes are
stored **already encoded** in the source (`g_flag_enc[]`) — the plaintext is never
present in the binary, so `strings` on the asm.js will not reveal it. The key
`GREYKEY` is intentionally left findable (as the `XOR_KEY` global).

The challenge is **reverse engineering**, not gameplay: the encoded flag bytes
(`g_flag_enc[]`) and the key (`GREYKEY`) both live in the asm.js. The player
finds the two byte sequences in the minified source and XORs them:

1. Open `pokemon_ctf.pdf` in Chrome → DevTools → Console (or just `strings`/grep
   the `.js`).
2. Locate the XOR key `GREYKEY` and the 26 encoded flag bytes.
3. Decode: `flag[i] = enc[i] ^ "GREYKEY"[i % 7]`.

```js
const key = "GREYKEY";
const enc = [/* the 26 bytes found in the asm.js */];
let flag = "";
for (let i = 0; i < 25; i++)   // flag is 25 bytes (a 26th XOR'd NUL follows)
    flag += String.fromCharCode(enc[i] ^ key.charCodeAt(i % 7));
console.log(flag); // FLAG{p0k3m0n_1n_4_pdf_gg}
```

`write_ctf_flag()` also copies those bytes into `g_flag_buf[]` (live memory,
`Module.HEAPU8`) on a battle win — a second, runtime solve path. That path needs
the battle to run in-PDF (Phase 3b). **If you want the battle to be _mandatory_,
the flag must instead be computed at runtime from battle state** so it isn't in
the file at all — a design choice noted in `PORTING.md`.
