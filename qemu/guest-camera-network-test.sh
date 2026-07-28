#!/bin/sh

fail()
{
    echo "FAIL: $*" >&2
    exit 1
}

cleanup()
{
    for pid in ${producer_pids:-} ${consumer_pids:-}; do
        kill "$pid" >/dev/null 2>&1 || true
    done
}
trap cleanup EXIT INT TERM

echo "=== Raspberry Pi image identity ==="
model=$(tr -d '\000' </proc/device-tree/model 2>/dev/null || true)
echo "model=$model"
case "$model" in
    *"Raspberry Pi 4 Model B"*) ;;
    *) fail "unexpected machine model: $model" ;;
esac

for command in ffmpeg ffprobe gst-launch-1.0 bpftool nft; do
    command -v "$command" >/dev/null || fail "required OS command is missing: $command"
done

echo "=== eBPF object verification ==="
mkdir -p /sys/fs/bpf
mountpoint -q /sys/fs/bpf || mount -t bpf bpf /sys/fs/bpf || fail "could not mount bpffs"
for specification in \
    'xdp_frontend xdp' \
    'xdp_cms_filter xdp' \
    'xdp_elastic_filter xdp' \
    'tc_cms_filter classifier' \
    'tc_elastic_filter classifier'
do
    set -- $specification
    filter=$1
    program_type=$2
    object="/usr/local/sbin/${filter}.bpf.o"
    pin="/sys/fs/bpf/qemu-test-${filter}"
    test -r "$object" || fail "$object is missing"
    rm -f "$pin"
    bpftool prog load "$object" "$pin" type "$program_type" || fail "kernel rejected $object"
    bpftool prog show pinned "$pin" | head -1
    if test "$filter" = xdp_frontend; then
        dd if=/dev/zero of=/tmp/truncated-ip.bin bs=14 count=1 2>/dev/null
        printf '\010\000' | dd of=/tmp/truncated-ip.bin bs=1 seek=12 conv=notrunc 2>/dev/null
        run_result=$(bpftool prog run pinned "$pin" data_in /tmp/truncated-ip.bin) ||
            fail "could not execute XDP front-end packet test"
        echo "$run_result" | grep -q 'Return value: 1' ||
            fail "XDP front end did not drop a truncated IPv4 frame"

        dd if=/dev/zero of=/tmp/truncated-vlan.bin bs=14 count=1 2>/dev/null
        printf '\201\000' | dd of=/tmp/truncated-vlan.bin bs=1 seek=12 conv=notrunc 2>/dev/null
        run_result=$(bpftool prog run pinned "$pin" data_in /tmp/truncated-vlan.bin) ||
            fail "could not execute XDP front-end VLAN test"
        echo "$run_result" | grep -q 'Return value: 1' ||
            fail "XDP front end did not drop a truncated VLAN frame"

        dd if=/dev/zero of=/tmp/vlan-non-ip.bin bs=18 count=1 2>/dev/null
        printf '\201\000' | dd of=/tmp/vlan-non-ip.bin bs=1 seek=12 conv=notrunc 2>/dev/null
        run_result=$(bpftool prog run pinned "$pin" data_in /tmp/vlan-non-ip.bin) ||
            fail "could not execute XDP front-end valid VLAN test"
        echo "$run_result" | grep -q 'Return value: 2' ||
            fail "XDP front end did not pass a complete VLAN frame"

        dd if=/dev/zero of=/tmp/non-ip-ethernet.bin bs=14 count=1 2>/dev/null
        run_result=$(bpftool prog run pinned "$pin" data_in /tmp/non-ip-ethernet.bin) ||
            fail "could not execute XDP front-end non-IP test"
        echo "$run_result" | grep -q 'Return value: 2' ||
            fail "XDP front end did not pass a valid non-IP Ethernet frame"
        echo "PASS: XDP front-end Ethernet, VLAN, and malformed-frame decisions"
    fi
    rm -f "$pin"
done

echo "=== Prerecorded camera inputs ==="
for camera in 1 2 3 4; do
    input="/dev/vid${camera}"
    test -r "$input" || fail "$input is missing"
    ffprobe -v error -select_streams v:0 \
        -show_entries stream=codec_name,width,height,r_frame_rate \
        -of default=noprint_wrappers=1 "$input" || fail "ffprobe failed for $input"
done

echo "=== Loopback network ==="
ip link set lo up || fail "could not enable loopback"
ip -brief address show dev lo

# Four concurrent consumers listen on separate loopback addresses while four
# ffmpeg producers replay the corresponding prerecorded camera indefinitely.
consumer_pids=""
for camera in 1 2 3 4; do
    rm -f "/tmp/camera${camera}.ok" "/tmp/camera${camera}-receive.log"
    (
        ffmpeg -nostdin -hide_banner -loglevel error \
            -i "udp://127.0.0.$((10 + camera)):$((12000 + camera))?fifo_size=1000000&overrun_nonfatal=1" \
            -frames:v 20 -f null - \
            >"/tmp/camera${camera}-receive.log" 2>&1 && \
            touch "/tmp/camera${camera}.ok"
    ) &
    consumer_pids="$consumer_pids $!"
done
sleep 1

producer_pids=""
for camera in 1 2 3 4; do
    ffmpeg -nostdin -hide_banner -loglevel error -stream_loop -1 -re \
        -i "/dev/vid${camera}" -an -c:v libopenh264 -b:v 250k \
        -f mpegts "udp://127.0.0.$((10 + camera)):$((12000 + camera))?pkt_size=1316" \
        >"/tmp/camera${camera}-send.log" 2>&1 &
    producer_pids="$producer_pids $!"
done

elapsed=0
while test "$elapsed" -lt 30; do
    complete=1
    for camera in 1 2 3 4; do
        test -e "/tmp/camera${camera}.ok" || complete=0
    done
    test "$complete" -eq 1 && break
    sleep 1
    elapsed=$((elapsed + 1))
done

for camera in 1 2 3 4; do
    if ! test -e "/tmp/camera${camera}.ok"; then
        echo "camera${camera} sender log:" >&2
        cat "/tmp/camera${camera}-send.log" >&2
        echo "camera${camera} receiver log:" >&2
        cat "/tmp/camera${camera}-receive.log" >&2
        fail "camera${camera} did not deliver 20 decoded frames over loopback UDP"
    fi
    echo "PASS: camera${camera} delivered 20 decoded frames at 127.0.0.$((10 + camera)):$((12000 + camera))"
done

echo "PASS: OS boot, four video inputs, ffmpeg encode/decode, and four-camera loopback network"
exit 0
