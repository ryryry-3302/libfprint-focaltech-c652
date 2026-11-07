#!/bin/sh
# Remove the FocalTech 2808:c652 driver.
# Delete your enrolled prints first (they only work with this driver):
#   fprintd-delete "$USER"
set -eu
dir=$(pkg-config --variable=tod_driversdir libfprint-2-tod-1 2>/dev/null || echo /usr/lib/x86_64-linux-gnu/libfprint-2/tod-1)
sudo rm -f "$dir/libfprint-2-tod-focaltech-c652.so" /etc/libfprint-focaltech-c652.conf
sudo rm -rf /var/lib/fprint/focaltech-c652          # learned touches
sudo systemctl stop fprintd.service 2>/dev/null || true
echo "Driver removed. If fingerprint login was enabled: sudo pam-auth-update --disable fprintd"
