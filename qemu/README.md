# Raspberry Pi 4 QEMU test environment

This container runs the generated Camera OS kernel, DTB and SD-card image on
QEMU's `raspi4b` machine. Debian 13 supplies QEMU 10, which is new enough to
include Raspberry Pi 4B emulation. The image builds natively on Docker Desktop
for Linux, macOS (Intel or Apple Silicon), and Windows.

## Prerequisites

- Docker Engine with Compose v2, or Docker Desktop
- A completed OS build in `output-images/`
- About 3 GB of free disk space for the QEMU image and writable SD copy

No host QEMU installation or privileged container is required. QEMU uses
software emulation, so it is slower than a physical Pi.

## Build and run

From the repository root:

```sh
./qemu-build.sh
./qemu-run.sh
```

Log in as `root` with an empty password. Stop QEMU with `Ctrl+A`, then `X`.
Changes are written to the Docker volume `qemu-rpi4-state` while the original
`output-images/sdcard.img` remains read-only.

Reset the emulated SD card to the latest source image with:

```sh
./qemu-reset.sh
```

After rebuilding Camera OS, run the reset command before booting it in QEMU.

Run an unattended boot smoke test with:

```sh
./qemu-smoke-test.sh
```

It succeeds only after the Pi 4 machine is detected, the ext4 root filesystem
is mounted, and the Camera OS login prompt appears.

## Prerecorded cameras

On first initialization, the runner adds four deterministic, three-second Y4M
recordings to the writable SD copy. An init script exposes them as regular
video files at `/dev/vid1` through `/dev/vid4`; the source recordings remain at
`/opt/qemu-camera/vid1.y4m` through `vid4.y4m`. Each file contains a distinct,
moving color-bar pattern at 160x120, 30 fps and can be replayed indefinitely:

```sh
ffprobe /dev/vid1
ffmpeg -stream_loop -1 -i /dev/vid1 -t 10 -f null -
```

These paths are prerecorded **files**, not V4L2 devices, so applications must
use file input rather than `v4l2src`/`/dev/videoN`. This is the closest reliable
camera substitute on QEMU's Pi 4 model because it has no CSI sensor or Unicam
emulation. Run `./qemu-reset.sh` after changing the bundled recordings or when
upgrading an older persistent SD volume.

Equivalent Compose commands are:

```sh
docker compose -f docker-compose.qemu.yml build
docker compose -f docker-compose.qemu.yml run --rm rpi4
```

## Configuration

The entrypoint accepts these environment variables:

| Variable | Default | Purpose |
|---|---|---|
| `SD_SOURCE` | `/images/sdcard.img` | Read-only Buildroot SD image |
| `KERNEL` | `/images/Image` | Directly loaded ARM64 kernel |
| `DTB` | `/images/bcm2711-rpi-4-b.dtb` | Pi 4 device tree |
| `SD_WORK` | `/state/sdcard-rpi4.img` | Persistent writable SD copy |
| `SD_SIZE` | `2G` | Power-of-two QEMU SD capacity |
| `CONSOLE` | `ttyAMA1` | QEMU PL011 guest console |
| `SERIAL_BACKEND` | `stdio` | QEMU serial backend |
| `KERNEL_APPEND` | see entrypoint | Complete kernel command line override |
| `QEMU_DISPLAY` | `none` | QEMU display backend |
| `RESET_SD` | `0` | Recreate the writable SD copy |
| `INJECT_CAMERA_VIDEOS` | `1` | Add `/dev/vid1` ... `/dev/vid4` recordings to a new SD copy |

Extra QEMU arguments can be placed after `./qemu-run.sh`. For example, an
experimental user-mode USB network adapter can be requested with:

```sh
./qemu-run.sh \
  -netdev user,id=net0,hostfwd=tcp::2222-:22 \
  -device usb-net,netdev=net0
```

USB networking depends on the guest kernel and the incomplete Raspberry Pi USB
model and is not guaranteed. It is not enabled by default.

## Emulated hardware

QEMU `raspi4b` provides:

- Four Cortex-A72 CPU cores
- Raspberry Pi BCM2711 interrupt, timer, mailbox, DMA and GPIO blocks
- PL011 and auxiliary UARTs
- SD/MMC controller and writable SD storage
- Basic VideoCore firmware mailbox and framebuffer
- USB2, SPI and I2C controller models

The kernel identifies the machine as `Raspberry Pi 4 Model B` and the generated
root filesystem boots from `/dev/mmcblk1p2` in QEMU.

## Important QEMU limitations

QEMU 10 does **not** implement the Pi 4 PCIe root port or BCM2711 GENET Ethernet
controller. It also does not emulate the Raspberry Pi CSI camera pipeline,
physical camera sensors, Wi-Fi radio, GPU/V4L2 codecs, or a real NIC with native
XDP offload. The `/dev/vid1` ... `/dev/vid4` test inputs are therefore regular
prerecorded files rather than emulated camera devices. Consequently this
environment is suitable for:

- Kernel and root-filesystem boot testing
- Init scripts and package presence
- ARM64 userspace execution
- bpftool and BPF object inspection
- Generic BPF syscall/map/program tests that do not need a physical NIC

Use a physical Raspberry Pi 4 for CSI camera, Wi-Fi, GENET Ethernet, driver-mode
XDP, throughput, latency and hardware codec validation. The repository's
network-namespace harness can test filter behavior on the Linux host separately.

Warnings that QEMU disabled `bcm2711-pcie`, `bcm2711-rng200`,
`bcm2711-thermal`, and `bcm2711-genet-v5` are expected.
