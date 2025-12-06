#!/bin/bash

TARGET=${1:-/mnt/gv0}
FILE=${2:-largefile}
SIZE_MB=${3:-1024}   # 1 GB = 1024 MB

echo "=== Large file benchmark ==="
echo "Target directory : $TARGET"
echo "File name        : $FILE"
echo "Size             : ${SIZE_MB} MB"
echo

cd "$TARGET" || { echo "[-] Cannot cd to $TARGET"; exit 1; }

echo "[*] Writing ${SIZE_MB} MB..."
sync
time dd if=/dev/zero of="$FILE" bs=1M count="$SIZE_MB" oflag=direct

echo
echo "[*] Reading ${SIZE_MB} MB..."
sync
time dd if="$FILE" of=/dev/null bs=1M iflag=direct

echo
echo "[*] Done."
