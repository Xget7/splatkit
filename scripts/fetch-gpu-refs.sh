#!/usr/bin/env bash
# Downloads Apple's Metal Shading Language specification and Metal Feature Set Tables into
# build/gpu-refs/metal (gitignored) as PDF plus UTF-8 text, for grep during GPU work.
# Apple's license allows personal copies only; docs/gpu-refs holds the cited facts we commit.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/build/gpu-refs/metal"
mkdir -p "$DEST"
for NAME in Metal-Shading-Language-Specification Metal-Feature-Set-Tables; do
  URL="https://developer.apple.com/metal/$NAME.pdf"
  echo "fetching $URL"
  curl -sSfL -o "$DEST/$NAME.pdf" -D "$DEST/$NAME.headers" "$URL"
  grep -i '^last-modified' "$DEST/$NAME.headers" || true
  pdftotext "$DEST/$NAME.pdf" - | iconv -f latin1 -t utf-8 > "$DEST/$NAME.txt"
done
ls -la "$DEST"
