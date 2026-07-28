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
    test -r /tmp/hostapd-test.pid && kill "$(cat /tmp/hostapd-test.pid)" >/dev/null 2>&1 || true
    for camera in 1 2 3 4; do
        ns="wifi-camera${camera}"
        for pid in $(ip netns pids "$ns" 2>/dev/null); do
            kill "$pid" >/dev/null 2>&1 || true
        done
        ip netns del "$ns" >/dev/null 2>&1 || true
    done
}
trap cleanup EXIT INT TERM

model=$(tr -d '\000' </proc/device-tree/model 2>/dev/null || true)
case "$model" in
    *"Raspberry Pi 4 Model B"*) echo "model=$model" ;;
    *) fail "unexpected machine model: $model" ;;
esac

for command in hostapd wpa_supplicant iw ip tc ffmpeg ffprobe bpftool airport-blocklist; do
    command -v "$command" >/dev/null || fail "required OS command is missing: $command"
done
for camera in 1 2 3 4; do
    test -r "/dev/vid${camera}" || fail "/dev/vid${camera} is missing"
done

echo "=== Creating five software-defined Wi-Fi radios ==="
if ! iw dev wlan0 info >/dev/null 2>&1; then
    if ! modprobe -v mac80211_hwsim radios=5; then
        lsmod >&2
        dmesg | tail -80 >&2
        fail "could not initialize mac80211_hwsim"
    fi
    sleep 2
fi
for radio in 0 1 2 3 4; do
    iw dev "wlan${radio}" info >/dev/null 2>&1 || fail "wlan${radio} was not created"
done

echo "=== Starting WPA2 camera access point ==="
cat >/tmp/qemu-camera-hostapd.conf <<'EOF'
interface=wlan0
driver=nl80211
ssid=QEMU-Camera-Lab
hw_mode=g
channel=6
country_code=US
ieee80211d=1
wmm_enabled=1
auth_algs=1
wpa=2
wpa_key_mgmt=WPA-PSK
wpa_passphrase=qemu-camera-test
rsn_pairwise=CCMP
EOF

ip link set wlan0 down
ip address flush dev wlan0
hostapd -B -P /tmp/hostapd-test.pid -f /tmp/hostapd-test.log \
    /tmp/qemu-camera-hostapd.conf || {
    cat /tmp/hostapd-test.log >&2
    fail "hostapd failed"
}
ip address add 10.30.0.1/24 dev wlan0
ip link set wlan0 up

cat >/tmp/qemu-camera-wpa.conf <<'EOF'
ctrl_interface=/run/wpa_supplicant
network={
    ssid="QEMU-Camera-Lab"
    psk="qemu-camera-test"
    key_mgmt=WPA-PSK
    scan_ssid=1
}
EOF

echo "=== Associating four isolated camera stations ==="
for camera in 1 2 3 4; do
    ns="wifi-camera${camera}"
    iface="wlan${camera}"
    phy=$(basename "$(readlink "/sys/class/net/${iface}/phy80211")")
    ip netns add "$ns"
    iw phy "$phy" set netns name "$ns" || fail "could not move $phy to $ns"
    ip -n "$ns" link set lo up
    ip -n "$ns" link set "$iface" up
    ip netns exec "$ns" wpa_supplicant -B -i "$iface" -D nl80211 \
        -c /tmp/qemu-camera-wpa.conf \
        -P "/tmp/wpa-camera${camera}.pid" \
        -f "/tmp/wpa-camera${camera}.log" || fail "wpa_supplicant failed for camera${camera}"
done

elapsed=0
while test "$elapsed" -lt 30; do
    associated=1
    for camera in 1 2 3 4; do
        ip netns exec "wifi-camera${camera}" iw dev "wlan${camera}" link 2>/dev/null |
            grep -q '^Connected to ' || associated=0
    done
    test "$associated" -eq 1 && break
    sleep 1
    elapsed=$((elapsed + 1))
done

for camera in 1 2 3 4; do
    ns="wifi-camera${camera}"
    iface="wlan${camera}"
    if ! ip netns exec "$ns" iw dev "$iface" link | grep -q '^Connected to '; then
        cat "/tmp/wpa-camera${camera}.log" >&2
        fail "camera${camera} did not associate with the WPA2 access point"
    fi
    ip -n "$ns" address add "10.30.0.$((10 + camera))/24" dev "$iface"
    ip netns exec "$ns" ping -c 2 -W 2 10.30.0.1 >/dev/null || fail "camera${camera} cannot reach access point"
    echo "PASS: camera${camera} associated as 10.30.0.$((10 + camera))"
done

echo "=== Attaching the XDP front end and TC pipeline to the AP ==="
mkdir -p /sys/fs/bpf
mountpoint -q /sys/fs/bpf || mount -t bpf bpf /sys/fs/bpf || fail "could not mount bpffs"
rm -rf /sys/fs/bpf/airport-defense
mkdir -m 0700 /sys/fs/bpf/airport-defense
bpftool prog load /usr/local/sbin/xdp_frontend.bpf.o \
    /sys/fs/bpf/airport-defense/xdp_frontend type xdp \
    pinmaps /sys/fs/bpf/airport-defense || fail "could not load and pin XDP front end"
ip link set dev wlan0 xdpgeneric pinned /sys/fs/bpf/airport-defense/xdp_frontend ||
    fail "could not attach the generic XDP front end to wlan0"
ip -details link show dev wlan0 | grep -q 'prog/xdp' ||
    fail "XDP front end was not reported on wlan0"
echo "PASS: XDP validation front end attached in generic mode"

