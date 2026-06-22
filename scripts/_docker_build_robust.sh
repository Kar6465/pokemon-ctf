#!/bin/bash
# Container entry for the robust (emulated-host) asm.js build. Mounts:
#   /work          = repo, /root/emsdk = persistent emsdk+cache volume.
# Object files land in /work/game/build (persisted on the host), so the build
# resumes across container/VM restarts.
set -e
export DEBIAN_FRONTEND=noninteractive HOME=/root

echo "==== deps ===="
apt-get update -qq
apt-get install -y -qq git python3 python3-venv python3-pip make wget xz-utils bzip2 ca-certificates >/dev/null

echo "==== emsdk (pinned 1.39.20 fastcomp, persisted) ===="
if [ ! -f /root/emsdk/emsdk ]; then
  # /root/emsdk is the (empty) mounted volume mountpoint — clone INTO it; do not
  # rm it (removing a busy mountpoint fails). git clones fine into an empty dir.
  git clone --branch 1.39.20 https://github.com/emscripten-core/emsdk.git /root/emsdk
fi
cd /root/emsdk
./emsdk install 1.39.20-fastcomp
./emsdk activate 1.39.20-fastcomp
source ./emsdk_env.sh

echo "==== install clang retry-shim (root-cause fix for emulated segfaults) ===="
# The fastcomp clang segfaults stochastically under qemu, on ANY single file
# (including the ~50 files inside a port like SDL2 — which is why whole-port
# retries never converge). Wrap the clang/clang++ entry points so EVERY compiler
# invocation auto-retries on a crash signature. Idempotent + persisted in the
# emsdk volume. clang/clang++ are symlinks to clang-6.0; we replace them with a
# shim that re-execs clang-6.0 under the original argv[0] (preserves driver mode).
BIN="/root/emsdk/fastcomp/fastcomp/bin"
for name in clang clang++; do
  if [ ! -f "$BIN/$name.shimmed" ] && [ -e "$BIN/clang-6.0" ]; then
    rm -f "$BIN/$name"
    cat > "$BIN/$name" <<'SHIM'
#!/bin/bash
REAL="$(dirname "$0")/clang-6.0"
nm="$(basename "$0")"
err="$(mktemp)"; n=0
while :; do
  ( exec -a "$nm" "$REAL" "$@" ) 2>"$err"; rc=$?
  if [ "$rc" -eq 0 ]; then cat "$err" >&2; rm -f "$err"; exit 0; fi
  if grep -qiE "segmentation fault|failed due to signal|core dumped|bus error|aborted" "$err"; then
    n=$((n+1)); if [ "$n" -ge 100 ]; then cat "$err" >&2; rm -f "$err"; exit "$rc"; fi
    continue
  fi
  cat "$err" >&2; rm -f "$err"; exit "$rc"
done
SHIM
    chmod +x "$BIN/$name"
    touch "$BIN/$name.shimmed"
    echo "  shimmed $BIN/$name"
  fi
done

# Same treatment for node, but the root cause is V8's JIT: under qemu emulation,
# V8's runtime-generated machine code crashes (segfault -11 / abort -6), exactly
# like clang did. --jitless runs V8's pure interpreter (no runtime codegen), which
# is emulation-stable (slower, but correct). Plus a big heap (the single-file JS is
# ~47 MB) and a retry as backstop.
NODE="/root/emsdk/node/12.18.1_64bit/bin/node"
# Move the real binary aside once...
if [ -f "$NODE" ] && [ ! -f "$NODE.real" ]; then mv "$NODE" "$NODE.real"; fi
# ...but always (re)write the wrapper so edits here take effect on re-run.
if [ -f "$NODE.real" ]; then
  cat > "$NODE" <<'NSHIM'
#!/bin/bash
REAL="$0.real"
err="$(mktemp)"; n=0
while :; do
  "$REAL" --jitless --max-old-space-size=4096 "$@" 2>"$err"; rc=$?
  if [ "$rc" -eq 0 ]; then cat "$err" >&2; rm -f "$err"; exit 0; fi
  if [ "$rc" -gt 128 ] || grep -qiE "segmentation fault|core dumped|aborted|out of memory" "$err"; then
    n=$((n+1)); if [ "$n" -ge 60 ]; then cat "$err" >&2; rm -f "$err"; exit "$rc"; fi
    continue
  fi
  cat "$err" >&2; rm -f "$err"; exit "$rc"
done
NSHIM
  chmod +x "$NODE"
  echo "  shimmed node (--jitless + 4 GB heap + retry)"
fi

echo "==== per-file retry build ===="
cd /work/game
bash /work/scripts/_emcc_retry_build.sh

echo "==== build complete ===="
ls -lh /work/game/out/game.js
