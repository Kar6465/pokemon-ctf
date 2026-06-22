// ============================================================================
// Official solver for the "Pokémon in a PDF" CTF.
//
// Intended path (what a player does):
//   1. Open pokemon_ctf.pdf in Chrome -> notice it's a playable game.
//   2. Realize the game is embedded JavaScript (asm.js). Extract it from the PDF
//      (the JS is the document OpenAction; e.g. `mutool show`, pdf-parser.py,
//      pikepdf, or strings). For convenience this script just loads game.js.
//   3. It's an Emscripten C++ build: the flag lives XOR-obfuscated in the
//      program's memory. Run it and read the heap (Module.HEAPU8).
//   4. Recover the 7-byte key from the heap and XOR-scan for "FLAG{".
//
//   Usage:  node --max-old-space-size=8192 solve.js  path/to/game.js
// ============================================================================
var path = process.argv[2] || __dirname + "/../game/out/game.js";

// Stub the PDF runtime so the program initializes in node.
var Module = { noInitialRun: true, print: function(){}, printErr: function(){} };
global.Module = Module;
globalThis.getField = function(){ return { value: "" }; };
global.app = { setInterval: function(){}, setTimeOut: function(){}, alert: function(){} };
global.crypto = { getRandomValues: function(a){ return a; } };
global.performance = { now: function(){ return 0; } };

try { require(require("path").resolve(path)); } catch (e) { /* asm warnings */ }

setTimeout(function () {
  var H = Module.HEAPU8;
  if (!H) { console.log("Module.HEAPU8 not exposed — rebuild with HEAPU8 in EXTRA_EXPORTED_RUNTIME_METHODS"); process.exit(1); }
  console.log("[*] heap bytes:", H.length);

  // (a) Recover the XOR key: scan the heap's ASCII strings for a short all-caps
  //     token (the key 'GREYKEY' sits in static data near the flag logic).
  function asciiAt(o, n){ var s=""; for (var i=0;i<n;i++){ var c=H[o+i]; if(c<32||c>126) return null; s+=String.fromCharCode(c);} return s; }
  var key = null;
  for (var o = 0; o < H.length - 8; o++) {
    if (H[o]===71 && asciiAt(o,7)==="GREYKEY" && (H[o+7]===0)) { key = "GREYKEY"; console.log("[*] found XOR key at heap offset", o, "->", key); break; }
  }
  if (!key) key = "GREYKEY"; // fall back to the known key

  // (b) XOR-scan the heap for a window that decodes to FLAG{...}
  for (var p = 0; p < H.length - 26; p++) {
    var ok = true;
    var want = "FLAG{";
    for (var j = 0; j < want.length; j++) {
      if ((H[p + j] ^ key.charCodeAt(j % key.length)) !== want.charCodeAt(j)) { ok = false; break; }
    }
    if (ok) {
      var flag = "";
      for (var k = 0; k < 25; k++) flag += String.fromCharCode(H[p + k] ^ key.charCodeAt(k % key.length));
      console.log("[+] FOUND encoded flag at heap offset", p);
      console.log("[+] FLAG:", flag);
      process.exit(0);
    }
  }
  console.log("[-] flag pattern not found in heap");
  process.exit(2);
}, 4000);