echo "=== Testing exact, prefix, and Bloom deny lists ==="
airport-blocklist exact add 10.30.0.14
if ip netns exec wifi-camera4 ping -c 1 -W 1 10.30.0.1 >/dev/null 2>&1; then
    fail "exact deny list did not block camera4"
fi
airport-blocklist exact delete 10.30.0.14
ip netns exec wifi-camera4 ping -c 1 -W 2 10.30.0.1 >/dev/null ||
    fail "camera4 did not recover after exact deny deletion"
echo "PASS: exact IPv4 deny list"

# Reload the program while reusing pinned maps to prove operational updates
# survive upgrades and attachment cycles.
airport-blocklist exact add 10.30.0.13
ip link set dev wlan0 xdp off
rm -f /sys/fs/bpf/airport-defense/xdp_frontend
bpftool prog load /usr/local/sbin/xdp_frontend.bpf.o \
    /sys/fs/bpf/airport-defense/xdp_frontend type xdp \
    map name exact_deny pinned /sys/fs/bpf/airport-defense/exact_deny \
    map name prefix_deny pinned /sys/fs/bpf/airport-defense/prefix_deny \
    map name bloom_bits pinned /sys/fs/bpf/airport-defense/bloom_bits \
    map name defense_cfg pinned /sys/fs/bpf/airport-defense/defense_cfg ||
    fail "could not reload XDP front end with persistent maps"
ip link set dev wlan0 xdpgeneric pinned /sys/fs/bpf/airport-defense/xdp_frontend ||
    fail "could not reattach XDP front end after reload"
if ip netns exec wifi-camera3 ping -c 1 -W 1 10.30.0.1 >/dev/null 2>&1; then
    fail "exact deny entry was lost across XDP reload"
fi
airport-blocklist exact delete 10.30.0.13
ip netns exec wifi-camera3 ping -c 1 -W 2 10.30.0.1 >/dev/null ||
    fail "camera3 did not recover after persistent-map test"
echo "PASS: blocklist maps persist across XDP reload"

airport-blocklist prefix add 10.30.0.0/24
if ip netns exec wifi-camera1 ping -c 1 -W 1 10.30.0.1 >/dev/null 2>&1; then
    fail "prefix deny list did not block camera1"
fi
airport-blocklist prefix delete 10.30.0.0/24
ip netns exec wifi-camera1 ping -c 1 -W 2 10.30.0.1 >/dev/null ||
    fail "camera1 did not recover after prefix deny deletion"
echo "PASS: IPv4 LPM prefix deny list"

airport-blocklist bloom add 10.30.0.12
if ip netns exec wifi-camera2 ping -c 1 -W 1 10.30.0.1 >/dev/null 2>&1; then
    fail "Bloom deny list did not block camera2"
fi
airport-blocklist bloom clear
ip netns exec wifi-camera2 ping -c 1 -W 2 10.30.0.1 >/dev/null ||
    fail "camera2 did not recover after Bloom clear"
echo "PASS: Bloom deny list"
airport-blocklist status

tc qdisc replace dev wlan0 clsact || fail "could not create wlan0 clsact qdisc"
for filter in tc_cms_filter tc_elastic_filter; do
    object="/usr/local/sbin/${filter}.bpf.o"
    tc filter replace dev wlan0 ingress bpf direct-action obj "$object" sec classifier ||
        fail "could not attach $filter to wlan0"
    tc filter show dev wlan0 ingress | grep -q 'direct-action' ||
        fail "$filter was not reported on wlan0 ingress"
    echo "PASS: $filter attached as a SCHED_CLS direct-action program"
done

echo "=== Streaming all four cameras concurrently over simulated Wi-Fi ==="
consumer_pids=""
for camera in 1 2 3 4; do
    rm -f "/tmp/wifi-camera${camera}.ok" "/tmp/wifi-camera${camera}-receive.log"
    (
        ffmpeg -nostdin -hide_banner -loglevel error \
            -i "udp://10.30.0.1:$((13000 + camera))?fifo_size=1000000&overrun_nonfatal=1" \
            -frames:v 20 -f null - \
            >"/tmp/wifi-camera${camera}-receive.log" 2>&1 && \
            touch "/tmp/wifi-camera${camera}.ok"
    ) &
    consumer_pids="$consumer_pids $!"
done
sleep 1

producer_pids=""
for camera in 1 2 3 4; do
    ip netns exec "wifi-camera${camera}" \
        ffmpeg -nostdin -hide_banner -loglevel error -stream_loop -1 -re \
        -i "/dev/vid${camera}" -an -c:v libopenh264 -b:v 250k \
        -f mpegts "udp://10.30.0.1:$((13000 + camera))?pkt_size=1316" \
        >"/tmp/wifi-camera${camera}-send.log" 2>&1 &
    producer_pids="$producer_pids $!"
done

elapsed=0
while test "$elapsed" -lt 40; do
    complete=1
    for camera in 1 2 3 4; do
        test -e "/tmp/wifi-camera${camera}.ok" || complete=0
    done
    test "$complete" -eq 1 && break
    sleep 1
    elapsed=$((elapsed + 1))
done

for camera in 1 2 3 4; do
    if ! test -e "/tmp/wifi-camera${camera}.ok"; then
        cat "/tmp/wifi-camera${camera}-send.log" >&2
        cat "/tmp/wifi-camera${camera}-receive.log" >&2
        fail "camera${camera} did not deliver 20 frames over Wi-Fi"
    fi
    echo "PASS: camera${camera} delivered 20 decoded Wi-Fi frames to 10.30.0.1:$((13000 + camera))"
done

echo "PASS: WPA2 association, four camera stations, and concurrent video delivery"
exit 0
