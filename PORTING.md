# Porting Pokemon-Grey to render inside a PDF

The CTF goal needs the game to run inside a PDF (via doompdf's asm.js trick). The
one hard blocker is **rendering**: Pokemon-Grey draws with OpenGL, and the PDF
JS sandbox (Chrome's pdfium) has no WebGL, no `<canvas>`, and no SDL video
backend. doompdf works only because DOOM produces a *software* RGBA framebuffer
that its glue paints into PDF form text-fields. So we must do the same.

This document is the implementation spec. It is broken into phases; each phase
is independently testable, and the early phases are testable **natively** (no
x86 Linux / Emscripten needed) so we can validate rendering before touching the
PDF toolchain.

---

## Key observation: the GL layer is already a 2D quad blitter

`Graphics.cpp` does not use real 3D. Every primitive is emitted as
`glBegin(GL_QUADS) … glTexCoord2d/glVertex3d … glEnd()` under an orthographic
camera. The camera is pure 2D:

```
Camera::Perspective():  gluOrtho2D(0, W/zoom, H/zoom, 0); translate(-pos)
Camera::GUI():          gluOrtho2D(0, W, H, 0);            translate(0)
```

So a vertex at world `(X,Y)` lands at screen pixel:

```
screen = (world - cam_pos) * zoom        // world/map draws (Perspective)
screen =  world                          // HUD/menus      (GUI, zoom=1, pos=0)
```

That means we can replace the entire GL backend with **one software quad
rasterizer** plus a 2D transform state. Each `Draw::*` becomes: compute four
screen-space corners + four (u,v), call `blit_quad()`. Regions, tinting, and
rotation all fall out of the same function (they already do in the GL version —
they only differ in the corner/uv math, which we keep verbatim).

---

## Target architecture

```
            game code (unchanged: Map/GUI/Battle call Draw::*)
                              │
                     Draw::*  │  (rewritten: GL → screen-space quads)
                              ▼
                  Gfx::blit_quad(corners, uvs, tex, tint)   ← software rasterizer
                              ▼
                  Gfx::framebuffer  (SDL_Surface RGBA8888, 1080×720)
                              ▼
            Gfx::Present()  ──────────────┬───────────────────────┐
                  native build            │      PDF / emscripten  │
            SDL_Renderer + texture        │   downscale → ASCII →   │
            (a real window, for testing)  │   update_framebuffer()  │
                                          ▼          (EM_ASM)
```

- **Framebuffer**: one `SDL_Surface` in `SDL_PIXELFORMAT_RGBA32`, sized to the
  game's coordinate space (1080×720). All blits are CPU surface writes. No GL,
  no SDL window/renderer needed for the blitting itself.
- **Bitmap**: stops holding a `GLuint` texture; holds an `SDL_Surface*` (the
  source image/text, converted once to `RGBA32` for fast sampling).
- **Transform state**: two globals `g_scale`, `g_off{x,y}` set by
  `Camera::Perspective()`/`GUI()` instead of GL matrices.
- **Present**: `#ifdef __EMSCRIPTEN__` → downscale the 1080×720 buffer to the
  PDF field grid and `EM_ASM(update_framebuffer(...))`; else → blit to a real
  SDL window (so we can *see* it natively).

### Resolution / the PDF grid

The game thinks in 1080×720. A PDF can't have ~777k text fields, so `Present()`
downscales to a small grid (DOOM uses 360×200; start with **320×180**, 16:9).
`generate.py` must be told that grid size (it currently hardcodes 360×200) so
`field_0..field_{H-1}` match. Downscale with `SDL_BlitScaled` then convert each
pixel to an ASCII glyph by luminance (reuse doompdf's ramp in `pre.js`).

---

## Phases

### Phase 1 — Software renderer, validated natively  ✅ DONE
Replace the GL backend; keep the existing native window so we can watch it run.

> **Status: complete and verified.** The overworld + HUD render correctly via the
> software rasterizer (no OpenGL). `make` builds natively; the game runs its main
> loop clean under AddressSanitizer. The GL footprint was wider than just
> `Graphics.cpp` — `Tile.cpp`/`Map.cpp` (tilemap quads) and `GUI.cpp` (battle-
> screen `glClear`/swap) were ported too. Two latent bugs that OpenGL's lenient
> handle semantics had masked were fixed: (a) uninitialized `Mix_Music*`/
> `Mix_Chunk*` in `Sound` (freed garbage → crash), and (b) `Bitmap` shallow-copy
> double-free (it owns an `SDL_Surface*` but is copied by value via Pokemon's
> by-value `operator=`) — fixed with a deep-copy copy ctor/assignment.
> A debug aid remains in `Gfx::Present` (native): `POKEMON_DUMP=<frame>` writes
> that frame to `frame.png` and exits — handy for headless validation.

1. `Graphics.h`: `Bitmap` holds `SDL_Surface* _surf` (not `GLuint`). Add a
   `Gfx` namespace: `Init(w,h)`, `Clear(rgba)`, `Present()`, `blit_quad(...)`,
   plus the `g_scale/g_off` transform globals.
2. `Graphics.cpp`:
   - `Bitmap::Load` → `IMG_Load` then `SDL_ConvertSurfaceFormat(...RGBA32)`.
   - `Bitmap::LoadText` → `TTF_RenderText_Blended` then convert to RGBA32.
   - Implement `blit_quad(Point2D c[4], float u[4], float v[4], SDL_Surface* tex,
     Color tint)`: rasterize as 2 triangles, barycentric-interpolate (u,v),
     nearest-sample `tex` (matches the old `GL_NEAREST`), multiply by `tint`,
     source-over alpha blend into the framebuffer. Colored primitives
     (`Rectangle/Line/Circle`) call it with `tex=nullptr` (sample = white).
   - Rewrite each `Draw::*` to compute the same corners/uvs the GL code used
     (the math is already in the file — copy it), apply the transform, call
     `blit_quad`.
   - `Camera::Perspective/GUI` set `g_scale/g_off` instead of GL matrices.
3. `main.cpp`:
   - `Create_window`: drop `SDL_WINDOW_OPENGL` / `SDL_GL_CreateContext`; create
     a normal window + `SDL_CreateRenderer` (software). Call `Gfx::Init(w,h)`.
   - `glClear(...)` → `Gfx::Clear(...)`; `SDL_GL_SwapWindow` → `Gfx::Present()`.
4. Remove GL/GLU includes (`Basics.h`, `Graphics.h`).
5. **Test**: `make` natively, run on a desktop, confirm the overworld + a battle
   render correctly with a real window. This proves the rasterizer before any
   PDF work.

### Phase 2 — Emscripten present path  ✅ DONE (code; run needs x86 build)
1. ✅ `Gfx::Present()` under `__EMSCRIPTEN__`: `SDL_BlitScaled` framebuffer →
   360×200 grid surface, then `EM_ASM` calls `update_framebuffer(ptr,len,w,h)`.
2. ✅ `build_emscripten.sh`: dropped `Music/`+`Sound/` from `--embed-file`,
   dropped `LEGACY_GL_EMULATION` (no GL now), added `EXIT_RUNTIME=0` and
   `EXPORTED_FUNCTIONS` += `_pokemon_tick`,`_pokemon_key`.
3. ⏳ Verify in a browser/PDF on the x86 build (can't run emcc on Apple Silicon).

### Phase 3 — Tick-driven loop + input bridge  ✅ DONE (code)
1. ✅ `main.cpp` split into `Main::SetupGame()` (one-time) + `Main::Frame()` (one
   frame), shared by the native `while`-loop and the exported `pokemon_tick()`
   (`EMSCRIPTEN_KEEPALIVE`). Under `__EMSCRIPTEN__`, `main()` sets up and returns;
   the JS timer drives frames.
2. ✅ `Inputs.cpp`: under `__EMSCRIPTEN__`, `UpdateKeyboardInputs` reads a
   JS-driven table instead of `SDL_PollEvent`; exported
   `pokemon_key(code, down)` (ASCII for letters/digits, `KC_*` for specials).
3. ✅ `patch/pokemon_pre.js`: defines `Module`, `update_framebuffer` (RGBA→ASCII
   into `field_*`), maps buttons/`key_input` → `_pokemon_key`, and
   `app.setInterval("pokemon_loop()", 60)` to call `_pokemon_tick`.

### Phase 3b — Convert nested blocking loops  ⛔ NOT DONE (required for flag)
`FightGUI::Battle()` (and `DialogueGUI`) run their **own** `while` loops that
block until done. That works natively, but the PDF tick model cannot re-enter a
blocking loop (no nested event pump). The overworld runs under the tick model;
**entering a battle would hang in-PDF.** Since the CTF flag fires on battle win,
`Battle()` must be refactored into the same `Setup`/`Frame` tick shape (a small
state machine: intro → menu → move → resolve → win) before the flag is reachable
inside the PDF. This is the main remaining work.

### Phase 4 — Audio stub + asset trim  ✅ DONE (minimal)
✅ Audio init is now non-fatal (`main.cpp`): a missing audio device just disables
sound instead of aborting. ✅ `Music/`+`Sound/` dropped from the embed. The
`Sound`/`Mix_*` calls are safe no-ops when audio isn't open. (`Menu/` at ~34 MB
is graphics and must stay, so the single-file JS is still large.)

### Phase 5 — PDF packaging  ✅ DONE (self-contained; PDF gen verified locally)
1. ✅ `scripts/generate_pdf.py`: our own generator (doompdf technique, AGPL-
   derived) with a 360×200 `field_*` grid (matches `Gfx::GRID_W/H` and
   `pokemon_pre.js`), `console_*`, a `key_input` field, and **Pokemon** control
   buttons (D-pad → z/q/s/d + arrows, OK=space/enter, ESC, TEAM=t, BAG=e).
2. ✅ `scripts/build_pdf.sh`: now self-contained (no doompdf clone) — venv +
   `pdfrw`, concatenate `pokemon_pre.js` + `game.js`, run `generate_pdf.py`.
3. ✅ Verified locally (no emcc needed): `generate_pdf.py` emits a valid 1-page
   PDF containing `field_0..field_199`, the console/input fields, every control
   button, and the embedded bridge.
4. ⏳ Final run (x86 host): `build_emscripten.sh` → `build_pdf.sh`, open in Chrome,
   walk the overworld. Recovering the flag from `Module.HEAPU8`/the JS already
   works (see README) and does not require the battle.

---

## Effort / risk notes

- Phase 1 is the bulk of the code and is **fully testable natively** — do it
  first and get it visually correct.
- The rotated-bitmap blits (`Rotated_BITMAP*`) are used for the KO/throw
  animations; the unified `blit_quad` handles them since rotation only changes
  the corner coordinates (kept from the GL math).
- Performance: a 1080×720 software blit per frame is fine natively; in asm.js
  expect DOOM-like ~tens-of-ms/frame. If too slow, lower the internal
  framebuffer resolution (scale all of `Display_info::width/height`).
- Nothing in this port touches the CTF flag code in `GUI.cpp`.
