FROM debian:trixie-slim

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        device-tree-compiler \
        file \
        qemu-system-arm \
        qemu-utils \
    && rm -rf /var/lib/apt/lists/*

COPY entrypoint.sh /usr/local/bin/qemu-rpi4
RUN chmod 0755 /usr/local/bin/qemu-rpi4

VOLUME ["/images", "/state"]
ENTRYPOINT ["/usr/local/bin/qemu-rpi4"]
