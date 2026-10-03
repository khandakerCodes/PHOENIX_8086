#!/bin/bash
# SPDX-License-Identifier: MIT
# ============================================================================
# Phoenix-8086 — Fetch the ia16-elf toolchain (no root required)
# ============================================================================
#
# Downloads pinned gcc-ia16 / binutils-ia16 packages from the toolchain
# maintainer's Ubuntu archive (ppa:tkchia/build-ia16), verifies their
# SHA-256 checksums, and unpacks them into .toolchain/ in the project root.
# The Makefile picks up .toolchain/usr/bin automatically.
#
# If you prefer a system-wide install instead:
#   sudo add-apt-repository ppa:tkchia/build-ia16
#   sudo apt install gcc-ia16-elf
# ============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
DEST="$PROJECT_DIR/.toolchain"
BASE="https://ppa.launchpadcontent.net/tkchia/build-ia16/ubuntu"

# path  sha256
PACKAGES="
pool/main/b/binutils-ia16-elf/binutils-ia16-elf_2.39-20260423.22-ppa260423231~noble_amd64.deb 01112d3b45ac9ab04a750488caadb8c4855d99d95c7713c807dc34649c8c7c88
pool/main/g/gcc-ia16-elf/gcc-ia16-elf_6.3.0-20260612.00-ppa260612104~noble_amd64.deb 4a23a16d38366398b9c540745ccc46537d152517f96a324cf69c3d71db67e62d
"

for tool in curl sha256sum dpkg-deb; do
    if ! command -v "$tool" &>/dev/null; then
        echo "ERROR: '$tool' not found. Please install it."
        exit 1
    fi
done

mkdir -p "$DEST/debs"

echo "$PACKAGES" | while read -r path sum; do
    [ -z "$path" ] && continue
    file="$DEST/debs/$(basename "$path")"
    if [ ! -f "$file" ]; then
        echo "[FETCH] $(basename "$path")"
        curl -fsSL -o "$file" "$BASE/$path"
    fi
    echo "$sum  $file" | sha256sum -c --quiet -
    echo "[UNPACK] $(basename "$path")"
    dpkg-deb -x "$file" "$DEST"
done

echo ""
"$DEST/usr/bin/ia16-elf-gcc" --version | head -1
echo "Toolchain installed in $DEST"
