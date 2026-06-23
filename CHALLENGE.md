# Challenge: Pokémon in a PDF

**Category:** Reverse Engineering / Forensics
**Difficulty:** Easy–Medium
**Files given to players:** `pokemon_ctf.pdf` only.

## Prompt

> We pulled this PDF off a machine. Open it in Chrome — it's not a normal document.
> There's a flag in it. Format: `FLAG{...}`

## How to solve (author reference)

1. Open `pokemon_ctf.pdf` in Chrome. It's a playable Pokémon game. A PDF running a
   game means it carries **embedded JavaScript**.
2. Extract that JavaScript. It's the document's open-action script. Any of:
   ```
   mutool show pokemon_ctf.pdf js > game.js
   strings pokemon_ctf.pdf | less
   ```
   (Most of it is a huge compiled blob; the useful part is the short readable
   header at the top.)
3. In that script, find two things sitting near each other:
   ```js
   var SAVE_KEY = "GREYKEY";
   var SAVE_REC = [0x01, 0x1e, 0x04, ...];   // 26 bytes
   ```
   The comment says the record is "obfuscated... XOR'd against the save key."
4. XOR the record with the repeating key:
   ```python
   key = b"GREYKEY"
   rec = [0x01,0x1e,0x04,0x1e,0x30,0x35,0x69,0x2c,0x61,0x28,0x69,0x25,0x1a,
          0x68,0x29,0x0d,0x71,0x06,0x3b,0x21,0x3f,0x18,0x35,0x22,0x24,0x4b]
   print("".join(chr(rec[i]^key[i%7]) for i in range(25)))
   # FLAG{p0k3m0n_1n_4_pdf_gg}
   ```
   Or just run the included solver against the PDF:
   ```
   node solve/solve.js pokemon_ctf.pdf
   ```

**Flag:** `FLAG{p0k3m0n_1n_4_pdf_gg}`

## Staged hints (for a scoreboard)

1. It's not just a document. What can a PDF do besides display text? (It runs JS.)
2. Pull the embedded script out of the PDF. Ignore the giant compiled section —
   read the short, human-written part at the top.
3. There's a key and an "obfuscated save record" right next to each other.
   Obfuscation here = repeating-key XOR.

## Distribution note

`pokemon_ctf.pdf` is ~196 MB (the whole game + assets are embedded). It's too big
for the git repo, so host it as a **GitHub Release asset** or a file link — don't
expect it in the source tree.
