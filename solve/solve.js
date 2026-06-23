#!/usr/bin/env node
// Official solver. Works on the extracted JS OR directly on the PDF, because the
// obfuscated save data is stored as plain text in the embedded script.
//
//   node solve/solve.js pokemon_ctf.pdf
//   node solve/solve.js game.js
//
// Method: find the save key and the XOR'd trainer record, XOR them, print the flag.
const fs = require("fs");
const data = fs.readFileSync(process.argv[2] || "pokemon_ctf.pdf", "latin1");

const key = (data.match(/SAVE_KEY\s*=\s*"([^"]+)"/) || [])[1];
const recM = data.match(/SAVE_REC\s*=\s*\[([\s\S]*?)\]/);
if (!key || !recM) { console.error("save key/record not found"); process.exit(1); }

const rec = recM[1].match(/0x[0-9a-fA-F]+|\d+/g).map(Number);
let flag = "";
for (let i = 0; i < rec.length; i++) {
  const c = rec[i] ^ key.charCodeAt(i % key.length);
  if (c === 0) break;            // stop at the XOR'd NUL terminator
  flag += String.fromCharCode(c);
}
console.log(flag);                // FLAG{p0k3m0n_1n_4_pdf_gg}
