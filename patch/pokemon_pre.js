// ============================================================================
// pokemon_pre.js — the bridge between the asm.js game and the PDF runtime.
// Concatenated BEFORE game.js (see build_pdf.sh). Analogous to doompdf's pre.js,
// but wired to OUR exports (_pokemon_tick / _pokemon_key) and a 360x200 grid
// (must match Gfx::GRID_W/H in Graphics.cpp and the field grid in generate.py).
// ============================================================================

// CRITICAL: the PDF JS sandbox (pdfium) has no Web Crypto, so emscripten's
// std::random_device aborts at startup ("no cryptographic support found for
// random_device") in global constructors — before main() ever runs. Polyfill a
// non-crypto getRandomValues so it doesn't throw. Game RNG (encounters, damage)
// does not need real entropy. This MUST run before game.js initializes.
if (typeof globalThis.crypto === "undefined" || !globalThis.crypto.getRandomValues) {
  globalThis.crypto = {
    getRandomValues: function (a) {
      for (var i = 0; i < a.length; i++) a[i] = (Math.random() * 256) | 0;
      return a;
    }
  };
}

// Same story: the PDF sandbox has no performance.now() (SDL's timer init calls
// emscripten_get_now -> performance.now), and no console. Polyfill both.
if (typeof globalThis.performance === "undefined") {
  globalThis.performance = { now: function () { return Date.now(); } };
}
if (typeof globalThis.console === "undefined") {
  var __noop = function () {};
  globalThis.console = { log: __noop, warn: __noop, error: __noop, info: __noop, debug: __noop };
}

// --- save data is obfuscated before it is written into memory ---------------
// The trainer record is stored XOR'd against the save key, so it isn't legible
// at rest. The game de-obfuscates it on load.
var SAVE_KEY = "GREYKEY";
var SAVE_REC = [
  0x01, 0x1e, 0x04, 0x1e, 0x30, 0x35, 0x69, 0x2c, 0x61, 0x28, 0x69, 0x25, 0x1a,
  0x68, 0x29, 0x0d, 0x71, 0x06, 0x3b, 0x21, 0x3f, 0x18, 0x35, 0x22, 0x24, 0x4b
];

// Emscripten respects a pre-existing Module, so define it before game.js loads.
var Module = {};

// ---- console/debug text fields (field names console_0..console_24) ----------
var __lines = [];
function print_msg(msg) {
  __lines.push(msg);
  if (__lines.length > 25) __lines.shift();
  for (var i = 0; i < __lines.length; i++) {
    try { globalThis.getField("console_" + (25 - i - 1)).value = __lines[i]; } catch (e) {}
  }
}
Module.print    = function (m) { print_msg(String(m).substr(0, 80)); };
Module.printErr = function (m) { print_msg(String(m).substr(0, 80)); };

// ---- framebuffer -> ASCII text fields ---------------------------------------
// C calls update_framebuffer(ptr,len,w,h) once per frame (Gfx::Present, EM_ASM).
// Each pixel's brightness picks a glyph; each row goes into field_<h-1-y>.
// (Glyphs chosen to be ~equal width in Chrome's text-field font, per doompdf.)
var __rows = [];
function update_framebuffer(ptr, len, w, h) {
  var mem = Module.HEAPU8.subarray(ptr, ptr + len);
  for (var y = 0; y < h; y++) {
    var s = "";
    var base = y * w * 4;
    for (var x = 0; x < w; x++) {
      var i = base + x * 4;
      var avg = (mem[i] + mem[i + 1] + mem[i + 2]) / 3;
      if      (avg > 200) s += "_";
      else if (avg > 150) s += "::";
      else if (avg > 100) s += "?";
      else if (avg > 50)  s += "//";
      else if (avg > 25)  s += "b";
      else                s += "#";
    }
    if (__rows[y] !== s) {
      __rows[y] = s;
      try { globalThis.getField("field_" + (h - y - 1)).value = s; } catch (e) {}
    }
  }
}

