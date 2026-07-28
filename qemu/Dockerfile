FROM debian:trixie-slim

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        device-tree-compiler \
        e2fsprogs \
        file \
        qemu-system-arm \
        qemu-utils \
        util-linux \
    && rm -rf /var/lib/apt/lists/*

COPY entrypoint.sh guest/inject-camera-videos.sh /usr/local/bin/
COPY guest/init/camera.sh /usr/local/lib/qemu-camera-init
COPY guest/tests/camera-network.sh /usr/local/lib/guest-camera-network-test.sh
COPY guest/init/camera-network-test.sh /usr/local/lib/camera-network-test-init.sh
COPY guest/tests/wifi-camera.sh /usr/local/lib/guest-wifi-camera-test.sh
COPY guest/init/wifi-camera-test.sh /usr/local/lib/wifi-camera-test-init.sh
COPY assets/ /usr/share/qemu-camera/
RUN chmod 0755 \
        /usr/local/bin/entrypoint.sh \
        /usr/local/bin/inject-camera-videos.sh \
        /usr/local/lib/qemu-camera-init \
        /usr/local/lib/guest-camera-network-test.sh \
        /usr/local/lib/camera-network-test-init.sh \
        /usr/local/lib/guest-wifi-camera-test.sh \
        /usr/local/lib/wifi-camera-test-init.sh \
    && ln -s /usr/local/bin/entrypoint.sh /usr/local/bin/qemu-rpi4

VOLUME ["/images", "/state"]
ENTRYPOINT ["/usr/local/bin/qemu-rpi4"]
