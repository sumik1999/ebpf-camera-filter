#!/bin/sh

# Installed in every QEMU SD copy, but only runs for the unattended Wi-Fi test.
grep -qw 'qemu_wifi_camera_test=1' /proc/cmdline || exit 0

echo '__QEMU_WIFI_CAMERA_TEST_START__'
sh /opt/qemu-camera/wifi-network-test.sh
rc=$?
echo "__QEMU_WIFI_CAMERA_TEST_DONE__:$rc"
sync
poweroff -f
exit "$rc"
