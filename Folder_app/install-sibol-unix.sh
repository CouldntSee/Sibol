#!/usr/bin/env bash
# ============================================================
#  SIBOL desktop installer - macOS and Linux, fully offline.
#  Keep this beside SIBOL.html, then:
#     chmod +x install-sibol-unix.sh && ./install-sibol-unix.sh
#  Uninstall:  ./install-sibol-unix.sh uninstall
# ============================================================
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APPDIR="$HOME/.local/share/SIBOL"
HTML=""

for cand in "$HERE/SIBOL.html" "$HERE/index.html" "$APPDIR/SIBOL.html"; do
  [ -f "$cand" ] && HTML="$cand" && break
done

if [ -z "$HTML" ]; then
  echo "SIBOL.html not found. Put it beside this script: $HERE" >&2
  exit 1
fi

uninstall() {
  rm -f "$HOME/Desktop/SIBOL.desktop" "$HOME/.local/share/applications/SIBOL.desktop"
  rm -f "$HOME/Desktop/SIBOL.command" "$HOME/Applications/SIBOL.command"
  rm -rf "$APPDIR"
  echo "SIBOL removed."
}
if [ "${1:-}" = "uninstall" ]; then uninstall; exit 0; fi

mkdir -p "$APPDIR"
cp -f "$HTML" "$APPDIR/SIBOL.html"
TARGET="$APPDIR/SIBOL.html"

# ---- icon: write an SVG, rasterise if a converter exists ----
cat > "$APPDIR/sibol.svg" <<'SVG'
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 128 128" width="128" height="128">
  <rect width="128" height="128" rx="28" fill="#07130C"/>
  <path d="M64 106V58" stroke="#1FC77A" stroke-width="9" stroke-linecap="round"/>
  <path d="M64 60C64 36 44 16 20 16c0 24 20 44 44 44Z" fill="#C9F75C"/>
  <path d="M64 72c0-20 18-36 40-36 0 20-18 36-40 36Z" fill="#1FC77A"/>
  <circle cx="64" cy="108" r="7" fill="#E9DCC3"/>
</svg>
SVG

ICON="$APPDIR/sibol.svg"
if command -v rsvg-convert >/dev/null 2>&1; then
  rsvg-convert -w 256 -h 256 "$APPDIR/sibol.svg" -o "$APPDIR/sibol.png" && ICON="$APPDIR/sibol.png"
elif command -v convert >/dev/null 2>&1; then
  convert -background none -resize 256x256 "$APPDIR/sibol.svg" "$APPDIR/sibol.png" && ICON="$APPDIR/sibol.png"
elif command -v qlmanage >/dev/null 2>&1; then
  qlmanage -t -s 256 -o "$APPDIR" "$APPDIR/sibol.svg" >/dev/null 2>&1 || true
  [ -f "$APPDIR/sibol.svg.png" ] && mv "$APPDIR/sibol.svg.png" "$APPDIR/sibol.png" && ICON="$APPDIR/sibol.png"
fi

launch_line() {
  case "$(uname -s)" in
    Darwin) printf 'open -a "Google Chrome" --args --app="file://%s" --window-size=1480,940' "$TARGET" ;;
    *)      printf 'sh -c '\''command -v google-chrome >/dev/null && google-chrome --app="file://%s" || command -v chromium >/dev/null && chromium --app="file://%s" || xdg-open "file://%s"'\''' "$TARGET" "$TARGET" "$TARGET" ;;
  esac
}

if [ "$(uname -s)" = "Darwin" ]; then
  mkdir -p "$HOME/Applications"
  CMD="$HOME/Applications/SIBOL.command"
  {
    echo '#!/bin/bash'
    echo 'H="file://'"$TARGET"'"'
    echo 'C="/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"'
    echo 'E="/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge"'
    echo 'if [ -x "$C" ]; then "$C" --app="$H" --window-size=1480,940 --no-first-run &'
    echo 'elif [ -x "$E" ]; then "$E" --app="$H" --window-size=1480,940 --no-first-run &'
    echo 'else open "$H"; fi'
  } > "$CMD"
  chmod +x "$CMD"
  ln -sf "$CMD" "$HOME/Desktop/SIBOL.command" 2>/dev/null || cp -f "$CMD" "$HOME/Desktop/SIBOL.command"
  echo "Installed: $CMD  (also on Desktop)"
else
  mkdir -p "$HOME/.local/share/applications" "$HOME/Desktop"
  ENTRY="$HOME/.local/share/applications/SIBOL.desktop"
  {
    echo "[Desktop Entry]"
    echo "Type=Application"
    echo "Version=1.0"
    echo "Name=SIBOL"
    echo "GenericName=Offline Node App"
    echo "Comment=SIBOL offline ESP32 grading node"
    echo "Exec=$(launch_line)"
    echo "Icon=$ICON"
    echo "Terminal=false"
    echo "Categories=Utility;Science;Education;"
    echo "StartupNotify=true"
  } > "$ENTRY"
  chmod +x "$ENTRY"
  cp -f "$ENTRY" "$HOME/Desktop/SIBOL.desktop"
  chmod +x "$HOME/Desktop/SIBOL.desktop"
  command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$HOME/.local/share/applications" || true
  echo "Installed: $ENTRY  (also on Desktop)"
fi

cat <<'NOTE'

To run a demo:
  1. Power the node.
  2. Join Wi-Fi  SIBOL-NODE  (open, no password).
  3. Open SIBOL from the Desktop or app menu.
  4. Enter the IP and CODE shown on the OLED.

No internet is used at any point.
NOTE