// ---- input bridge -----------------------------------------------------------
// Translate a character/token to the code pokemon_key() expects (see Inputs.cpp:
// ASCII for letters/digits; KC_* specials for arrows/space/enter/esc/shift).
function pk_code(ch) {
  if (!ch) return -1;
  if (ch.length === 1) {
    var c = ch.toLowerCase();
    // WASD controls: the game internally uses AZERTY movement (z=up, q=left),
    // so map physical w->z (up) and a->q (left). s/d already match.
    if (c === "w") c = "z";
    else if (c === "a") c = "q";
    return c.charCodeAt(0);
  }
  switch (ch) {
    case "up": return 128; case "down": return 129;
    case "left": return 130; case "right": return 131;
    case "enter": return 13; case "esc": return 27;
    case "space": return 32; case "shift": return 16;
  }
  return -1;
}
function pk_set(code, down) {
  if (code < 0) return;
  try {
    if (typeof _pokemon_key === "function") _pokemon_key(code, down);
    else if (Module && Module._pokemon_key) Module._pokemon_key(code, down);
  } catch (e) {}
}
// On-screen D-pad/action buttons send tokens that may map to SEVERAL keys, so a
// single button works in both the overworld (z/q/s/d) and menus (arrows).
var PAD = {
  padu: [128, 122 /*z*/], padd: [129, 115 /*s*/],
  padl: [130, 113 /*q*/], padr: [131, 100 /*d*/],
  ok:   [32, 13],         cancel: [27],
  team: [116 /*t*/],      bag:    [101 /*e*/]
};
function key_codes(ch) {
  if (PAD[ch]) return PAD[ch];
  var c = pk_code(ch);
  return c < 0 ? [] : [c];
}
// Held buttons (PDF form buttons fire these on mouse down / up).
function key_down(ch) { var a = key_codes(ch), i; for (i = 0; i < a.length; i++) pk_set(a[i], 1); }
function key_up(ch)   { var a = key_codes(ch), i; for (i = 0; i < a.length; i++) pk_set(a[i], 0); }
// Button TAP: a quick click holds a key for only a few ms — shorter than the
// game's input poll, so it gets missed. Pulse it (down, auto-release ~280ms) the
// same way typing does, so one click = one reliable step.
function key_btn(ch) {
  var a = key_codes(ch), i;
  for (i = 0; i < a.length; i++) pk_set(a[i], 1);
  try { app.setTimeOut("key_btn_up('" + ch + "')", 280); }
  catch (e) { for (i = 0; i < a.length; i++) pk_set(a[i], 0); }
}
function key_btn_up(ch) { var a = key_codes(ch), i; for (i = 0; i < a.length; i++) pk_set(a[i], 0); }
// Typing into the key_input field: pulse the key (press, auto-release shortly).
function key_pressed(ch) {
  var c = pk_code(ch);
  if (c < 0) return;
  pk_set(c, 1);
  // Hold the key long enough that the (low-framerate) game loop reliably samples
  // it; too short and presses get missed ("stuck"). For continuous movement,
  // holding the on-screen D-pad buttons is smoother than key auto-repeat.
  try { app.setTimeOut("pk_release(" + c + ")", 260); } catch (e) { pk_set(c, 0); }
}
function pk_release(c) { pk_set(c, 0); }
function reset_input_box() {
  try { globalThis.getField("key_input").value = "Type here to play (W/A/S/D, space, enter)."; } catch (e) {}
}
try { app.setInterval("reset_input_box()", 1000); } catch (e) {}

// ---- main loop: drive one frame per timer tick ------------------------------
// pre.js is evaluated before game.js defines _pokemon_tick, so poll until ready.
var __run_held = false;
function pokemon_loop() {
  try {
    var tick = (typeof _pokemon_tick === "function") ? _pokemon_tick
             : (Module && Module._pokemon_tick) ? Module._pokemon_tick : null;
    if (!tick) return;
    // Hold Shift permanently => the game's built-in "run" speed (faster walking).
    if (!__run_held) { pk_set(16 /*KC_SHIFT*/, 1); __run_held = true; }
    tick();
  } catch (e) {}
}
try { app.setInterval("pokemon_loop()", 40); } catch (e) {}
