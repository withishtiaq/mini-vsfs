#!/usr/bin/env bash
set -euo pipefail

# Simple automated tests for MiniVSFS
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

BUILDER="./mkfs_builder"
ADDER="./mkfs_adder"

if [[ ! -x "$BUILDER" || ! -x "$ADDER" ]]; then
  echo "[tests] Binaries not found. Run: make build"
  exit 1
fi

mkdir -p examples

# 1) Create a fresh filesystem image
echo "[tests] Creating image..."
$BUILDER --image mini.img --size-kib 512 --inodes 256
[[ -f mini.img ]] || (echo "[tests] mini.img not created" && exit 1)

# 2) Add a small file
echo "hello, mini-vsfs" > examples/hello.txt
$ADDER --input mini.img --output mini2.img --file examples/hello.txt
[[ -f mini2.img ]] || (echo "[tests] mini2.img not created" && exit 1)

# 3) Basic sanity: output must be same size as input (filesystem rewrite)
in_size=$(stat -c%s mini.img 2>/dev/null || wc -c < mini.img)
out_size=$(stat -c%s mini2.img 2>/dev/null || wc -c < mini2.img)
if [[ "$in_size" -ne "$out_size" ]]; then
  echo "[tests] size mismatch: $in_size vs $out_size"
  exit 1
fi

# 4) Try adding a larger file (still within 12 blocks)
python3 - <<'PY'
open('examples/40k.bin','wb').write(b'B'*(40*1024))
PY
$ADDER --input mini2.img --output mini3.img --file examples/40k.bin
[[ -f mini3.img ]] || (echo "[tests] mini3.img not created" && exit 1)

echo "[tests] OK ✅"
