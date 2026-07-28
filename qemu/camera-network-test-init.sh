#!/bin/sh

# Installed in every QEMU SD copy, but only runs for the unattended test boot.
grep -qw 'qemu_camera_test=1' /proc/cmdline || exit 0

echo '__QEMU_CAMERA_TEST_START__'
sh /opt/qemu-camera/network-test.sh
rc=$?
echo "__QEMU_CAMERA_TEST_DONE__:$rc"
sync
poweroff -f
exit "$rc"
