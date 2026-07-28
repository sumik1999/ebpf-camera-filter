FROM debian:bullseye

# Prevent interactive prompts during build
ENV DEBIAN_FRONTEND=noninteractive
# Allow building as root (required for Docker builds)
ENV FORCE_UNSAFE_CONFIGURE=1

# Install ALL build dependencies for buildroot (with --fix-missing for transient network errors)
RUN apt-get update && \
    apt-get install -y --fix-missing --no-install-recommends \
    build-essential \
    git \
    bc \
    bison \
    flex \
    libssl-dev \
    make \
    libc6-dev \
    libncurses5-dev \
    wget \
    unzip \
    curl \
    ca-certificates \
    python3 \
    python3-dev \
    python3-matplotlib \
    graphviz \
    file \
    gperf \
    libelf-dev \
    texinfo \
    cpio \
    rsync \
    xz-utils \
    bzip2 \
    && rm -rf /var/lib/apt/lists/*

# Source is mounted at /build by build-docker.sh. Keeping source and output out
# of this image makes Docker caching effective and ensures artifacts are written
# directly to the host checkout.
WORKDIR /build

CMD ["bash", "-c", "cd buildroot && make clean && make rpi4_camera_ebpf_defconfig && make -j$(nproc) && ls -la output/images/"]
