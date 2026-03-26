#!/usr/bin/env bash
set -e

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="$ROOT_DIR/build/initramfs_tmp"

echo "Generating initramfs in ${OUT_DIR}..."

# 🔥 CLEAN OLD INITRAMFS (CRUCIAL)
rm -rf "${OUT_DIR}"
mkdir -p "${OUT_DIR}/bin"

# Check toybox
TOYBOX_BIN="$ROOT_DIR/build/toybox"
if [[ ! -f "$TOYBOX_BIN" ]]; then
    echo "Error: Toybox binary not found at $TOYBOX_BIN"
    exit 1
fi

# Copy toybox
cp "$TOYBOX_BIN" "${OUT_DIR}/bin/toybox"
chmod +x "${OUT_DIR}/bin/toybox"

# Symlink /bin/sh to toybox (relative)
ln -sf toybox "${OUT_DIR}/bin/sh"

# Create dirs
mkdir -p "${OUT_DIR}/dev" \
         "${OUT_DIR}/proc" \
         "${OUT_DIR}/sys" \
         "${OUT_DIR}/tmp" \
         "${OUT_DIR}/etc" \
         "${OUT_DIR}/home"

# Minimal init
cat > "${OUT_DIR}/init" << 'EOF'
#!/bin/sh
echo "Welcome to MeowOS!"

mount -t proc none /proc
mount -t sysfs none /sys

exec /bin/toybox sh
EOF

chmod +x "${OUT_DIR}/init"

# 🔥 Build BOTH: .cpio and .gz
pushd "${OUT_DIR}" > /dev/null

find . | cpio -H newc -o > "${ROOT_DIR}/build/initramfs.cpio"
gzip -kf "${ROOT_DIR}/build/initramfs.cpio"

popd > /dev/null

echo "Initramfs created:"
echo " - ${ROOT_DIR}/build/initramfs.cpio"
echo " - ${ROOT_DIR}/build/initramfs.cpio.gz"