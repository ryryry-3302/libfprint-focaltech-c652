#!/bin/sh
# Build and install the FocalTech 2808:c652 driver for fprintd (Ubuntu/Debian).
set -eu
cd "$(dirname "$0")/.."

echo "==> Installing build dependencies"
sudo apt-get install -y build-essential meson ninja-build pkg-config \
    libfprint-2-tod-dev libfprint-2-dev libopencv-dev fprintd libpam-fprintd

echo "==> Building"
[ -d build ] || meson setup build
ninja -C build

echo "==> Installing plugin"
sudo meson install -C build --no-rebuild
[ -f /etc/libfprint-focaltech-c652.conf ] || \
    sudo install -m 644 packaging/libfprint-focaltech-c652.conf /etc/libfprint-focaltech-c652.conf

echo "==> Restarting fprintd"
sudo systemctl stop fprintd.service 2>/dev/null || true   # D-Bus activated; restarts on demand

echo
echo "Done. Next:"
echo "  fprintd-enroll -f right-index-finger   # 40 touches; move around your fingertip"
echo "  fprintd-verify"
echo "  sudo pam-auth-update --enable fprintd  # use fingerprint for login and sudo"
